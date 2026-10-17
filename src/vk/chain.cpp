// libslang-vk — Chain implementation (M1): pipelines, FBOs, samplers,
// descriptor sets and the per-frame execution loop.
//
// Descriptor conventions come from the translator (README "Consuming"):
//   set 1 binding 0 — vertex UBO (MVP),   set 2 — fragment combined image
//   samplers (dense), set 3 binding 0 — fragment params UBO (std140 offsets
//   already reflected in UniformBlock::members).
//
// M1 semantics (ported from the reference host, PROVENANCE.md):
//   - pass output sizes: Source/Viewport/Absolute rules, half-up rounding,
//     viewport base aspect-corrected per the F6 pin;
//   - UBO fill: sizes as vec4(w,h,1/w,1/h), FrameCount with frame_count_mod,
//     FrameDirection/FrameTime/PASSINDEX/Rotation, <Texture>Size forms,
//     parameters (preset override -> pragma default -> zero);
//   - MVP divergence (documented): the Vulkan-native quad pairs uv (0,0)
//     with NDC (-1,-1) top-left, so every pass gets the unit-quad -> NDC
//     matrix and no per-draw mirror exists anywhere in the chain.
//
// Not in M1 (unsupported_feature): history/feedback rings, LUT image
// sampling, rotation != None, swapchain targets, final-pass blit when the
// last pass does not resolve to the output target size.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "vk_internal.hpp"
#include "impls.hpp"

