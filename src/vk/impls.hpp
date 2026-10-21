// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

#pragma once

// libslang-vk internal — shared pimpl definitions.
//
// The runner's Impl structs live here (not in the .cpp that owns the public
// methods) because runner translation units cross-reference each other:
// Image/Chain need Device::Impl's allocator + submit machinery, Chain reads
// Preset::Impl's compiled artifacts. Included only by src/vk/*.cpp.

#include <functional>

#include "vk_internal.hpp"

namespace slv {

// Core-layer vocabulary + internal Vulkan helpers, unqualified in these TUs.
using slv::preset::ParamInfo;
using slv::preset::PassDef;
using slv::preset::SlangTranslation;
using vk::ValidationLog;

struct Device::Impl {
        bool adopted = false;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        uint32_t graphicsFamily = 0;
        VkPhysicalDeviceProperties properties{};
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        ValidationLog validation;
        VmaVulkanFunctions vulkanFunctions{}; // volk-bound fetchers (see adopt/create).
        // Device extensions requested at create() time. Kept so recover()
        // can build a replacement device with the same extension set.
        std::vector<std::string> deviceExtensions;
        // Bumped by every successful recover(). Presets and chains stamp
        // their GPU resources with the generation they were built in, so a
        // stale object can be recognised and rebuilt (ROB-35).
        uint64_t generation = 0;

        // --- Release registry (ROB-35) -------------------------------------
        //
        // Everything created on the device (chains, presets, images) has to
        // release its GPU resources BEFORE the device is replaced: a
        // destroyed device does not reliably reclaim the memory its objects
        // used, and the objects cannot be destroyed through the replacement
        // device either. Owners register a callback here; recover() runs
        // them all, then swaps the device.
        std::vector<std::pair<uint64_t, std::function<void()>>> releaseCallbacks;
        uint64_t nextReleaseToken = 1;

        auto AddReleaseCallback(std::function<void()> callback) -> uint64_t
        {
                const uint64_t token = nextReleaseToken++;
                releaseCallbacks.emplace_back(token, std::move(callback));
                return token;
        }

        void RemoveReleaseCallback(uint64_t token)
        {
                for (auto it = releaseCallbacks.begin(); it != releaseCallbacks.end(); ++it) {
                        if (it->first == token) {
                                releaseCallbacks.erase(it);
                                return;
                        }
                }
        }

        // Releases every registered object's GPU resources. The callbacks
        // are moved out first so an owner that unregisters itself while
        // being released does not invalidate the iteration.
        void ReleaseRegisteredResources()
        {
                auto callbacks = std::move(releaseCallbacks);
                releaseCallbacks.clear();
                for (auto &entry : callbacks)
                        entry.second();
        }

        ~Impl()
        {
                // Destroy in reverse dependency order: pool's command buffers
                // must die before the device (VUID-vkDestroyDevice-device-05137).
                if (messenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT != nullptr)
                        vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
                if (commandPool != VK_NULL_HANDLE)
                        vkDestroyCommandPool(device, commandPool, nullptr);
                if (allocator != VK_NULL_HANDLE)
                        vmaDestroyAllocator(allocator);
                if (device != VK_NULL_HANDLE && !adopted)
                        vkDestroyDevice(device, nullptr);
                if (instance != VK_NULL_HANDLE && !adopted)
                        vkDestroyInstance(instance, nullptr);
        }

