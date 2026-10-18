// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk M4 unit test — presentation (plan §11 M4 / §8 output (a)):
// SwapchainFrame rendering into an acquired swapchain image (headless
// surface), the fit-and-center / explicit-rect letterbox draw, input-frame
// rotation (present draw + Rotation uniform), Image::update host uploads,
// and the output-target validation contract.
//
// Goldens are byte-exact on lavapipe: every rotation/letterbox case uses a
// 1:1 content rect so the linear present sampler hits texel centers exactly.
// The swapchain cycle runs on VK_EXT_headless_surface (present on lavapipe
// is a no-op but the full acquire/render/present/readback path is real) and
// skips cleanly when the surface or swapchain extension is unavailable.
//
// Skips cleanly (exit 0) when no Vulkan device is present — CI images
// provide lavapipe (mesa-vulkan-drivers); Windows CI has no ICD.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <volk.h>
#include <slangvk/slangvk.hpp>

#ifndef SLANGVK_ENABLE_VK
int main() { return 0; } // Core-only build: runner not compiled.
#else

namespace {

int failures = 0;

void Check(bool condition, const std::string &label)
{
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition)
                ++failures;
}

void CheckBytes(const std::vector<uint8_t> &got, const std::vector<uint8_t> &want, const std::string &label)
{
        if (got.size() != want.size()) {
                std::printf("FAIL %s (size %zu != %zu)\n", label.c_str(), got.size(), want.size());
                ++failures;
                return;
        }
        for (size_t i = 0; i < got.size(); ++i)
                if (got[i] != want[i]) {
                        std::printf("FAIL %s (first diff at byte %zu: got %u want %u)\n", label.c_str(), i, got[i],
                            want[i]);
                        ++failures;
                        return;
                }
        std::printf("PASS %s\n", label.c_str());
}

void CheckBytesTolerant(const std::vector<uint8_t> &got, const std::vector<uint8_t> &want, int tol,
    const std::string &label)
{
        if (got.size() != want.size()) {
                std::printf("FAIL %s (size %zu != %zu)\n", label.c_str(), got.size(), want.size());
                ++failures;
                return;
        }
        for (size_t i = 0; i < got.size(); ++i)
                if (std::abs(static_cast<int>(got[i]) - static_cast<int>(want[i])) > tol) {
                        std::printf("FAIL %s (byte %zu: got %u want %u, tolerance %d)\n", label.c_str(), i, got[i],
                            want[i], tol);
                        ++failures;
                        return;
                }
        std::printf("PASS %s\n", label.c_str());
}

std::vector<uint8_t> MakePattern(uint32_t width, uint32_t height)
{
        std::vector<uint8_t> pixels(width * height * 4);
        for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x) {
                        const size_t o = (static_cast<size_t>(y) * width + x) * 4;
                        pixels[o + 0] = static_cast<uint8_t>((x * 4) & 0xFF);
                        pixels[o + 1] = static_cast<uint8_t>((y * 4) & 0xFF);
                        pixels[o + 2] = static_cast<uint8_t>((x + y) & 0xFF);
                        pixels[o + 3] = 0xFF;
                }
        return pixels;
}

// Black margins (render pass clear: opaque black) + placed content.
std::vector<uint8_t> BlackFrame(uint32_t width, uint32_t height)
{
        std::vector<uint8_t> frame(static_cast<size_t>(width) * height * 4, 0);
        for (uint32_t i = 0; i < width * height; ++i)
                frame[i * 4 + 3] = 0xFF;
        return frame;
}

// Places `content` (contentW x contentH) at (offsetX, offsetY) on black.
std::vector<uint8_t> PlacedFrame(const std::vector<uint8_t> &content, uint32_t contentW, uint32_t contentH,
    uint32_t targetW, uint32_t targetH, int32_t offsetX, int32_t offsetY)
{
        std::vector<uint8_t> frame = BlackFrame(targetW, targetH);
        for (uint32_t y = 0; y < contentH; ++y) {
                const int32_t dy = offsetY + static_cast<int32_t>(y);
                if (dy < 0 || dy >= static_cast<int32_t>(targetH))
                        continue;
                for (uint32_t x = 0; x < contentW; ++x) {
                        const int32_t dx = offsetX + static_cast<int32_t>(x);
                        if (dx < 0 || dx >= static_cast<int32_t>(targetW))
                                continue;
                        std::memcpy(&frame[(static_cast<size_t>(dy) * targetW + dx) * 4],
                            &content[(static_cast<size_t>(y) * contentW + x) * 4], 4);
                }
        }
        return frame;
}