namespace slv::vk {

auto MapTargetFormat(const std::string &pragmaFormat, bool srgbFramebuffer, bool floatFramebuffer) -> VkFormat
{
        // Preset flags first (RetroArch order), then #pragma format tokens;
        // unknown tokens fall back to RGBA8 (documented divergence, SPEC).
        if (floatFramebuffer)
                return VK_FORMAT_R16G16B16A16_SFLOAT;
        if (srgbFramebuffer && pragmaFormat.empty())
                return VK_FORMAT_R8G8B8A8_SRGB;
        if (pragmaFormat.empty() || pragmaFormat == "RGBA8")
                return VK_FORMAT_R8G8B8A8_UNORM;
        if (pragmaFormat == "R8")
                return VK_FORMAT_R8_UNORM;
        if (pragmaFormat == "R16F")
                return VK_FORMAT_R16_SFLOAT;
        if (pragmaFormat == "R32F")
                return VK_FORMAT_R32_SFLOAT;
        if (pragmaFormat == "RGBA16F")
                return VK_FORMAT_R16G16B16A16_SFLOAT;
        if (pragmaFormat == "RGBA32F")
                return VK_FORMAT_R32G32B32A32_SFLOAT;
        if (pragmaFormat == "RGBA16")
                return VK_FORMAT_R16G16B16A16_UNORM;
        if (pragmaFormat == "sRGB8" || pragmaFormat == "SRGB8_ALPHA8")
                return VK_FORMAT_R8G8B8A8_SRGB;
        if (pragmaFormat == "A2B10G10R10_UNORM_PACK32")
                return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        return VK_FORMAT_R8G8B8A8_UNORM;
}

auto FormatTexelSize(VkFormat format) -> uint32_t
{
        switch (format) {
                case VK_FORMAT_R8_UNORM:
                case VK_FORMAT_R8_SRGB:
                        return 1;
                case VK_FORMAT_R16_SFLOAT:
                case VK_FORMAT_R16_UNORM:
                        return 2;
                case VK_FORMAT_R32_SFLOAT:
                case VK_FORMAT_R8G8B8A8_UNORM:
                case VK_FORMAT_R8G8B8A8_SRGB:
                case VK_FORMAT_B8G8R8A8_UNORM:
                case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
                        return 4;
                case VK_FORMAT_R16G16B16A16_SFLOAT:
                case VK_FORMAT_R16G16B16A16_UNORM:
                        return 8;
                case VK_FORMAT_R32G32B32A32_SFLOAT:
                        return 16;
                default:
                        return 0;
        }
}

auto DeriveViewportSize(uint32_t targetWidth, uint32_t targetHeight, uint32_t gameWidth, uint32_t gameHeight)
    -> std::pair<uint32_t, uint32_t>
{
        // Aspect-corrected game rect inside the output target (F6 pin,
        // half-up rounding; pillarbox/letterbox).
        if (targetWidth == 0 || targetHeight == 0 || gameWidth == 0 || gameHeight == 0)
                return {targetWidth, targetHeight};
        const float gameAspect = static_cast<float>(gameWidth) / static_cast<float>(gameHeight);
        if (gameAspect > static_cast<float>(targetWidth) / static_cast<float>(targetHeight))
                return {targetWidth,
                    static_cast<uint32_t>(std::max(1.0F, std::floor(static_cast<float>(targetWidth) / gameAspect + 0.5F)))};
        return {static_cast<uint32_t>(std::max(1.0F, std::floor(static_cast<float>(targetHeight) * gameAspect + 0.5F))),
            targetHeight};
}

namespace {

void WriteFloat(std::vector<uint8_t> &bytes, uint32_t offset, float value)
{
        std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

void WriteInt(std::vector<uint8_t> &bytes, uint32_t offset, int32_t value)
{
        std::memcpy(bytes.data() + offset, &value, sizeof(int32_t));
}

void WriteUint(std::vector<uint8_t> &bytes, uint32_t offset, uint32_t value)
{
        std::memcpy(bytes.data() + offset, &value, sizeof(uint32_t));
}

} // namespace

auto FillBlockBytes(const UniformBlock &block, const PassDef &passDef, const SlangTranslation &translation,
    const std::vector<ParamOverride> &presetOverrides, const BlockFillContext &ctx) -> std::vector<uint8_t>
{
        uint32_t total = 0;
        for (const UniformMember &member : block.members)
                total = std::max(total, member.offset + member.size);
        std::vector<uint8_t> out(std::max<uint32_t>(total, 16), 0);

        // Unit-quad -> NDC matrix, column-major (GLSL convention). Vulkan NDC
        // (-1,-1) is top-left, matching texture uv (0,0), so no flip anywhere.
        static constexpr float kQuadToNdc[16] = {2.F, 0.F, 0.F, 0.F, 0.F, 2.F, 0.F, 0.F, 0.F, 0.F, 1.F, 0.F, -1.F, -1.F, 0.F, 1.F};

        const auto writeVec4 = [&](const UniformMember &member, float x, float y, float z, float w) {
                WriteFloat(out, member.offset, x);
                WriteFloat(out, member.offset + 4, y);
                if (member.type == UniformType::Vec4 || member.type == UniformType::IVec4
                    || member.type == UniformType::UVec4) {
                        WriteFloat(out, member.offset + 8, z);
                        WriteFloat(out, member.offset + 12, w);
                }
        };

        const auto sizeVec = [](uint32_t w, uint32_t h) {
                const float fw = static_cast<float>(w);
                const float fh = static_cast<float>(h);
                return std::make_tuple(fw, fh, fw > 0.0F ? 1.0F / fw : 0.0F, fh > 0.0F ? 1.0F / fh : 0.0F);
        };

        for (const UniformMember &member : block.members) {
                const std::string &name = member.name;
                if (member.arraySize > 0)
                        continue; // Size members are never arrays; leave zeros.

                if (name == "MVP") {
                        for (int i = 0; i < 16; ++i)
                                WriteFloat(out, member.offset + 4 * i, kQuadToNdc[i]);
                        continue;
                }
                if (name == "OutputSize") {
                        const auto [x, y, z, w] = sizeVec(ctx.outputWidth, ctx.outputHeight);
                        writeVec4(member, x, y, z, w);
                        continue;
                }
                if (name == "FinalViewportSize") {
                        const auto [x, y, z, w] = sizeVec(ctx.targetWidth, ctx.targetHeight);
                        writeVec4(member, x, y, z, w);
                        continue;
                }
                if (name == "OriginalSize") {
                        const auto [x, y, z, w] = sizeVec(ctx.gameWidth, ctx.gameHeight);
                        writeVec4(member, x, y, z, w);
                        continue;
                }
                if (name == "SourceSize") {
                        const auto [x, y, z, w] = sizeVec(ctx.inputWidth, ctx.inputHeight);
                        writeVec4(member, x, y, z, w);
                        continue;
                }
                if (name == "FrameCount") {
                        uint64_t count = ctx.frameCount;
                        if (passDef.frameCountMod > 0)
                                count %= passDef.frameCountMod;
                        WriteUint(out, member.offset, static_cast<uint32_t>(count));
                        continue;
                }
                if (name == "FrameDirection") {
                        WriteInt(out, member.offset, ctx.frameDirection);
                        continue;
                }
                if (name == "FrameTime") {
                        WriteFloat(out, member.offset, ctx.frameDeltaUs);
                        continue;
                }
                if (name == "PASSINDEX") {
                        WriteInt(out, member.offset, static_cast<int32_t>(ctx.passIndex));
                        continue;
                }
                if (name == "Rotation") {
                        WriteUint(out, member.offset, 0); // M1 frontend never rotates (ported semantic).
                        continue;
                }

                // <TextureName>Size members (ported resolution order).
                if (name.size() > 4 && name.compare(name.size() - 4, 4, "Size") == 0) {
                        std::string texName = name.substr(0, name.size() - 4);
                        if (texName.size() > 8 && texName.compare(texName.size() - 8, 8, "Feedback") == 0)
                                texName = texName.substr(0, texName.size() - 8); // Feedback shares pass output size.
                        uint32_t w = 0;
                        uint32_t h = 0;
                        if (texName == "Source") {
                                w = ctx.inputWidth;
                                h = ctx.inputHeight;
                        } else if (texName == "Original" || texName.rfind("OriginalHistory", 0) == 0) {
                                w = ctx.gameWidth;
                                h = ctx.gameHeight;
                        } else if (texName.rfind("PassOutput", 0) == 0 || texName.rfind("PassFeedback", 0) == 0) {
                                // Parse the trailing index after the 10-char prefix.
                                if (texName.size() > 10) {
                                        const int idx = std::atoi(texName.c_str() + 10);
                                        if (idx >= 0 && idx < static_cast<int>(ctx.passCount) && ctx.passOutputSizes != nullptr
                                            && static_cast<size_t>(idx) < ctx.passOutputSizes->size()) {
                                                w = (*ctx.passOutputSizes)[static_cast<size_t>(idx)].first;
                                                h = (*ctx.passOutputSizes)[static_cast<size_t>(idx)].second;
                                        }
                                }
                        } else {
                                // Alias or lookup texture: alias sizes come from the owning pass.
                                if (CaseInsensitiveEqual(passDef.alias, texName)) {
                                        w = ctx.outputWidth;
                                        h = ctx.outputHeight;
                                }
                                // LUT sizes resolve in M2 when LUT images are uploaded.
                        }
                        const auto [x, y, z, ww] = sizeVec(w, h);
                        writeVec4(member, x, y, z, ww);
                        continue;
                }

                // Shader parameters: preset override, then pragma default, then zero.
                // Name matching is case-insensitive: the parser lowercases .slangp
                // keys while shader members keep their declared case.
                float value = 0.0F;
                bool found = false;
                for (const ParamOverride &override : presetOverrides) {
                        if (CaseInsensitiveEqual(override.name, name)) {
                                value = override.value;
                                found = true;
                                break;
                        }
                }
                if (!found) {
                        for (const slv::preset::ParamInfo &param : translation.parameters) {
                                if (CaseInsensitiveEqual(param.name, name)) {
                                        value = param.defaultValue;
                                        found = true;
                                        break;
                                }
                        }
                }
                WriteFloat(out, member.offset, value);
        }
        return out;
}

} // namespace slv::vk

namespace slv {

// Core-layer vocabulary + internal Vulkan helpers, unqualified in these TUs.
using slv::preset::FilterMode;
using slv::preset::ParamInfo;
using slv::preset::ParamOverride;
using slv::preset::PassDef;
using slv::preset::PassSpirv;
using slv::preset::SlangTranslation;
using slv::preset::ScaleRule;
using slv::preset::ScaleType;
using slv::preset::TextureDef;
using slv::preset::TextureRef;
using slv::preset::UniformBlock;
using slv::preset::UniformMember;
using slv::preset::UniformType;
using slv::preset::WrapMode;
using vk::VkImageWithView;

struct Chain::Impl {
        struct Ubo {
                VkBuffer buffer = VK_NULL_HANDLE;
                VmaAllocation allocation = VK_NULL_HANDLE;
                uint8_t *mapped = nullptr;
                VkDeviceSize size = 0;
        };