        // One-shot command recording: allocates from the pool, records, submits
        // with a fence and waits. Deliberately synchronous (plan §6). The
        // optional wait semaphore is the M4 swapchain contract: the host's
        // acquire semaphore is waited at `waitDstStageMask` before the recorded
        // work runs; completion is fence-gated, so the host may present right
        // after this returns.
        auto SubmitAndWait(const std::function<void(VkCommandBuffer)> &record,
            VkSemaphore waitSemaphore = VK_NULL_HANDLE,
            VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
            -> std::expected<void, Error>
        {
                VkCommandBufferAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocInfo.commandPool = commandPool;
                allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocInfo.commandBufferCount = 1;
                VkCommandBuffer cmd = VK_NULL_HANDLE;
                SLV_VK(vkAllocateCommandBuffers(device, &allocInfo, &cmd), Errc::vk_error);

                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                SLV_VK(vkBeginCommandBuffer(cmd, &beginInfo), Errc::vk_error);
                record(cmd);
                SLV_VK(vkEndCommandBuffer(cmd), Errc::vk_error);

                VkFenceCreateInfo fenceInfo{};
                fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                VkFence fence = VK_NULL_HANDLE;
                SLV_VK(vkCreateFence(device, &fenceInfo, nullptr, &fence), Errc::vk_error);

                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &cmd;
                if (waitSemaphore != VK_NULL_HANDLE) {
                        submitInfo.waitSemaphoreCount = 1;
                        submitInfo.pWaitSemaphores = &waitSemaphore;
                        submitInfo.pWaitDstStageMask = &waitDstStageMask;
                }
                const VkResult submitted = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence);
                if (submitted != VK_SUCCESS) {
                        vkDestroyFence(device, fence, nullptr);
                        return std::unexpected(SLV_VKERR(submitted,
                            ::slv::vk::VkErrorCode(submitted, Errc::vk_error), "vkQueueSubmit"));
                }
                const VkResult waited = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(device, fence, nullptr);
                SLV_VK(waited, Errc::vk_error);
                // Return the command buffer to the pool. A pool-wide
                // vkResetCommandPool would also free command buffers that are
                // still in flight on an async (ROB-33) submission, so the
                // single buffer is freed instead.
                vkFreeCommandBuffers(device, commandPool, 1, &cmd);
                return {};
        }

        // --- Async submission primitives (ROB-33) ---------------------------
        //
        // The synchronous SubmitAndWait above collapses record -> submit ->
        // wait into one call. The async path splits it so the host can hold a
        // frame in flight while it does its own CPU work:
        //
        //   RecordCommandBuffer()  -> a recorded (not yet submitted) command
        //   SubmitRecorded()        -> queue submit, no wait
        //   the host waits on the fence it was handed
        //
        // Command buffers come from the same pool as the sync path, but they
        // are freed individually (never by a pool reset), so an in-flight
        // async frame is never pulled out from under the GPU.

        // Allocates a command buffer from the pool. The caller owns it until
        // it frees it.
        auto AllocateCommandBuffer() -> std::expected<VkCommandBuffer, Error>
        {
                VkCommandBufferAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocInfo.commandPool = commandPool;
                allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocInfo.commandBufferCount = 1;
                VkCommandBuffer cmd = VK_NULL_HANDLE;
                SLV_VK(vkAllocateCommandBuffers(device, &allocInfo, &cmd), Errc::vk_error);
                return cmd;
        }

        // Returns a command buffer to the pool.
        auto FreeCommandBuffer(VkCommandBuffer cmd) -> void
        {
                if (cmd != VK_NULL_HANDLE)
                        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
        }