struct Fixture
{
        slv::Device *device = nullptr;
        std::optional<slv::Image> input;
        std::optional<slv::Image> output;

        Fixture(slv::Device &dev, const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height,
            uint32_t outputWidth, uint32_t outputHeight) : device(&dev)
        {
                slv::Image::CreateInfo in;
                in.desc = {width, height, VK_FORMAT_R8G8B8A8_UNORM};
                in.pixels = pixels.data();
                in.pixelBytes = pixels.size();
                if (auto made = slv::Image::create(dev, in))
                        input = std::move(*made);
                slv::Image::CreateInfo out;
                out.desc = {outputWidth, outputHeight, VK_FORMAT_R8G8B8A8_UNORM};
                if (auto madeOut = slv::Image::create(dev, out))
                        output = std::move(*madeOut);
        }

        slv::InputFrame Frame(slv::Rotation rotation = slv::Rotation::None) const
        {
                slv::InputFrame f;
                f.view = input->view();
                f.image = input->image();
                f.format = VK_FORMAT_R8G8B8A8_UNORM;
                f.width = input->desc().width;
                f.height = input->desc().height;
                f.rotation = rotation;
                return f;
        }

        slv::Offscreen Target() const
        {
                slv::Offscreen t;
                t.view = output->view();
                t.image = output->image();
                t.format = VK_FORMAT_R8G8B8A8_UNORM;
                t.width = output->desc().width;
                t.height = output->desc().height;
                return t;
        }

        auto Readback() -> std::expected<std::vector<uint8_t>, slv::Error>
        {
                return device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM, output->desc().width,
                    output->desc().height, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
};

} // namespace