        struct PassRuntime {
                PassDef def;
                SlangTranslation translation;
                VkShaderModule vertexModule = VK_NULL_HANDLE;
                VkShaderModule fragmentModule = VK_NULL_HANDLE;
                VkPipeline pipeline = VK_NULL_HANDLE;
                VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
                uint32_t outputWidth = 0;
                uint32_t outputHeight = 0;
                uint32_t allocatedWidth = 0;   // Actual FBO allocation (drift detection).
                uint32_t allocatedHeight = 0;
                VkFormat targetFormat = VK_FORMAT_R8G8B8A8_UNORM;
                VkImageWithView target;
                VkFramebuffer framebuffer = VK_NULL_HANDLE; // Over own target (intermediate passes).
                Ubo vertexUbo;
                Ubo fragmentUbo;
                VkDescriptorSet set1 = VK_NULL_HANDLE;
                VkDescriptorSet set2 = VK_NULL_HANDLE;
                VkDescriptorSet set3 = VK_NULL_HANDLE;
        };

        Device *device = nullptr;
        std::vector<PassRuntime> passes;
        std::vector<slv::preset::TextureDef> presetTextures;   // Recorded LUT declarations (M2 uploads them).
        std::vector<ParamOverride> presetParameterOverrides;   // From the .slangp parameters list.
        std::unordered_map<std::string, float> paramOverrides; // setParameter() runtime overrides.
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet emptySet = VK_NULL_HANDLE;
        VkDescriptorSetLayout emptyLayout = VK_NULL_HANDLE;
        std::unordered_map<size_t, VkDescriptorSetLayout> set2Layouts; // Binding count -> layout
        VkDescriptorSetLayout set1Layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout set3Layout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unordered_map<uint64_t, VkSampler> samplers; // (wrap|filter|mipmap) -> sampler
        std::unordered_map<VkFormat, VkRenderPass> renderPasses;
        std::unordered_map<VkImageView, VkFramebuffer> outputFramebuffers;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = VK_NULL_HANDLE;

        uint32_t gameWidth = 0;
        uint32_t gameHeight = 0;
        uint32_t targetWidth = 0;
        uint32_t targetHeight = 0;
        VkFormat targetFormat = VK_FORMAT_R8G8B8A8_UNORM;

        ~Impl()
        {
                if (device == nullptr)
                        return;
                VkDevice dev = device->device();
                for (PassRuntime &pass : passes) {
                        if (pass.pipeline != VK_NULL_HANDLE)
                                vkDestroyPipeline(dev, pass.pipeline, nullptr);
                        if (pass.pipelineLayout != VK_NULL_HANDLE)
                                vkDestroyPipelineLayout(dev, pass.pipelineLayout, nullptr);
                        if (pass.vertexModule != VK_NULL_HANDLE)
                                vkDestroyShaderModule(dev, pass.vertexModule, nullptr);
                        if (pass.fragmentModule != VK_NULL_HANDLE)
                                vkDestroyShaderModule(dev, pass.fragmentModule, nullptr);
                        DestroyTarget(pass);
                        DestroyUbo(pass.vertexUbo);
                        DestroyUbo(pass.fragmentUbo);
                }
                for (auto &entry : outputFramebuffers)
                        vkDestroyFramebuffer(dev, entry.second, nullptr);
                for (auto &entry : renderPasses)
                        vkDestroyRenderPass(dev, entry.second, nullptr);
                for (auto &entry : samplers)
                        vkDestroySampler(dev, entry.second, nullptr);
                for (auto &entry : set2Layouts)
                        vkDestroyDescriptorSetLayout(dev, entry.second, nullptr);
                if (set1Layout != VK_NULL_HANDLE)
                        vkDestroyDescriptorSetLayout(dev, set1Layout, nullptr);
                if (set3Layout != VK_NULL_HANDLE)
                        vkDestroyDescriptorSetLayout(dev, set3Layout, nullptr);
                if (emptyLayout != VK_NULL_HANDLE)
                        vkDestroyDescriptorSetLayout(dev, emptyLayout, nullptr);
                if (descriptorPool != VK_NULL_HANDLE)
                        vkDestroyDescriptorPool(dev, descriptorPool, nullptr);
                if (pipelineLayout != VK_NULL_HANDLE)
                        vkDestroyPipelineLayout(dev, pipelineLayout, nullptr);
                if (vertexBuffer != VK_NULL_HANDLE)
                        vmaDestroyBuffer(device->impl().allocator, vertexBuffer, vertexAllocation);
        }

        void DestroyTarget(PassRuntime &pass)
        {
                if (device == nullptr)
                        return;
                VkDevice dev = device->device();
                if (pass.framebuffer != VK_NULL_HANDLE)
                        vkDestroyFramebuffer(dev, pass.framebuffer, nullptr);
                if (pass.target.view != VK_NULL_HANDLE)
                        vkDestroyImageView(dev, pass.target.view, nullptr);
                if (pass.target.image != VK_NULL_HANDLE)
                        vmaDestroyImage(device->impl().allocator, pass.target.image, pass.target.allocation);
                pass.framebuffer = VK_NULL_HANDLE;
                pass.target = {};
                pass.allocatedWidth = 0;
                pass.allocatedHeight = 0;
        }

        void DestroyUbo(Ubo &ubo)
        {
                if (ubo.buffer != VK_NULL_HANDLE)
                        vmaDestroyBuffer(device->impl().allocator, ubo.buffer, ubo.allocation);
                ubo = {};
        }

        auto MakeUbo(VkDeviceSize size, Ubo &out) -> std::expected<void, Error>
        {
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = size;
                bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                VmaAllocationCreateInfo allocInfo{};
                allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
                allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                    | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                VmaAllocationInfo mappedInfo{};
                if (vmaCreateBuffer(device->impl().allocator, &bufferInfo, &allocInfo, &out.buffer, &out.allocation,
                        &mappedInfo)
                    != VK_SUCCESS)
                        return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "uniform buffer allocation failed"));
                out.mapped = static_cast<uint8_t *>(mappedInfo.pMappedData);
                out.size = size;
                return {};
        }