        // Records into a command buffer the caller already owns. The buffer
        // must be in the initial state — freshly allocated, or reset by
        // ResetCommandBuffer() once its previous submission has retired.
        //
        // Reusing the buffer (rather than freeing and reallocating it every
        // frame) keeps the per-frame driver work down to a reset: the pool
        // was created with VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT.
        auto RecordInto(VkCommandBuffer cmd, const std::function<void(VkCommandBuffer)> &record)
            -> std::expected<void, Error>
        {
                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "vkBeginCommandBuffer failed"));
                record(cmd);
                if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "vkEndCommandBuffer failed"));
                return {};
        }

        // Puts a command buffer back into the initial state so it can be
        // recorded into again.
        auto ResetCommandBuffer(VkCommandBuffer cmd) -> std::expected<void, Error>
        {
                SLV_VK(vkResetCommandBuffer(cmd, 0), Errc::vk_error);
                return {};
        }

        // Allocates a command buffer from the pool and records into it.
        // The caller owns the returned buffer until it frees it (or submits
        // it and lets the chain reclaim it after the fence signals).
        auto RecordCommandBuffer(const std::function<void(VkCommandBuffer)> &record)
            -> std::expected<VkCommandBuffer, Error>
        {
                auto cmd = AllocateCommandBuffer();
                if (!cmd)
                        return std::unexpected(cmd.error());
                auto recorded = RecordInto(*cmd, record);
                if (!recorded) {
                        FreeCommandBuffer(*cmd);
                        return std::unexpected(recorded.error());
                }
                return cmd;
        }

        // Submits a recorded command buffer. `fence` is signalled when the
        // work completes; this call does NOT wait. The optional wait
        // semaphore is the M4 swapchain contract (the host's acquire
        // semaphore is waited at `waitDstStageMask` before the work runs).
        auto SubmitRecorded(VkCommandBuffer cmd, VkSemaphore waitSemaphore,
            VkPipelineStageFlags waitDstStageMask, VkFence fence) -> std::expected<void, Error>
        {
                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &cmd;
                if (waitSemaphore != VK_NULL_HANDLE) {
                        submitInfo.waitSemaphoreCount = 1;
                        submitInfo.pWaitSemaphores = &waitSemaphore;
                        submitInfo.pWaitDstStageMask = &waitDstStageMask;
                }
                const VkResult submitted = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence);
                if (submitted != VK_SUCCESS)
                        return std::unexpected(SLV_VKERR(submitted,
                            ::slv::vk::VkErrorCode(submitted, Errc::vk_error), "vkQueueSubmit"));
                return {};
        }

        // Device lost recovery (ROB-35): rebuilds the VkDevice, its command
        // pool and the VMA allocator from the existing instance + physical
        // device. The caller must have released the objects it created on the
        // lost device first (chains and presets do that before calling in).
        auto RecreateDevice() -> std::expected<void, Error>
        {
                // Let everything created on this device release its GPU
                // resources first. The objects still belong to the device
                // that is about to go away, so this is the last chance to
                // destroy them properly (ROB-35).
                ReleaseRegisteredResources();

                // Drop the old device's resources. On a lost device these
                // calls report VK_ERROR_DEVICE_LOST, which is expected and
                // harmless: the objects are already gone.
                if (commandPool != VK_NULL_HANDLE) {
                        vkDestroyCommandPool(device, commandPool, nullptr);
                        commandPool = VK_NULL_HANDLE;
                }
                if (allocator != VK_NULL_HANDLE) {
                        // Safe now: ReleaseRegisteredResources() above gave
                        // every object a chance to free its allocations
                        // first, so the allocator holds no live allocation
                        // and VMA's "allocations were not freed" assertion
                        // does not fire.
                        vmaDestroyAllocator(allocator);
                        allocator = VK_NULL_HANDLE;
                }
                if (device != VK_NULL_HANDLE) {
                        vkDestroyDevice(device, nullptr);
                        device = VK_NULL_HANDLE;
                }
                graphicsQueue = VK_NULL_HANDLE;

                // Build the replacement device with the same queue setup and
                // extension set the original was created with.
                const float priority = 1.0F;
                VkDeviceQueueCreateInfo queueInfo{};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = graphicsFamily;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &priority;

                std::vector<const char *> extensions;
                extensions.reserve(deviceExtensions.size());
                for (const std::string &name : deviceExtensions)
                        extensions.push_back(name.c_str());

                VkDeviceCreateInfo deviceInfo{};
                deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                deviceInfo.queueCreateInfoCount = 1;
                deviceInfo.pQueueCreateInfos = &queueInfo;
                deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
                deviceInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

                VkDevice newDevice = VK_NULL_HANDLE;
                SLV_VK(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &newDevice), Errc::vk_init_failed);
                device = newDevice;
                volkLoadDevice(device);

                vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
                vkGetPhysicalDeviceProperties(physicalDevice, &properties);

                VmaAllocatorCreateInfo allocInfo{};
                allocInfo.physicalDevice = physicalDevice;
                allocInfo.device = device;
                allocInfo.instance = instance;
                vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
                vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
                allocInfo.pVulkanFunctions = &vulkanFunctions;
                if (vmaCreateAllocator(&allocInfo, &allocator) != VK_SUCCESS) {
                        allocator = VK_NULL_HANDLE;
                        return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "vmaCreateAllocator failed"));
                }

                VkCommandPoolCreateInfo poolInfo{};
                poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                poolInfo.queueFamilyIndex = graphicsFamily;
                SLV_VK(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), Errc::vk_init_failed);

                ++generation;
                return {};
        }

        // Device lost recovery (ROB-35): the real recovery entry point.
        // The chain's render path calls this after it has released its own
        // (and its preset's) GPU resources, which the lost device took with
        // it; this then rebuilds the device itself.
        auto RecoverFromDeviceLost() -> std::expected<void, Error>
        {
                if (adopted)
                        return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                            "device lost: the device is host-owned (Device::adopt); "
                            "the host must recreate the VkDevice itself"));
                return RecreateDevice();
        }
};