int main()
{
        const std::filesystem::path dataDir = SLANGVK_TESTS_DATA_DIR;

        auto device = slv::Device::create({.appName = "libslang-vk-m4-test", .validationLayers = true});
        if (!device) {
                std::printf("SKIP runner_m4 (no Vulkan device: %s)\n", device.error().message.c_str());
                return 0;
        }

        // ------------------------------------------------------------------
        // 1. Fit-and-center letterbox (offscreen present path): final pass
        // 96x64 into a 96x128 target -> scale 1 rect (0,32,96,64).
        {
                const std::vector<uint8_t> pattern = MakePattern(96, 64);
                Fixture fx(*device, pattern, 96, 64, 96, 128);
                auto chain = slv::Chain::create(*device, *slv::Preset::load(*device, dataDir / "runner4/blit.slangp"),
                    {.outputWidth = 96, .outputHeight = 128});
                Check(chain.has_value(), "letterbox: chain creates");
                if (chain) {
                        auto rendered = chain->render(fx.Frame(), fx.Target(), {});
                        Check(rendered.has_value(), "letterbox: render ok");
                        auto got = fx.Readback();
                        Check(got.has_value(), "letterbox: readback");
                        if (got) {
                                const std::vector<uint8_t> want =
                                    PlacedFrame(pattern, 96, 64, 96, 128, 0, 32);
                                CheckBytes(*got, want, "letterbox: fit+center golden (scale-1 rect, black bands)");
                        }
                }
        }

        // ------------------------------------------------------------------
        // 2. Rotation goldens (offscreen present path): 32x16 content into a
        // 32x32 target; every quarter turn yields a scale-1 rect.
        {
                const std::vector<uint8_t> pattern = MakePattern(32, 16);
                const auto pixel = [&](const std::vector<uint8_t> &img, uint32_t x, uint32_t y) {
                        return std::array<uint8_t, 4>{img[(y * 32u + x) * 4 + 0], img[(y * 32u + x) * 4 + 1],
                            img[(y * 32u + x) * 4 + 2], img[(y * 32u + x) * 4 + 3]};
                };

                struct RotationCase {
                        slv::Rotation rotation;
                        const char *label;
                        std::vector<uint8_t> expected;
                };
                std::vector<RotationCase> cases;
                { // Cw90: content 16x32 at rect (8,0,16,32); (x,y) <- src(23-x, y).
                        std::vector<uint8_t> want = BlackFrame(32, 32);
                        for (uint32_t y = 0; y < 32; ++y)
                                for (uint32_t x = 8; x < 24; ++x) {
                                        const auto p = pixel(pattern, y, 23 - x);
                                        std::memcpy(&want[(y * 32u + x) * 4], p.data(), 4);
                                }
                        cases.push_back({slv::Rotation::Cw90, "Cw90", std::move(want)});
                }
                { // Cw180: content 32x16 at rect (0,8,32,16); (x,y) <- src(31-x, 23-y).
                        std::vector<uint8_t> want = BlackFrame(32, 32);
                        for (uint32_t y = 8; y < 24; ++y)
                                for (uint32_t x = 0; x < 32; ++x) {
                                        const auto p = pixel(pattern, 31 - x, 23 - y);
                                        std::memcpy(&want[(y * 32u + x) * 4], p.data(), 4);
                                }
                        cases.push_back({slv::Rotation::Cw180, "Cw180", std::move(want)});
                }
                { // Cw270: content 16x32 at rect (8,0,16,32); (x,y) <- src(31-y, x-8).
                        std::vector<uint8_t> want = BlackFrame(32, 32);
                        for (uint32_t y = 0; y < 32; ++y)
                                for (uint32_t x = 8; x < 24; ++x) {
                                        const auto p = pixel(pattern, 31 - y, x - 8);
                                        std::memcpy(&want[(y * 32u + x) * 4], p.data(), 4);
                                }
                        cases.push_back({slv::Rotation::Cw270, "Cw270", std::move(want)});
                }

                auto preset = slv::Preset::load(*device, dataDir / "runner4/blit.slangp");
                auto chain = slv::Chain::create(*device, *preset, {.outputWidth = 32, .outputHeight = 32});
                Check(chain.has_value(), "rotation: chain creates");
                if (chain) {
                        for (const RotationCase &c : cases) {
                                Fixture fx(*device, pattern, 32, 16, 32, 32);
                                auto rendered = chain->render(fx.Frame(c.rotation), fx.Target(), {});
                                Check(rendered.has_value(), std::string("rotation ") + c.label + ": render ok");
                                auto got = fx.Readback();
                                Check(got.has_value(), std::string("rotation ") + c.label + ": readback");
                                if (got)
                                        CheckBytes(*got, c.expected, std::string("rotation ") + c.label + " golden");
                        }
                }
        }

        // ------------------------------------------------------------------
        // 3. Rotation uniform: the fragment writes float(Rotation)/3.0; the
        // readback pixel encodes the input frame's rotation (0..3).
        {
                const std::vector<uint8_t> pattern = MakePattern(8, 8);
                auto preset = slv::Preset::load(*device, dataDir / "runner4/rot.slangp");
                auto chain = slv::Chain::create(*device, *preset, {.outputWidth = 8, .outputHeight = 8});
                Check(chain.has_value(), "rotation uniform: chain creates");
                if (chain) {
                        const struct {
                                slv::Rotation rotation;
                                uint8_t expected;
                        } cases[] = {
                            {slv::Rotation::None, 0},
                            {slv::Rotation::Cw90, 85},  // 1/3 * 255
                            {slv::Rotation::Cw180, 170}, // 2/3 * 255
                        };
                        for (const auto &c : cases) {
                                Fixture fx(*device, pattern, 8, 8, 8, 8);
                                auto rendered = chain->render(fx.Frame(c.rotation), fx.Target(), {});
                                Check(rendered.has_value(), "rotation uniform: render ok");
                                auto got = fx.Readback();
                                Check(got.has_value(), "rotation uniform: readback");
                                if (got) {
                                        const size_t o = (4u * 8u + 4u) * 4u;
                                        CheckBytesTolerant({(*got)[o], (*got)[o + 1], (*got)[o + 2]},
                                            {c.expected, c.expected, c.expected}, 1,
                                            "rotation uniform: pixel encodes rotation "
                                                + std::to_string(static_cast<uint32_t>(c.rotation)));
                                }
                        }
                }
        }

        // ------------------------------------------------------------------
        // 4. Image::update: per-frame host upload replaces the content.
        {
                const std::vector<uint8_t> patternA = MakePattern(96, 64);
                std::vector<uint8_t> patternB(96 * 64 * 4);
                for (size_t i = 0; i < 96 * 64; ++i) {
                        patternB[i * 4 + 0] = static_cast<uint8_t>(255 - (i & 0xFF));
                        patternB[i * 4 + 1] = static_cast<uint8_t>((i * 7) & 0xFF);
                        patternB[i * 4 + 2] = 0x40;
                        patternB[i * 4 + 3] = 0xFF;
                }
                Fixture fx(*device, patternA, 96, 64, 96, 64);
                auto chain = slv::Chain::create(*device, *slv::Preset::load(*device, dataDir / "runner4/blit.slangp"),
                    {.outputWidth = 96, .outputHeight = 64});
                Check(chain.has_value(), "Image::update: chain creates");
                if (chain) {
                        Check(chain->render(fx.Frame(), fx.Target(), {}).has_value(), "Image::update: render A");
                        auto gotA = fx.Readback();
                        if (gotA)
                                CheckBytes(*gotA, patternA, "Image::update: first content matches");
                        auto updated = fx.input->update(patternB.data(), patternB.size());
                        Check(updated.has_value(), "Image::update: update succeeds");
                        if (updated) {
                                Check(chain->render(fx.Frame(), fx.Target(), {}).has_value(),
                                    "Image::update: render B");
                                auto gotB = fx.Readback();
                                if (gotB)
                                        CheckBytes(*gotB, patternB, "Image::update: updated content matches");
                        }
                }
        }

        // ------------------------------------------------------------------
        // 5. Validation negatives for the M4 output paths.
        {
                const std::vector<uint8_t> pattern = MakePattern(96, 64);
                Fixture fx(*device, pattern, 96, 64, 96, 64);
                auto chain = slv::Chain::create(*device, *slv::Preset::load(*device, dataDir / "runner4/blit.slangp"),
                    {.outputWidth = 96, .outputHeight = 64});
                Check(chain.has_value(), "negatives: chain creates");
                if (!chain) {
                        ++failures;
                } else {

                slv::SwapchainFrame bad;
                bad.format = VK_FORMAT_R8G8B8A8_UNORM;
                bad.width = 96;
                bad.height = 64;
                Check(!chain->render(fx.Frame(), slv::OutputTarget{bad}, {}).has_value(),
                    "negatives: null swapchain view rejected");

                slv::SwapchainFrame zero = bad;
                zero.swapchainImageView = fx.output->view();
                zero.width = 0;
                Check(!chain->render(fx.Frame(), slv::OutputTarget{zero}, {}).has_value(),
                    "negatives: zero swapchain size rejected");

                slv::SwapchainFrame undefinedFormat = bad;
                undefinedFormat.swapchainImageView = fx.output->view();
                undefinedFormat.format = VK_FORMAT_UNDEFINED;
                Check(!chain->render(fx.Frame(), slv::OutputTarget{undefinedFormat}, {}).has_value(),
                    "negatives: undefined swapchain format rejected");

                slv::SwapchainFrame resized = bad;
                resized.swapchainImageView = fx.output->view();
                resized.width = 128;
                Check(!chain->render(fx.Frame(), slv::OutputTarget{resized}, {}).has_value(),
                    "negatives: swapchain size mismatch rejected");

                slv::SwapchainFrame oobRect = bad;
                oobRect.swapchainImageView = fx.output->view();
                oobRect.outputRect = {64, 0, 96, 64};
                Check(!chain->render(fx.Frame(), slv::OutputTarget{oobRect}, {}).has_value(),
                    "negatives: outputRect outside the target rejected");

                auto swapchainOnly = slv::Chain::create(*device, *slv::Preset::load(*device, dataDir / "runner4/blit.slangp"),
                    {.outputWidth = 96, .outputHeight = 64, .outputFormat = VK_FORMAT_UNDEFINED});
                Check(swapchainOnly.has_value(), "negatives: swapchain-only chain (UNDEFINED format) creates");
                if (swapchainOnly)
                        Check(!swapchainOnly->render(fx.Frame(), fx.Target(), {}).has_value(),
                            "negatives: offscreen render on UNDEFINED-format chain rejected");
                }
        }

        // ------------------------------------------------------------------
        // 6. Swapchain end-to-end on VK_EXT_headless_surface: acquire ->
        // render -> present per frame, then read the presented images back
        // and compare byte-exact against the offscreen reference. Skips when
        // the surface or swapchain extension is unavailable.
        {
                slv::DeviceOptions swapOptions;
                swapOptions.appName = "libslang-vk-m4-swapchain";
                swapOptions.validationLayers = true;
                swapOptions.instanceExtensions = {"VK_KHR_surface", "VK_EXT_headless_surface"};
                swapOptions.deviceExtensions = {"VK_KHR_swapchain"};
                auto device2 = slv::Device::create(swapOptions);
                if (!device2) {
                        std::printf("SKIP swapchain e2e (device with surface extensions unavailable: %s)\n",
                            device2.error().message.c_str());
                } else {
                        VkDevice dev = device2->device();
                        VkSurfaceKHR surface = VK_NULL_HANDLE;
                        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
                        VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
                        VkCommandPool cmdPool = VK_NULL_HANDLE;
                        std::vector<VkImage> swapImages;
                        std::vector<VkImageView> swapViews;

                        VkHeadlessSurfaceCreateInfoEXT surfaceInfo{};
                        surfaceInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
                        const VkResult surfaceResult = vkCreateHeadlessSurfaceEXT(device2->instance(), &surfaceInfo,
                            nullptr, &surface);
                        VkBool32 presentSupported = VK_FALSE;
                        VkSurfaceCapabilitiesKHR caps{};
                        if (surfaceResult == VK_SUCCESS)
                                vkGetPhysicalDeviceSurfaceSupportKHR(device2->physicalDevice(),
                                    device2->graphicsFamily(), surface, &presentSupported);
                        if (surfaceResult == VK_SUCCESS && presentSupported)
                                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device2->physicalDevice(), surface, &caps);
                        constexpr VkExtent2D kSwapExtent{128, 64};
                        const bool capsOk = surfaceResult == VK_SUCCESS && presentSupported
                            && kSwapExtent.width >= caps.minImageExtent.width
                            && kSwapExtent.height >= caps.minImageExtent.height
                            && kSwapExtent.width <= caps.maxImageExtent.width
                            && kSwapExtent.height <= caps.maxImageExtent.height;
                        if (!capsOk) {
                                std::printf("SKIP swapchain e2e (headless surface/swapchain unavailable: surface=%d "
                                            "present=%d)\n",
                                    static_cast<int>(surfaceResult), static_cast<int>(presentSupported));
                                if (surface != VK_NULL_HANDLE)
                                        vkDestroySurfaceKHR(device2->instance(), surface, nullptr);
                        } else {
                                // Surface format: prefer RGBA8; most drivers only
                                // list BGRA8 (readback bytes then swizzle).
                                uint32_t formatCount = 0;
                                vkGetPhysicalDeviceSurfaceFormatsKHR(device2->physicalDevice(), surface,
                                    &formatCount, nullptr);
                                std::vector<VkSurfaceFormatKHR> formats(formatCount);
                                vkGetPhysicalDeviceSurfaceFormatsKHR(device2->physicalDevice(), surface,
                                    &formatCount, formats.data());
                                VkFormat swapFormat = formats.empty() ? VK_FORMAT_R8G8B8A8_UNORM
                                                                      : formats[0].format;
                                bool bgra = false;
                                for (const VkSurfaceFormatKHR &f : formats)
                                        if (f.format == VK_FORMAT_R8G8B8A8_UNORM) {
                                                swapFormat = VK_FORMAT_R8G8B8A8_UNORM;
                                                bgra = false;
                                                break;
                                        }
                                if (swapFormat != VK_FORMAT_R8G8B8A8_UNORM)
                                        for (const VkSurfaceFormatKHR &f : formats)
                                                if (f.format == VK_FORMAT_B8G8R8A8_UNORM) {
                                                        swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
                                                        bgra = true;
                                                        break;
                                                }
                                const uint32_t minImages = std::max(2u, caps.minImageCount);

                                VkSwapchainCreateInfoKHR swapInfo{};
                                swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
                                swapInfo.surface = surface;
                                swapInfo.minImageCount = minImages;
                                swapInfo.imageFormat = swapFormat;
                                swapInfo.imageColorSpace = formats.empty() ? VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
                                                                           : formats[0].colorSpace;
                                swapInfo.imageExtent = kSwapExtent;
                                swapInfo.imageArrayLayers = 1;
                                swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                                swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
                                swapInfo.preTransform = caps.currentTransform;
                                swapInfo.compositeAlpha = (caps.supportedCompositeAlpha
                                                              & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                        ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                        : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
                                swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
                                swapInfo.clipped = VK_FALSE;
                                const VkResult swapResult = vkCreateSwapchainKHR(dev, &swapInfo, nullptr, &swapchain);
                                Check(swapResult == VK_SUCCESS, "swapchain e2e: swapchain creates");

                                VkSemaphoreCreateInfo semInfo{};
                                semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                                vkCreateSemaphore(dev, &semInfo, nullptr, &acquireSemaphore);

                                VkCommandPoolCreateInfo poolInfo{};
                                poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                                poolInfo.queueFamilyIndex = device2->graphicsFamily();
                                vkCreateCommandPool(dev, &poolInfo, nullptr, &cmdPool);

                                uint32_t imageCount = 0;
                                vkGetSwapchainImagesKHR(dev, swapchain, &imageCount, nullptr);
                                swapImages.resize(imageCount);
                                vkGetSwapchainImagesKHR(dev, swapchain, &imageCount, swapImages.data());
                                swapViews.resize(imageCount);
                                for (uint32_t i = 0; i < imageCount; ++i) {
                                        VkImageViewCreateInfo viewInfo{};
                                        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                                        viewInfo.image = swapImages[i];
                                        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                                        viewInfo.format = swapFormat;
                                        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                                        vkCreateImageView(dev, &viewInfo, nullptr, &swapViews[i]);
                                }

                                const std::vector<uint8_t> pattern = MakePattern(96, 64);
                                Fixture fx(*device2, pattern, 96, 64, kSwapExtent.width, kSwapExtent.height);
                                auto preset = slv::Preset::load(*device2, dataDir / "runner4/blit.slangp");
                                auto chain = slv::Chain::create(*device2, *preset,
                                    {.outputWidth = kSwapExtent.width, .outputHeight = kSwapExtent.height});
                                Check(chain.has_value(), "swapchain e2e: chain creates");
                                if (chain) {
                                        // Three presentation cases: auto fit+center
                                        // rect (16,0,96,64) and two explicit rects;
                                        // all scale-1 so goldens are byte-exact.
                                        const std::vector<slv::Rect> cases = {
                                            {}, // empty = fit + center
                                            {32, 0, 96, 64},
                                            {0, 0, 96, 64},
                                        };
                                        std::map<uint32_t, int> caseForImage;
                                        bool allFramesOk = true;
                                        for (size_t c = 0; c < cases.size(); ++c) {
                                                uint32_t idx = 0;
                                                const VkResult acquired = vkAcquireNextImageKHR(dev, swapchain,
                                                    UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &idx);
                                                Check(acquired == VK_SUCCESS, "swapchain e2e: acquire");
                                                if (acquired != VK_SUCCESS) {
                                                        allFramesOk = false;
                                                        break;
                                                }
                                                slv::SwapchainFrame frame;
                                                frame.swapchainImageView = swapViews[idx];
                                                frame.format = swapFormat;
                                                frame.width = kSwapExtent.width;
                                                frame.height = kSwapExtent.height;
                                                frame.outputRect = cases[c];
                                                frame.acquireSemaphore = acquireSemaphore;
                                                auto rendered = chain->render(fx.Frame(),
                                                    slv::OutputTarget{frame}, {});
                                                Check(rendered.has_value(),
                                                    "swapchain e2e: render case " + std::to_string(c));
                                                if (!rendered) {
                                                        allFramesOk = false;
                                                        break;
                                                }
                                                VkPresentInfoKHR presentInfo{};
                                                presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                                                presentInfo.swapchainCount = 1;
                                                presentInfo.pSwapchains = &swapchain;
                                                presentInfo.pImageIndices = &idx;
                                                const VkResult presented = vkQueuePresentKHR(
                                                    device2->graphicsQueue(), &presentInfo);
                                                Check(presented == VK_SUCCESS,
                                                    "swapchain e2e: present case " + std::to_string(c));
                                                if (presented != VK_SUCCESS) {
                                                        allFramesOk = false;
                                                        break;
                                                }
                                                caseForImage[idx] = static_cast<int>(c);
                                        }

                                        if (allFramesOk) {
                                                // Read back every presented image, one re-acquire
                                                // per image: the acquire semaphore is what makes
                                                // the present -> readback ordering validation-
                                                // visible (synchronization validation does not
                                                // model host waitIdle).
                                                const auto transition = [&](VkImage image, VkImageLayout from,
                                                                                  VkImageLayout to,
                                                                                  VkSemaphore waitSem,
                                                                                  VkAccessFlags srcAccess,
                                                                                  VkAccessFlags dstAccess) {
                                                        VkCommandBufferAllocateInfo allocInfo{};
                                                        allocInfo.sType
                                                            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                                                        allocInfo.commandPool = cmdPool;
                                                        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                                                        allocInfo.commandBufferCount = 1;
                                                        VkCommandBuffer cmd = VK_NULL_HANDLE;
                                                        vkAllocateCommandBuffers(dev, &allocInfo, &cmd);
                                                        VkCommandBufferBeginInfo beginInfo{};
                                                        beginInfo.sType
                                                            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                                                        beginInfo.flags
                                                            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                                                        vkBeginCommandBuffer(cmd, &beginInfo);
                                                        VkImageMemoryBarrier barrier{};
                                                        barrier.sType
                                                            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                                                        barrier.srcQueueFamilyIndex
                                                            = VK_QUEUE_FAMILY_IGNORED;
                                                        barrier.dstQueueFamilyIndex
                                                            = VK_QUEUE_FAMILY_IGNORED;
                                                        barrier.oldLayout = from;
                                                        barrier.newLayout = to;
                                                        barrier.image = image;
                                                        barrier.subresourceRange = {
                                                            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                                                        barrier.srcAccessMask = srcAccess;
                                                        barrier.dstAccessMask = dstAccess;
                                                        vkCmdPipelineBarrier(cmd,
                                                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                                                            0, nullptr, 0, nullptr, 1, &barrier);
                                                        vkEndCommandBuffer(cmd);
                                                        VkFenceCreateInfo fenceInfo{};
                                                        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                                                        VkFence fence = VK_NULL_HANDLE;
                                                        vkCreateFence(dev, &fenceInfo, nullptr, &fence);
                                                        VkSubmitInfo submitInfo{};
                                                        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                                                        submitInfo.commandBufferCount = 1;
                                                        submitInfo.pCommandBuffers = &cmd;
                                                        VkPipelineStageFlags waitStage
                                                            = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                                                        if (waitSem != VK_NULL_HANDLE) {
                                                                submitInfo.waitSemaphoreCount = 1;
                                                                submitInfo.pWaitSemaphores = &waitSem;
                                                                submitInfo.pWaitDstStageMask = &waitStage;
                                                        }
                                                        vkQueueSubmit(device2->graphicsQueue(), 1, &submitInfo,
                                                            fence);
                                                        vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
                                                        vkDestroyFence(dev, fence, nullptr);
                                                        vkFreeCommandBuffers(dev, cmdPool, 1, &cmd);
                                                };
                                                const auto presentBack = [&](uint32_t index) {
                                                        VkPresentInfoKHR presentInfo{};
                                                        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                                                        presentInfo.swapchainCount = 1;
                                                        presentInfo.pSwapchains = &swapchain;
                                                        presentInfo.pImageIndices = &index;
                                                        vkQueuePresentKHR(device2->graphicsQueue(), &presentInfo);
                                                };

                                                std::map<uint32_t, bool> readBack;
                                                uint32_t processedCount = 0;
                                                // Headless presenters may never retire images (present
                                                // is a no-op), so FIFO can keep handing back already
                                                // processed ones: bound the attempts and let a
                                                // finite acquire timeout end the sweep. Each
                                                // acquire gets its own semaphore — one left
                                                // signaled (a dropped duplicate) must never be
                                                // handed to the next acquire.
                                                const uint32_t maxAttempts = imageCount * 8;
                                                for (uint32_t attempt = 0;
                                                    attempt < maxAttempts && processedCount < imageCount;
                                                    ++attempt) {
                                                        VkSemaphore acquireSem = VK_NULL_HANDLE;
                                                        VkSemaphoreCreateInfo semInfo{};
                                                        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                                                        vkCreateSemaphore(dev, &semInfo, nullptr, &acquireSem);
                                                        uint32_t idx = 0;
                                                        const VkResult acquired = vkAcquireNextImageKHR(dev,
                                                            swapchain, 250000000ULL /* 250 ms */, acquireSem,
                                                            VK_NULL_HANDLE, &idx);
                                                        if (acquired != VK_SUCCESS) {
                                                                // timeout/exhausted: images still in flight
                                                                vkDestroySemaphore(dev, acquireSem, nullptr);
                                                                break;
                                                        }
                                                        if (readBack[idx]) {
                                                                // FIFO handed back an image we already
                                                                // processed and its presentation may never
                                                                // retire here: leave it acquired (the
                                                                // swapchain retires it on destroy) and drop
                                                                // the still-signaled semaphore.
                                                                vkDestroySemaphore(dev, acquireSem, nullptr);
                                                                continue;
                                                        }
                                                        readBack[idx] = true;
                                                        ++processedCount;
                                                        // UNDEFINED -> TRANSFER_SRC: an acquired image
                                                        // reads UNDEFINED to validation (WSI), and the
                                                        // acquire semaphore orders the transition after
                                                        // the presentation engine released the image.
                                                        // Pixel content survives present -> acquire on
                                                        // FIFO presenters — the goldens below pin it.
                                                        transition(swapImages[idx],
                                                            VK_IMAGE_LAYOUT_UNDEFINED,
                                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                            acquireSem, 0, VK_ACCESS_TRANSFER_READ_BIT);
                                                        const auto it = caseForImage.find(idx);
                                                        if (it != caseForImage.end()) {
                                                                auto got = device2->readPixels(swapImages[idx],
                                                                    swapFormat, kSwapExtent.width,
                                                                    kSwapExtent.height,
                                                                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                                                                Check(got.has_value(),
                                                                    "swapchain e2e: readback image "
                                                                        + std::to_string(idx));
                                                                if (got) {
                                                                        // Case 0 auto-fits to (16,0); cases 1/2
                                                                        // are explicit.
                                                                        const int32_t offsetX = it->second == 0
                                                                            ? 16
                                                                            : (it->second == 1 ? 32 : 0);
                                                                        std::vector<uint8_t> want = PlacedFrame(
                                                                            pattern, 96, 64, kSwapExtent.width,
                                                                            kSwapExtent.height, offsetX, 0);
                                                                        if (bgra) {
                                                                                // B8G8R8A8 readback: bytes are
                                                                                // B,G,R,A.
                                                                                for (size_t t = 0;
                                                                                    t + 3 < want.size(); t += 4)
                                                                                        std::swap(want[t],
                                                                                            want[t + 2]);
                                                                        }
                                                                        CheckBytes(*got, want,
                                                                            "swapchain e2e: image "
                                                                                + std::to_string(idx)
                                                                                + " matches case "
                                                                                + std::to_string(it->second));
                                                                }
                                                        }
                                                        // Rest in PRESENT_SRC and give it back.
                                                        transition(swapImages[idx],
                                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_NULL_HANDLE,
                                                            VK_ACCESS_TRANSFER_READ_BIT, 0);
                                                        presentBack(idx);
                                                        vkDestroySemaphore(dev, acquireSem, nullptr);
                                                }
                                                device2->waitIdle();
                                        }
                                }

                                for (VkImageView view : swapViews)
                                        if (view != VK_NULL_HANDLE)
                                                vkDestroyImageView(dev, view, nullptr);
                                if (cmdPool != VK_NULL_HANDLE)
                                        vkDestroyCommandPool(dev, cmdPool, nullptr);
                                if (acquireSemaphore != VK_NULL_HANDLE)
                                        vkDestroySemaphore(dev, acquireSemaphore, nullptr);
                                if (swapchain != VK_NULL_HANDLE)
                                        vkDestroySwapchainKHR(dev, swapchain, nullptr);
                                if (surface != VK_NULL_HANDLE)
                                        vkDestroySurfaceKHR(device2->instance(), surface, nullptr);

                                auto dev2Errors = device2->validationErrors();
                                Check(dev2Errors.empty(), "swapchain device: zero validation errors");
                                if (!dev2Errors.empty())
                                        for (const auto &e : dev2Errors)
                                                std::printf("  validation: %s\n", e.c_str());
                        }
                }
        }

        auto devErrors = device->validationErrors();
        Check(devErrors.empty(), "main device: zero validation errors");
        if (!devErrors.empty())
                for (const auto &e : devErrors)
                        std::printf("  validation: %s\n", e.c_str());

        if (failures == 0)
                std::printf("\nAll M4 tests passed.\n");
        else
                std::printf("\n%d M4 test(s) FAILED.\n", failures);
        return failures == 0 ? 0 : 1;
}

#endif // SLANGVK_ENABLE_VK