        auto MakeTarget(PassRuntime &pass, uint32_t width, uint32_t height) -> std::expected<void, Error>
        {
                DestroyTarget(pass);
                VkImageCreateInfo imageInfo{};
                imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.format = pass.targetFormat;
                imageInfo.extent = {width, height, 1};
                imageInfo.mipLevels = 1;
                imageInfo.arrayLayers = 1;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                VmaAllocationCreateInfo allocInfo{};
                allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                if (vmaCreateImage(device->impl().allocator, &imageInfo, &allocInfo, &pass.target.image,
                        &pass.target.allocation, nullptr)
                    != VK_SUCCESS)
                        return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "pass target allocation failed"));

                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = pass.target.image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = pass.targetFormat;
                viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                SLV_VK(vkCreateImageView(device->device(), &viewInfo, nullptr, &pass.target.view), Errc::vk_error);

                const VkRenderPass renderPass = GetRenderPass(pass.targetFormat);
                VkFramebufferCreateInfo fbInfo{};
                fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbInfo.renderPass = renderPass;
                fbInfo.attachmentCount = 1;
                fbInfo.pAttachments = &pass.target.view;
                fbInfo.width = width;
                fbInfo.height = height;
                fbInfo.layers = 1;
                SLV_VK(vkCreateFramebuffer(device->device(), &fbInfo, nullptr, &pass.framebuffer), Errc::vk_error);

                // New images start UNDEFINED; the renderpass expects every
                // target to rest in SHADER_READ_ONLY between uses (its
                // initialLayout), so transition once here.
                auto transitioned = device->impl().SubmitAndWait([&](VkCommandBuffer cmd) {
                        VkImageMemoryBarrier barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        barrier.image = pass.target.image;
                        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);
                });
                if (!transitioned)
                        return std::unexpected(transitioned.error());

                pass.outputWidth = width;
                pass.outputHeight = height;
                pass.allocatedWidth = width;
                pass.allocatedHeight = height;
                return {};
        }

        auto GetRenderPass(VkFormat format) -> VkRenderPass
        {
                const auto it = renderPasses.find(format);
                if (it != renderPasses.end())
                        return it->second;

                VkAttachmentDescription attachment{};
                attachment.format = format;
                attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                // Every target rests in SHADER_READ_ONLY between uses: the next
                // pass samples it, the next frame re-renders into it, and the
                // host composites the final one.
                attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                VkSubpassDescription subpass{};
                subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                subpass.colorAttachmentCount = 1;
                subpass.pColorAttachments = &colorRef;

                std::array<VkSubpassDependency, 2> deps{};
                deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
                deps[0].dstSubpass = 0;
                deps[0].srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                deps[0].srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                deps[1].srcSubpass = 0;
                deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
                deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                deps[1].dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                deps[1].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

                VkRenderPassCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                info.attachmentCount = 1;
                info.pAttachments = &attachment;
                info.subpassCount = 1;
                info.pSubpasses = &subpass;
                info.dependencyCount = static_cast<uint32_t>(deps.size());
                info.pDependencies = deps.data();

                VkRenderPass renderPass = VK_NULL_HANDLE;
                if (vkCreateRenderPass(device->device(), &info, nullptr, &renderPass) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                renderPasses.emplace(format, renderPass);
                return renderPass;
        }

        auto GetSampler(const PassDef &def) -> VkSampler
        {
                const uint64_t key = (static_cast<uint64_t>(def.wrap) << 8) | (static_cast<uint64_t>(def.filter) << 1)
                    | static_cast<uint64_t>(def.mipmapInput);
                const auto it = samplers.find(key);
                if (it != samplers.end())
                        return it->second;

                VkSamplerCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                info.magFilter = def.filter == FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
                info.minFilter = info.magFilter;
                info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                switch (def.wrap) {
                        case WrapMode::Repeat: info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
                        case WrapMode::MirroredRepeat: info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
                        case WrapMode::ClampToEdge: break;
                }
                info.anisotropyEnable = VK_FALSE;
                info.maxLod = def.mipmapInput ? 128.0F : 0.0F;

                VkSampler sampler = VK_NULL_HANDLE;
                if (vkCreateSampler(device->device(), &info, nullptr, &sampler) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                samplers.emplace(key, sampler);
                return sampler;
        }

        auto GetSet2Layout(size_t bindingCount) -> VkDescriptorSetLayout
        {
                const auto it = set2Layouts.find(bindingCount);
                if (it != set2Layouts.end())
                        return it->second;
                std::vector<VkDescriptorSetLayoutBinding> bindings(bindingCount);
                for (size_t i = 0; i < bindingCount; ++i)
                        bindings[i] = {static_cast<uint32_t>(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
                VkDescriptorSetLayoutCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                info.bindingCount = static_cast<uint32_t>(bindings.size());
                info.pBindings = bindings.empty() ? nullptr : bindings.data();
                VkDescriptorSetLayout layout = VK_NULL_HANDLE;
                if (vkCreateDescriptorSetLayout(device->device(), &info, nullptr, &layout) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                set2Layouts.emplace(bindingCount, layout);
                return layout;
        }

        auto MakeLayout(uint32_t binding, VkShaderStageFlags stage) -> VkDescriptorSetLayout
        {
                const VkDescriptorSetLayoutBinding bindingDef = {binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stage,
                    nullptr};
                VkDescriptorSetLayoutCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                info.bindingCount = 1;
                info.pBindings = &bindingDef;
                VkDescriptorSetLayout layout = VK_NULL_HANDLE;
                if (vkCreateDescriptorSetLayout(device->device(), &info, nullptr, &layout) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                return layout;
        }

        auto AllocateSet(VkDescriptorSetLayout layout) -> VkDescriptorSet
        {
                VkDescriptorSetAllocateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                info.descriptorPool = descriptorPool;
                info.descriptorSetCount = 1;
                info.pSetLayouts = &layout;
                VkDescriptorSet set = VK_NULL_HANDLE;
                if (vkAllocateDescriptorSets(device->device(), &info, &set) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                return set;
        }
};

namespace {

// Semantics ported from the reference host (F6 pin): Viewport-scale passes
// resolve against the aspect-corrected game rect inside the output target.
void ResolvePassSizes(Chain::Impl &impl, uint32_t gameW, uint32_t gameH, uint32_t targetW, uint32_t targetH)
{
        const auto [viewportW, viewportH] = vk::DeriveViewportSize(targetW, targetH, gameW, gameH);
        float prevW = static_cast<float>(gameW);
        float prevH = static_cast<float>(gameH);
        for (Chain::Impl::PassRuntime &pass : impl.passes) {
                const auto resolve = [](const ScaleRule &rule, float inputSize, float viewportSize) {
                        switch (rule.type) {
                                case ScaleType::Viewport: return viewportSize * rule.scale;
                                case ScaleType::Absolute: return rule.scale;
                                case ScaleType::Source: break;
                        }
                        return inputSize * rule.scale;
                };
                const float outW = resolve(pass.def.scaleX, prevW, static_cast<float>(viewportW));
                const float outH = resolve(pass.def.scaleY, prevH, static_cast<float>(viewportH));
                pass.outputWidth = static_cast<uint32_t>(std::max(1.0F, std::floor(outW + 0.5F)));
                pass.outputHeight = static_cast<uint32_t>(std::max(1.0F, std::floor(outH + 0.5F)));
                prevW = static_cast<float>(pass.outputWidth);
                prevH = static_cast<float>(pass.outputHeight);
        }
}

auto ResolveTexture(const std::string &name, size_t passIndex, Chain::Impl &impl, const InputFrame &input,
    Chain::Impl::PassRuntime &pass, VkImageView &view, VkSampler &sampler) -> std::expected<void, Error>
{
        const Chain::Impl::PassRuntime &self = impl.passes[passIndex];
        sampler = impl.GetSampler(self.def);
        if (sampler == VK_NULL_HANDLE)
                return std::unexpected(SLV_ERROR(Errc::vk_error, "sampler creation failed"));

        if (name == "Source") {
                view = passIndex == 0 ? input.view : impl.passes[passIndex - 1].target.view;
                return {};
        }
        if (name == "Original" || name.rfind("OriginalHistory", 0) == 0) {
                // History rings land in M2; Original itself is the chain input.
                if (name != "Original")
                        return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                            "OriginalHistory# taps land in M2 (history rings)"));
                view = input.view;
                return {};
        }
        if (name.rfind("PassOutput", 0) == 0 || name.rfind("PASSOUTPUT", 0) == 0) {
                if (name.size() <= 10)
                        return std::unexpected(SLV_ERROR(Errc::preset_parse, "malformed PassOutput# reference: " + name));
                const int idx = std::atoi(name.c_str() + 10);
                if (idx < 0 || static_cast<size_t>(idx) >= impl.passes.size())
                        return std::unexpected(SLV_ERROR(Errc::preset_parse, "PassOutput index out of range: " + name));
                if (static_cast<size_t>(idx) == passIndex)
                        return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                            "feedback reads of a pass's own output land in M2"));
                view = impl.passes[static_cast<size_t>(idx)].target.view;
                return {};
        }
        if (name.rfind("PASSPREV", 0) == 0 || name.rfind("PassFeedback", 0) == 0)
                return std::unexpected(
                    SLV_ERROR(Errc::unsupported_feature, "PASSPREV/PassFeedback taps land in M2 (feedback rings)"));
        for (size_t j = 0; j < passIndex; ++j)
                if (!impl.passes[j].def.alias.empty() && impl.passes[j].def.alias == name) {
                        view = impl.passes[j].target.view;
                        return {};
                }
        for (const slv::preset::TextureDef &texture : impl.presetTextures)
                if (texture.name == name)
                        return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                            "LUT texture '" + name + "' image upload lands in M2"));
        return std::unexpected(SLV_ERROR(Errc::preset_parse, "unresolved texture reference: " + name));
}

} // namespace