struct Preset::Impl {
        // Uploaded LUT texture (M3): RGBA8, R8G8B8A8_UNORM, full mip chain
        // when the .slangp sets `name_mipmap = true`. Owned here so chains
        // from the same preset share one upload; views are borrowed by
        // Chain::Impl::luts (Preset must outlive its Chains).
        struct LutGpu {
                std::string name; // Declared spelling (case-sensitive GLSL name).
                // Absolute path the texture was decoded from. Kept so the
                // upload can be repeated after a device-lost recovery
                // (ROB-35) without re-parsing the preset.
                std::string sourcePath;
                uint32_t width = 0;
                uint32_t height = 0;
                uint32_t mipLevels = 1;
                vk::VkImageWithView img;
        };

        Device *device = nullptr; // Borrowed; the host must destroy the Preset before the Device.
        std::vector<PassDef> passDefs;
        std::vector<SlangTranslation> translations;
        std::vector<preset::PassSpirv> spirv;
        std::vector<ParameterInfo> parameters;
        std::vector<LutGpu> luts;
        // True when the preset was loaded with noLutImages: the textures were
        // never uploaded, so a rebuild must not upload them either.
        bool lutsSkipped = false;
        // The device generation these textures were uploaded on. A mismatch
        // means the device was replaced and the textures have to be uploaded
        // again (ROB-35).
        uint64_t generation = 0;
        // Token of this preset's entry in the device's release registry.
        uint64_t releaseToken = 0;
        slv::preset::Preset parsed; // Core data model (textures, feedback, histories).

        ~Impl()
        {
                if (device != nullptr)
                        device->impl().RemoveReleaseCallback(releaseToken);
                // When the device was replaced (a device-lost recovery) the
                // textures died with the old device and must not be
                // destroyed through the new one.
                const bool deviceReplaced = (device != nullptr && device->generation() != generation);
                ReleaseLuts(deviceReplaced);
        }

        // Registers (or re-registers) the preset with the device's release
        // registry, so a device replacement can release the textures while
        // they still belong to a live device (ROB-35).
        void RegisterReleaseCallback()
        {
                if (device == nullptr)
                        return;
                releaseToken = device->impl().AddReleaseCallback([this]() {
                        ReleaseLuts();
                        if (device != nullptr)
                                RegisterReleaseCallback();
                });
        }

        // Releases the uploaded LUT images. Split out of the destructor so a
        // device-lost recovery (ROB-35) can drop the dead textures before the
        // device that owns them is replaced.
        // Releases the uploaded LUT images. Split out of the destructor so a
        // device-lost recovery (ROB-35) can drop the dead textures before the
        // device that owns them is replaced.
        //
        // `deviceLost` skips the destroy calls: the device that owned the
        // textures is gone, and destroying its objects through a different
        // device would be invalid. The handles are simply forgotten.
        void ReleaseLuts(bool deviceLost = false)
        {
                if (device == nullptr)
                        return;
                if (!deviceLost) {
                        VkDevice dev = device->device();
                        for (LutGpu &lut : luts) {
                                if (lut.img.view != VK_NULL_HANDLE)
                                        vkDestroyImageView(dev, lut.img.view, nullptr);
                                if (lut.img.image != VK_NULL_HANDLE)
                                        vmaDestroyImage(
                                            device->impl().allocator, lut.img.image, lut.img.allocation);
                        }
                }
                for (LutGpu &lut : luts)
                        lut.img = {};
                luts.clear();
        }
};

// LUT upload (M3; defined in lut.cpp). LoadLutPixels decodes an image file
// to tightly packed RGBA8 (stb_image; git-lfs pointer files and unsupported
// formats fail with a named error). UploadLutTexture uploads it as
// R8G8B8A8_UNORM with the TextureDef's own options (full mip chain when
// `name_mipmap = true`) and fills `out` (name/width/height/mipLevels/img).
namespace vk {
auto LoadLutPixels(const std::string &absolutePath, std::vector<uint8_t> &outRgba, uint32_t &outWidth,
    uint32_t &outHeight, std::string &error) -> bool;
auto UploadLutTexture(Device &device, const slv::preset::TextureDef &def, const std::string &resolvedPath,
    Preset::Impl::LutGpu &out) -> std::expected<void, Error>;
} // namespace vk

} // namespace slv