auto Chain::create(Device &device, const Preset &preset, const ChainOptions &options) -> std::expected<Chain, Error>
{
        if (options.outputWidth == 0 || options.outputHeight == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Chain::create: zero output size"));
        if (preset.passCount() == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Chain::create: preset has no passes"));
        if (vk::FormatTexelSize(options.outputFormat) == 0)
                return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                    "Chain::create: unsupported output VkFormat"));

        Chain out;
        out.impl_ = std::make_unique<Impl>();
        Impl &impl = *out.impl_;
        impl.device = &device;
        impl.targetWidth = options.outputWidth;
        impl.targetHeight = options.outputHeight;
        impl.targetFormat = options.outputFormat;
        impl.presetTextures = preset.impl().parsed.textures;
        impl.presetParameterOverrides = preset.impl().parsed.parameters;

        const auto &presetPasses = preset.impl().passDefs;
        const auto &presetTranslations = preset.impl().translations;
        const auto &presetSpirv = preset.impl().spirv;
        const size_t passCount = presetPasses.size();

        // Layouts: set0 empty placeholder, set1 vertex UBO, set2 textures, set3 params UBO.
        VkDescriptorSetLayoutCreateInfo emptyInfo{};
        emptyInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        SLV_VK(vkCreateDescriptorSetLayout(device.device(), &emptyInfo, nullptr, &impl.emptyLayout), Errc::vk_error);

        VkDescriptorSetLayoutBinding set1Binding = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
            nullptr};
        VkDescriptorSetLayoutCreateInfo set1Info{};
        set1Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set1Info.bindingCount = 1;
        set1Info.pBindings = &set1Binding;
        SLV_VK(vkCreateDescriptorSetLayout(device.device(), &set1Info, nullptr, &impl.set1Layout), Errc::vk_error);

        VkDescriptorSetLayoutBinding set3Binding = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo set3Info{};
        set3Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set3Info.bindingCount = 1;
        set3Info.pBindings = &set3Binding;
        SLV_VK(vkCreateDescriptorSetLayout(device.device(), &set3Info, nullptr, &impl.set3Layout), Errc::vk_error);

        // Descriptor pool: worst case 4 sets per pass + uniform/sampler counts.
        VkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(2 * passCount)};
        uint32_t samplerCount = 0;
        for (const SlangTranslation &translation : presetTranslations)
                samplerCount += static_cast<uint32_t>(translation.fragment.textures.size());
        poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, std::max(samplerCount, 1u)};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = static_cast<uint32_t>(4 * passCount);
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        SLV_VK(vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &impl.descriptorPool), Errc::vk_error);
        impl.emptySet = impl.AllocateSet(impl.emptyLayout);
        if (impl.emptySet == VK_NULL_HANDLE)
                return std::unexpected(SLV_ERROR(Errc::vk_error, "empty descriptor set allocation failed"));

        // Per-pass resources.
        impl.passes.resize(passCount);
        for (size_t i = 0; i < passCount; ++i) {
                Chain::Impl::PassRuntime &pass = impl.passes[i];
                pass.def = presetPasses[i];
                pass.translation = presetTranslations[i];
                pass.targetFormat = vk::MapTargetFormat(
                    pass.translation.pragmaFormat, pass.def.srgbFramebuffer, pass.def.floatFramebuffer);

                VkShaderModuleCreateInfo moduleInfo{};
                moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                moduleInfo.pCode = reinterpret_cast<const uint32_t *>(presetSpirv[i].vertex.data());
                moduleInfo.codeSize = presetSpirv[i].vertex.size();
                SLV_VK(vkCreateShaderModule(device.device(), &moduleInfo, nullptr, &pass.vertexModule), Errc::shader_compile);
                moduleInfo.pCode = reinterpret_cast<const uint32_t *>(presetSpirv[i].fragment.data());
                moduleInfo.codeSize = presetSpirv[i].fragment.size();
                SLV_VK(vkCreateShaderModule(device.device(), &moduleInfo, nullptr, &pass.fragmentModule), Errc::shader_compile);

                // Pipeline layout: [set0 empty, set1 vertex UBO, set2 textures, set3 params UBO],
                // with empty slots where the pass declares no such resource.
                const bool hasVertexUbo = !pass.translation.vertex.uniformBlocks.empty();
                const bool hasFragmentUbo = !pass.translation.fragment.uniformBlocks.empty();
                const size_t textureCount = pass.translation.fragment.textures.size();
                const VkDescriptorSetLayout set2Layout = impl.GetSet2Layout(textureCount);
                if (set2Layout == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "set2 layout creation failed"));
                std::array<VkDescriptorSetLayout, 4> setLayouts = {impl.emptyLayout,
                    hasVertexUbo ? impl.set1Layout : impl.emptyLayout, set2Layout,
                    hasFragmentUbo ? impl.set3Layout : impl.emptyLayout};
                VkPipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
                layoutInfo.pSetLayouts = setLayouts.data();
                SLV_VK(vkCreatePipelineLayout(device.device(), &layoutInfo, nullptr, &pass.pipelineLayout), Errc::vk_error);

                std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
                stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = pass.vertexModule;
                stages[0].pName = "main";
                stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = pass.fragmentModule;
                stages[1].pName = "main";

                const VkVertexInputBindingDescription vertexBinding = {0, 24, VK_VERTEX_INPUT_RATE_VERTEX};
                std::array<VkVertexInputAttributeDescription, 2> attributes{};
                attributes[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
                attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 16};
                VkPipelineVertexInputStateCreateInfo vertexInput{};
                vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertexInput.vertexBindingDescriptionCount = 1;
                vertexInput.pVertexBindingDescriptions = &vertexBinding;
                vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                vertexInput.pVertexAttributeDescriptions = attributes.data();

                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

                VkPipelineViewportStateCreateInfo viewportState{};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.scissorCount = 1;

                VkPipelineRasterizationStateCreateInfo raster{};
                raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                raster.polygonMode = VK_POLYGON_MODE_FILL;
                raster.cullMode = VK_CULL_MODE_NONE;
                raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                raster.lineWidth = 1.0F;

                VkPipelineMultisampleStateCreateInfo multisample{};
                multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                VkPipelineColorBlendAttachmentState blendAttachment{};
                blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                VkPipelineColorBlendStateCreateInfo blend{};
                blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                blend.attachmentCount = 1;
                blend.pAttachments = &blendAttachment;

                std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
                VkPipelineDynamicStateCreateInfo dynamic{};
                dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
                dynamic.pDynamicStates = dynamicStates.data();

                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages.data();
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &raster;
                pipelineInfo.pMultisampleState = &multisample;
                pipelineInfo.pColorBlendState = &blend;
                pipelineInfo.pDynamicState = &dynamic;
                pipelineInfo.layout = pass.pipelineLayout;
                pipelineInfo.renderPass = impl.GetRenderPass(pass.targetFormat);
                if (pipelineInfo.renderPass == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "render pass creation failed"));
                SLV_VK(vkCreateGraphicsPipelines(device.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                    &pass.pipeline), Errc::shader_compile);

                // Descriptor sets (empty slots bound as real empty sets).
                pass.set1 = impl.AllocateSet(hasVertexUbo ? impl.set1Layout : impl.emptyLayout);
                pass.set2 = impl.AllocateSet(set2Layout);
                pass.set3 = impl.AllocateSet(hasFragmentUbo ? impl.set3Layout : impl.emptyLayout);
                if (pass.set1 == VK_NULL_HANDLE || pass.set2 == VK_NULL_HANDLE || pass.set3 == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "descriptor set allocation failed"));

                // UBOs sized by the reflected blocks; UBO descriptors written once.
                if (hasVertexUbo) {
                        const UniformBlock &block = pass.translation.vertex.uniformBlocks[0];
                        uint32_t bytes = 16;
                        for (const UniformMember &member : block.members)
                                bytes = std::max(bytes, member.offset + member.size);
                        auto made = impl.MakeUbo(bytes, pass.vertexUbo);
                        if (!made)
                                return std::unexpected(made.error());
                        VkDescriptorBufferInfo bufferInfo{pass.vertexUbo.buffer, 0, VK_WHOLE_SIZE};
                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = pass.set1;
                        write.dstBinding = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        write.descriptorCount = 1;
                        write.pBufferInfo = &bufferInfo;
                        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
                }
                if (hasFragmentUbo) {
                        const UniformBlock &block = pass.translation.fragment.uniformBlocks[0];
                        uint32_t bytes = 16;
                        for (const UniformMember &member : block.members)
                                bytes = std::max(bytes, member.offset + member.size);
                        auto made = impl.MakeUbo(bytes, pass.fragmentUbo);
                        if (!made)
                                return std::unexpected(made.error());
                        VkDescriptorBufferInfo bufferInfo{pass.fragmentUbo.buffer, 0, VK_WHOLE_SIZE};
                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = pass.set3;
                        write.dstBinding = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        write.descriptorCount = 1;
                        write.pBufferInfo = &bufferInfo;
                        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
                }
        }

        // Fullscreen unit quad: positions in [0,1], uv = position (Vulkan-native
        // orientation; see the MVP comment at the top of this file).
        static constexpr float kQuad[4][6] = {
            {0.F, 0.F, 0.F, 1.F, 0.F, 0.F},
            {1.F, 0.F, 0.F, 1.F, 1.F, 0.F},
            {0.F, 1.F, 0.F, 1.F, 0.F, 1.F},
            {1.F, 1.F, 0.F, 1.F, 1.F, 1.F},
        };
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(kQuad);
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo vertexAllocInfo{};
        vertexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        vertexAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo vertexMapped{};
        if (vmaCreateBuffer(device.impl().allocator, &bufferInfo, &vertexAllocInfo, &impl.vertexBuffer,
                &impl.vertexAllocation, &vertexMapped)
            != VK_SUCCESS)
                return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "vertex buffer allocation failed"));
        std::memcpy(vertexMapped.pMappedData, kQuad, sizeof(kQuad));
        return out;
}

Chain::~Chain() = default;
Chain::Chain(Chain &&) noexcept = default;
auto Chain::operator=(Chain &&) noexcept -> Chain & = default;

auto Chain::resize(uint32_t /*inputWidth*/, uint32_t /*inputHeight*/, uint32_t outputWidth, uint32_t outputHeight)
    -> std::expected<void, Error>
{
        if (outputWidth == 0 || outputHeight == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Chain::resize: zero output size"));
        impl_->targetWidth = outputWidth;
        impl_->targetHeight = outputHeight;
        return {};
}

auto Chain::setParameter(std::string_view id, float value) -> std::expected<void, Error>
{
        if (id.empty())
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Chain::setParameter: empty id"));
        impl_->paramOverrides.insert_or_assign(std::string(id), value);
        return {};
}

auto Chain::passOutputSize(uint32_t passIndex) const -> std::expected<std::pair<uint32_t, uint32_t>, Error>
{
        if (passIndex >= impl_->passes.size())
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "passOutputSize: index out of range"));
        const Chain::Impl::PassRuntime &pass = impl_->passes[passIndex];
        return std::make_pair(pass.outputWidth, pass.outputHeight);
}

auto Chain::render(const InputFrame &input, const OutputTarget &target, const FrameInfo &frame)
    -> std::expected<void, Error>
{
        Impl &impl = *impl_;
        if (input.view == VK_NULL_HANDLE || input.width == 0 || input.height == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "render: invalid input frame"));
        if (input.rotation != Rotation::None)
                return std::unexpected(SLV_ERROR(Errc::unsupported_feature, "rotation lands in M2"));

        if (std::holds_alternative<SwapchainFrame>(target))
                return std::unexpected(SLV_ERROR(Errc::unsupported_feature, "swapchain output lands in M4"));
        const Offscreen &offscreen = std::get<Offscreen>(target);
        if (offscreen.view == VK_NULL_HANDLE || offscreen.width == 0 || offscreen.height == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "render: invalid offscreen target"));
        if (offscreen.width != impl.targetWidth || offscreen.height != impl.targetHeight
            || offscreen.format != impl.targetFormat)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument,
                    "render: offscreen target must match ChainOptions size/format (resize() to change)"));

        impl.gameWidth = input.width;
        impl.gameHeight = input.height;

        // 1. Resolve pass sizes; the final pass must land on the output target.
        ResolvePassSizes(impl, input.width, input.height, impl.targetWidth, impl.targetHeight);
        Chain::Impl::PassRuntime &last = impl.passes.back();
        if (last.outputWidth != impl.targetWidth || last.outputHeight != impl.targetHeight)
                return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                    "final pass resolves to " + std::to_string(last.outputWidth) + "x"
                        + std::to_string(last.outputHeight) + " but the output target is "
                        + std::to_string(impl.targetWidth) + "x" + std::to_string(impl.targetHeight)
                        + "; final-pass blit/letterbox lands in M2"));

        // 2. Ensure intermediate FBOs exist (intermediate = all but last).
        for (size_t i = 0; i + 1 < impl.passes.size(); ++i) {
                Chain::Impl::PassRuntime &pass = impl.passes[i];
                if (pass.target.image == VK_NULL_HANDLE || pass.outputWidth != pass.allocatedWidth
                    || pass.outputHeight != pass.allocatedHeight) {
                        // Recreate when size drifted (resize() path).
                        if (pass.target.image != VK_NULL_HANDLE)
                                return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                                    "mid-chain FBO recreation on resize lands in M2 (recreate the Chain)"));
                        auto made = impl.MakeTarget(pass, pass.outputWidth, pass.outputHeight);
                        if (!made)
                                return std::unexpected(made.error());
                }
        }

        // 3. Output framebuffer over the host view (cached per view).
        VkFramebuffer outputFramebuffer = VK_NULL_HANDLE;
        {
                const auto it = impl.outputFramebuffers.find(offscreen.view);
                if (it != impl.outputFramebuffers.end()) {
                        outputFramebuffer = it->second;
                } else {
                        const VkRenderPass renderPass = impl.GetRenderPass(impl.targetFormat);
                        VkFramebufferCreateInfo fbInfo{};
                        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                        fbInfo.renderPass = renderPass;
                        fbInfo.attachmentCount = 1;
                        fbInfo.pAttachments = &offscreen.view;
                        fbInfo.width = impl.targetWidth;
                        fbInfo.height = impl.targetHeight;
                        fbInfo.layers = 1;
                        SLV_VK(vkCreateFramebuffer(impl.device->device(), &fbInfo, nullptr, &outputFramebuffer),
                            Errc::vk_error);
                        impl.outputFramebuffers.emplace(offscreen.view, outputFramebuffer);
                }
        }

        // 4. Per-pass UBO fills.
        std::vector<std::pair<uint32_t, uint32_t>> passSizes;
        for (const Chain::Impl::PassRuntime &pass : impl.passes)
                passSizes.emplace_back(pass.outputWidth, pass.outputHeight);

        // Effective parameter values: .slangp overrides, then setParameter()
        // runtime overrides on top (matching is case-insensitive in the fill,
        // consistent with the translator's member whitelist).
        std::vector<ParamOverride> effectiveOverrides = impl.presetParameterOverrides;
        for (const auto &[name, value] : impl.paramOverrides) {
                bool found = false;
                for (ParamOverride &override : effectiveOverrides)
                        if (vk::CaseInsensitiveEqual(override.name, name)) {
                                override.value = value;
                                found = true;
                                break;
                        }
                if (!found)
                        effectiveOverrides.push_back({name, value});
        }

        uint32_t inputW = input.width;
        uint32_t inputH = input.height;
        for (size_t i = 0; i < impl.passes.size(); ++i) {
                Chain::Impl::PassRuntime &pass = impl.passes[i];

                vk::BlockFillContext ctx;
                ctx.passIndex = static_cast<uint32_t>(i);
                ctx.passCount = static_cast<uint32_t>(impl.passes.size());
                ctx.inputWidth = inputW;
                ctx.inputHeight = inputH;
                ctx.outputWidth = pass.outputWidth;
                ctx.outputHeight = pass.outputHeight;
                ctx.gameWidth = input.width;
                ctx.gameHeight = input.height;
                ctx.targetWidth = impl.targetWidth;
                ctx.targetHeight = impl.targetHeight;
                ctx.frameCount = frame.frameCount;
                ctx.frameDeltaUs = frame.frameDeltaUs;
                ctx.frameDirection = frame.frameDirection;
                ctx.passOutputSizes = &passSizes;

                if (!pass.translation.vertex.uniformBlocks.empty()) {
                        ctx.vertexStage = true;
                        const std::vector<uint8_t> bytes = vk::FillBlockBytes(
                            pass.translation.vertex.uniformBlocks[0], pass.def, pass.translation, effectiveOverrides, ctx);
                        if (bytes.size() > static_cast<size_t>(pass.vertexUbo.size))
                                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "vertex UBO overflow"));
                        if (pass.vertexUbo.mapped != nullptr)
                                std::memcpy(pass.vertexUbo.mapped, bytes.data(), bytes.size());
                }
                if (!pass.translation.fragment.uniformBlocks.empty()) {
                        ctx.vertexStage = false;
                        const std::vector<uint8_t> bytes = vk::FillBlockBytes(
                            pass.translation.fragment.uniformBlocks[0], pass.def, pass.translation, effectiveOverrides, ctx);
                        if (bytes.size() > static_cast<size_t>(pass.fragmentUbo.size))
                                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "fragment UBO overflow"));
                        if (pass.fragmentUbo.mapped != nullptr)
                                std::memcpy(pass.fragmentUbo.mapped, bytes.data(), bytes.size());
                }
                inputW = pass.outputWidth;
                inputH = pass.outputHeight;
        }

        // 5. Bind textures into set 2 for every pass.
        for (size_t i = 0; i < impl.passes.size(); ++i) {
                Chain::Impl::PassRuntime &pass = impl.passes[i];
                const size_t textureCount = pass.translation.fragment.textures.size();
                if (textureCount == 0)
                        continue;
                std::vector<VkDescriptorImageInfo> imageInfos(textureCount);
                std::vector<VkWriteDescriptorSet> writes(textureCount);
                for (size_t t = 0; t < textureCount; ++t) {
                        const TextureRef &ref = pass.translation.fragment.textures[t];
                        VkImageView view = VK_NULL_HANDLE;
                        VkSampler sampler = VK_NULL_HANDLE;
                        auto resolved = ResolveTexture(ref.name, i, impl, input, pass, view, sampler);
                        if (!resolved)
                                return std::unexpected(resolved.error());
                        imageInfos[t] = {sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                        writes[t] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, pass.set2,
                            static_cast<uint32_t>(t), 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            &imageInfos[t], nullptr, nullptr};
                }
                vkUpdateDescriptorSets(impl.device->device(), static_cast<uint32_t>(writes.size()), writes.data(), 0,
                    nullptr);
        }

        // 6. Record + submit (synchronous M1).
        auto recorded = impl.device->impl().SubmitAndWait([&](VkCommandBuffer cmd) {
                uint32_t inputW2 = input.width;
                uint32_t inputH2 = input.height;
                for (size_t i = 0; i < impl.passes.size(); ++i) {
                        Chain::Impl::PassRuntime &pass = impl.passes[i];
                        const bool isLast = i + 1 == impl.passes.size();
                        const uint32_t outW = isLast ? impl.targetWidth : pass.outputWidth;
                        const uint32_t outH = isLast ? impl.targetHeight : pass.outputHeight;

                        VkRenderPassBeginInfo beginInfo{};
                        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                        beginInfo.renderPass = impl.GetRenderPass(pass.targetFormat);
                        beginInfo.framebuffer = isLast ? outputFramebuffer : pass.framebuffer;
                        beginInfo.renderArea = {{0, 0}, {outW, outH}};
                        std::array<float, 4> black = {0.F, 0.F, 0.F, 1.F};
                        beginInfo.clearValueCount = 1;
                        beginInfo.pClearValues = reinterpret_cast<const VkClearValue *>(&black);
                        vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

                        VkViewport viewport{0.F, 0.F, static_cast<float>(outW), static_cast<float>(outH), 0.F, 1.F};
                        VkRect2D scissor{{0, 0}, {outW, outH}};
                        vkCmdSetViewport(cmd, 0, 1, &viewport);
                        vkCmdSetScissor(cmd, 0, 1, &scissor);

                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline);
                        std::array<VkDescriptorSet, 4> sets = {impl.emptySet, pass.set1, pass.set2, pass.set3};
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipelineLayout, 0,
                            static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
                        const VkDeviceSize offset = 0;
                        vkCmdBindVertexBuffers(cmd, 0, 1, &impl.vertexBuffer, &offset);
                        vkCmdDraw(cmd, 4, 1, 0, 0);
                        vkCmdEndRenderPass(cmd);

                        inputW2 = outW;
                        inputH2 = outH;
                }
                (void)inputW2;
                (void)inputH2;
        });
        if (!recorded)
                return std::unexpected(recorded.error());
        return {};
}

} // namespace slv
