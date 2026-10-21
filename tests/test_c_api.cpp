// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk C API smoke test — the extern "C" surface (slangvk/c.h)
// drives the same headless path as the C++ runner tests, end to end:
// device create (skip-clean without ICD), image create/update, preset
// load, parameter metadata + live override, identity/gain chains, resize,
// error paths — all with byte-exact or +-1 readback expectations.
//
// The test TU consumes ONLY the C header (no slangvk C++ includes), so it
// doubles as the proof that the C surface is self-sufficient. Readback
// goes through slangvk_device_read_pixels. The shim makes no Vulkan calls
// of its own; validation-layer cleanliness is inherited from the C++
// runner tests that exercise the same library paths.
//
// Skips cleanly (exit 0) when no Vulkan device is present — CI images
// provide lavapipe (mesa-vulkan-drivers); Windows CI has no ICD.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <slangvk/c.h>

#ifndef SLANGVK_ENABLE_VK
int main() { return 0; } // Core-only build: runner not compiled.
#else

namespace {

int failures = 0;
constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 64;
constexpr uint32_t kSmall = 32;
constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr size_t kBytesPerTexel = 4;

void Check(bool condition, const std::string &label)
{
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition)
                ++failures;
}

// Deterministic 8-bit gradient: r = 4x, g = 4y, b = (x+y) & 0xFF, a = 0xFF.
std::vector<uint8_t> MakePattern(uint32_t width, uint32_t height)
{
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * kBytesPerTexel);
        for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x) {
                        const size_t o = (static_cast<size_t>(y) * width + x) * kBytesPerTexel;
                        pixels[o + 0] = static_cast<uint8_t>((x * 4) & 0xFF);
                        pixels[o + 1] = static_cast<uint8_t>((y * 4) & 0xFF);
                        pixels[o + 2] = static_cast<uint8_t>((x + y) & 0xFF);
                        pixels[o + 3] = 0xFF;
                }
        return pixels;
}

// Byte-exact comparison.
void CheckBytes(const std::vector<uint8_t> &got, const std::vector<uint8_t> &want, const std::string &label)
{
        if (got.size() != want.size()) {
                std::printf("FAIL %s (size %zu != %zu)\n", label.c_str(), got.size(), want.size());
                ++failures;
                return;
        }
        size_t count = 0;
        size_t firstDiff = 0;
        for (size_t i = 0; i < got.size(); ++i)
                if (got[i] != want[i]) {
                        if (count == 0)
                                firstDiff = i;
                        ++count;
                }
        if (count != 0) {
                std::printf("FAIL %s (%zu byte diffs, first at byte %zu: got %u want %u)\n", label.c_str(), count,
                    firstDiff, got[firstDiff], want[firstDiff]);
                ++failures;
        } else {
                std::printf("PASS %s\n", label.c_str());
        }
}

// Per-byte comparison with a +-maxDiff tolerance (UNORM round-trips).
void CheckCloseBytes(const std::vector<uint8_t> &got, const std::vector<uint8_t> &want, int maxDiff,
    const std::string &label)
{
        if (got.size() != want.size()) {
                std::printf("FAIL %s (size %zu != %zu)\n", label.c_str(), got.size(), want.size());
                ++failures;
                return;
        }
        size_t count = 0;
        size_t firstDiff = 0;
        for (size_t i = 0; i < got.size(); ++i) {
                const int d = std::abs(static_cast<int>(got[i]) - static_cast<int>(want[i]));
                if (d > maxDiff) {
                        if (count == 0)
                                firstDiff = i;
                        ++count;
                }
        }
        if (count != 0) {
                std::printf("FAIL %s (%zu bytes off by more than %d, first at byte %zu: got %u want %u)\n",
                    label.c_str(), count, maxDiff, firstDiff, got[firstDiff], want[firstDiff]);
                ++failures;
        } else {
                std::printf("PASS %s\n", label.c_str());
        }
}

// Reads an image back through the C API into a tightly packed RGBA buffer.
bool Readback(slangvk_device *device, slangvk_image *image, std::vector<uint8_t> &out)
{
        VkImage vkImage = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        if (slangvk_image_get_handles(image, nullptr, &vkImage, nullptr, &width, &height) != SLANGVK_OK)
                return false;
        out.assign(static_cast<size_t>(width) * height * kBytesPerTexel, 0);
        return slangvk_device_read_pixels(device, vkImage, kFormat, width, height,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, out.data(), out.size())
            == SLANGVK_OK;
}

} // namespace

int main()
{
        // Device: headless create + validation layers when present.
        slangvk_device_options deviceOptions;
        std::memset(&deviceOptions, 0, sizeof(deviceOptions));
        deviceOptions.app_name = "libslang-vk-c-api-test";
        deviceOptions.validation_layers = 1;
        slangvk_device *device = nullptr;
        const slangvk_result deviceResult = slangvk_device_create(&deviceOptions, &device);
        if (deviceResult != SLANGVK_OK || device == nullptr) {
                std::printf("SKIP c_api: no Vulkan device (%s)\n", slangvk_last_error());
                return 0;
        }
        Check(slangvk_last_error()[0] == '\0', "success clears last error");

        // --- Device surface ---------------------------------------------------
        slangvk_device_info info;
        std::memset(&info, 0, sizeof(info));
        Check(slangvk_device_get_info(device, &info) == SLANGVK_OK && info.device_name[0] != '\0',
            "device info reports a name");
        std::printf("c_api device: %s\n", info.device_name);

        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        VkDevice vkDevice = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t family = 0;
        Check(slangvk_device_get_handles(device, &instance, &physical, &vkDevice, &queue, &family) == SLANGVK_OK
                && instance != VK_NULL_HANDLE && physical != VK_NULL_HANDLE && vkDevice != VK_NULL_HANDLE
                && queue != VK_NULL_HANDLE,
            "device handles resolve");
        Check(slangvk_device_wait_idle(device) == SLANGVK_OK, "device wait idle");
        // Device-lost recovery (ROB-35): the C surface exposes the same
        // rebuild the C++ API does. The device's objects die with it, so
        // this runs at the end of the test, after everything else.
        Check(slangvk_device_recover(nullptr) == SLANGVK_ERR_INVALID_ARGUMENT, "device recover rejects NULL");

        const std::string dataDir = SLANGVK_TESTS_DATA_DIR;

        // --- Input image (created with initial pixels) + handles --------------
        const std::vector<uint8_t> pattern = MakePattern(kWidth, kHeight);
        slangvk_image_options imageOptions;
        std::memset(&imageOptions, 0, sizeof(imageOptions));
        imageOptions.width = kWidth;
        imageOptions.height = kHeight;
        imageOptions.format = kFormat;
        imageOptions.pixels = pattern.data();
        imageOptions.pixel_bytes = pattern.size();
        slangvk_image *input = nullptr;
        Check(slangvk_image_create(device, &imageOptions, &input) == SLANGVK_OK && input != nullptr,
            "input image creates");

        VkImageView inputView = VK_NULL_HANDLE;
        VkImage inputImage = VK_NULL_HANDLE;
        VkFormat inputFormat = VK_FORMAT_UNDEFINED;
        uint32_t inputWidth = 0;
        uint32_t inputHeight = 0;
        Check(slangvk_image_get_handles(input, &inputView, &inputImage, &inputFormat, &inputWidth, &inputHeight)
                == SLANGVK_OK
            && inputView != VK_NULL_HANDLE && inputImage != VK_NULL_HANDLE && inputFormat == kFormat
            && inputWidth == kWidth && inputHeight == kHeight,
            "image handles match the create options");

        // --- Identity blit: the M1 golden, through the C surface --------------
        slangvk_preset *blit = nullptr;
        Check(slangvk_preset_load(device, (dataDir + "/runner/blit.slangp").c_str(), nullptr, &blit) == SLANGVK_OK,
            "blit preset loads");
        uint32_t passCount = 0;
        Check(blit != nullptr && slangvk_preset_pass_count(blit, &passCount) == SLANGVK_OK && passCount == 1,
            "blit pass count is 1");

        slangvk_chain *blitChain = nullptr;
        Check(slangvk_chain_create(device, blit, kWidth, kHeight, kFormat, &blitChain) == SLANGVK_OK,
            "blit chain creates");
        if (blitChain != nullptr) {
                slangvk_offscreen target;
                std::memset(&target, 0, sizeof(target));
                slangvk_image *output = nullptr;
                imageOptions.pixels = nullptr;
                imageOptions.pixel_bytes = 0;
                Check(slangvk_image_create(device, &imageOptions, &output) == SLANGVK_OK, "blit output image creates");
                slangvk_image_get_handles(output, &target.view, &target.image, &target.format, &target.width,
                    &target.height);

                slangvk_input_frame inputFrame;
                std::memset(&inputFrame, 0, sizeof(inputFrame));
                inputFrame.view = inputView;
                inputFrame.image = inputImage;
                inputFrame.format = kFormat;
                inputFrame.width = kWidth;
                inputFrame.height = kHeight;
                inputFrame.rotation = SLANGVK_ROTATION_NONE;

                slangvk_frame_info frameInfo { 1, 16000.0F, 1 }; // explicit frame inputs
                Check(slangvk_chain_render_offscreen(blitChain, &inputFrame, &target, &frameInfo) == SLANGVK_OK,
                    "blit renders via the C API");
                std::vector<uint8_t> got;
                if (Readback(device, output, got))
                        CheckBytes(got, pattern, "blit golden matches input");
                else
                        Check(false, "blit readback");
                slangvk_image_destroy(output);
        }

        // --- Two-pass chain: FBO indirection + alias ---------------------------
        slangvk_preset *twopass = nullptr;
        Check(slangvk_preset_load(device, (dataDir + "/runner/twopass.slangp").c_str(), nullptr, &twopass)
                == SLANGVK_OK,
            "twopass preset loads");
        Check(twopass != nullptr && slangvk_preset_pass_count(twopass, &passCount) == SLANGVK_OK && passCount == 2,
            "twopass pass count is 2");
        slangvk_chain *twopassChain = nullptr;
        Check(slangvk_chain_create(device, twopass, kWidth, kHeight, kFormat, &twopassChain) == SLANGVK_OK,
            "twopass chain creates");
        if (twopassChain != nullptr) {
                slangvk_offscreen target;
                std::memset(&target, 0, sizeof(target));
                slangvk_image *output = nullptr;
                imageOptions.pixels = nullptr;
                imageOptions.pixel_bytes = 0;
                slangvk_image_create(device, &imageOptions, &output);
                slangvk_image_get_handles(output, &target.view, &target.image, &target.format, &target.width,
                    &target.height);

                slangvk_input_frame inputFrame;
                std::memset(&inputFrame, 0, sizeof(inputFrame));
                inputFrame.view = inputView;
                inputFrame.image = inputImage;
                inputFrame.format = kFormat;
                inputFrame.width = kWidth;
                inputFrame.height = kHeight;
                inputFrame.rotation = SLANGVK_ROTATION_NONE;

                Check(slangvk_chain_render_offscreen(twopassChain, &inputFrame, &target, nullptr) == SLANGVK_OK,
                    "twopass renders via the C API (null frame info defaults)");
                std::vector<uint8_t> got;
                if (Readback(device, output, got))
                        CheckBytes(got, pattern, "twopass golden matches input");
                else
                        Check(false, "twopass readback");
                slangvk_image_destroy(output);
        }

        // --- Parameter metadata + live override -------------------------------
        slangvk_preset *param = nullptr;
        Check(slangvk_preset_load(device, (dataDir + "/runner/param.slangp").c_str(), nullptr, &param) == SLANGVK_OK,
            "param preset loads");
        uint32_t paramCount = 0;
        Check(param != nullptr && slangvk_preset_parameter_count(param, &paramCount) == SLANGVK_OK && paramCount == 1,
            "param parameter count is 1");
        slangvk_parameter meta;
        std::memset(&meta, 0, sizeof(meta));
        Check(param != nullptr && slangvk_preset_parameter(param, 0, &meta) == SLANGVK_OK
                && std::string(meta.name) == "GAIN" && std::abs(meta.default_value - 2.0F) < 1e-6F
                && std::abs(meta.minimum - 0.0F) < 1e-6F && std::abs(meta.maximum - 4.0F) < 1e-6F
                && std::abs(meta.step - 0.05F) < 1e-6F,
            "param metadata merges the preset override");
        std::printf("c_api parameter: %s — %s\n", meta.name, meta.description);

        slangvk_chain *paramChain = nullptr;
        Check(slangvk_chain_create(device, param, kWidth, kHeight, kFormat, &paramChain) == SLANGVK_OK,
            "param chain creates");
        if (paramChain != nullptr) {
                slangvk_offscreen target;
                std::memset(&target, 0, sizeof(target));
                slangvk_image *output = nullptr;
                imageOptions.pixels = nullptr;
                imageOptions.pixel_bytes = 0;
                slangvk_image_create(device, &imageOptions, &output);
                slangvk_image_get_handles(output, &target.view, &target.image, &target.format, &target.width,
                    &target.height);

                slangvk_input_frame inputFrame;
                std::memset(&inputFrame, 0, sizeof(inputFrame));
                inputFrame.view = inputView;
                inputFrame.image = inputImage;
                inputFrame.format = kFormat;
                inputFrame.width = kWidth;
                inputFrame.height = kHeight;
                inputFrame.rotation = SLANGVK_ROTATION_NONE;

                // Preset default GAIN=2.0: out = clamp(in * 2, 0, 255). UNORM
                // pipeline: byte*2/255 in, *255 out; values that would exceed
                // 255 clamp there. Float32 rounding stays far below the +-1
                // byte tolerance used here (same expectation as the C++ suite).
                std::vector<uint8_t> wantDoubled(pattern.size());
                for (size_t i = 0; i < pattern.size(); ++i)
                        wantDoubled[i] = static_cast<uint8_t>(std::min(255.0F, static_cast<float>(pattern[i]) * 2.0F) + 0.5F);
                Check(slangvk_chain_render_offscreen(paramChain, &inputFrame, &target, nullptr) == SLANGVK_OK,
                    "param renders via the C API");
                std::vector<uint8_t> got;
                if (Readback(device, output, got))
                        CheckCloseBytes(got, wantDoubled, 1, "param gain doubles pixels");
                else
                        Check(false, "param readback");

                // Live override GAIN=1.0: out = in (exact multiply-by-one).
                Check(slangvk_chain_set_parameter(paramChain, "GAIN", 1.0F) == SLANGVK_OK, "set_parameter GAIN=1.0");
                Check(slangvk_chain_render_offscreen(paramChain, &inputFrame, &target, nullptr) == SLANGVK_OK,
                    "param renders again after override");
                if (Readback(device, output, got))
                        CheckCloseBytes(got, pattern, 1, "param override restores the input");
                else
                        Check(false, "param override readback");
                slangvk_image_destroy(output);
        }

        // --- Resize: rebuild the blit chain target at 32x32 --------------------
        if (blitChain != nullptr) {
                Check(slangvk_chain_resize(blitChain, kSmall, kSmall) == SLANGVK_OK, "chain resize to 32x32");

                // A second input image, created empty and filled through the
                // per-frame update path (the host-upload contract).
                const std::vector<uint8_t> patternSmall = MakePattern(kSmall, kSmall);
                slangvk_image_options smallOptions;
                std::memset(&smallOptions, 0, sizeof(smallOptions));
                smallOptions.width = kSmall;
                smallOptions.height = kSmall;
                smallOptions.format = kFormat;
                slangvk_image *smallInput = nullptr;
                Check(slangvk_image_create(device, &smallOptions, &smallInput) == SLANGVK_OK,
                        "small input image creates");
                Check(slangvk_image_update(smallInput, patternSmall.data(), patternSmall.size()) == SLANGVK_OK,
                        "image update fills the small input");

                slangvk_image *smallOutput = nullptr;
                imageOptions.pixels = nullptr;
                imageOptions.pixel_bytes = 0;
                imageOptions.width = kSmall;
                imageOptions.height = kSmall;
                slangvk_image_create(device, &imageOptions, &smallOutput);

                slangvk_input_frame inputFrame;
                std::memset(&inputFrame, 0, sizeof(inputFrame));
                slangvk_image_get_handles(smallInput, &inputFrame.view, &inputFrame.image, &inputFrame.format,
                    &inputFrame.width, &inputFrame.height);
                slangvk_offscreen target;
                std::memset(&target, 0, sizeof(target));
                slangvk_image_get_handles(smallOutput, &target.view, &target.image, &target.format, &target.width,
                    &target.height);

                Check(slangvk_chain_render_offscreen(blitChain, &inputFrame, &target, nullptr) == SLANGVK_OK,
                        "resized chain renders via the C API");
                std::vector<uint8_t> got;
                if (Readback(device, smallOutput, got))
                        CheckBytes(got, patternSmall, "resized blit golden matches 32x32 input");
                else
                        Check(false, "resized readback");
                slangvk_image_destroy(smallInput);
                slangvk_image_destroy(smallOutput);
        }

        // --- Error paths -------------------------------------------------------
        //
        // Every entry point must reject a NULL handle with
        // SLANGVK_ERR_INVALID_ARGUMENT and a message, rather than letting it
        // reach Vulkan (SEC-07). The contract is what a C host can rely on.
        {
                slangvk_device_info info;
                slangvk_image_options imageOptions;
                std::memset(&imageOptions, 0, sizeof(imageOptions));
                slangvk_chain_options chainOptions;
                std::memset(&chainOptions, 0, sizeof(chainOptions));
                slangvk_parameter parameter;
                std::memset(&parameter, 0, sizeof(parameter));
                slangvk_frame_info frameInfo;
                std::memset(&frameInfo, 0, sizeof(frameInfo));
                slangvk_input_frame inputFrame;
                std::memset(&inputFrame, 0, sizeof(inputFrame));
                slangvk_swapchain_frame swapchainFrame;
                std::memset(&swapchainFrame, 0, sizeof(swapchainFrame));
                slangvk_offscreen offscreenTarget;
                std::memset(&offscreenTarget, 0, sizeof(offscreenTarget));
                uint32_t u32 = 0;
                VkImageView view = VK_NULL_HANDLE;
                VkImage image = VK_NULL_HANDLE;
                VkFormat format = kFormat;
                uint32_t w = 0, h = 0;
                void *pixels = nullptr;
                size_t pixelBytes = 0;

                // Device.
                Check(slangvk_device_get_info(nullptr, &info) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "device_get_info rejects a NULL device");
                Check(slangvk_device_get_handles(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "device_get_handles rejects a NULL device");
                Check(slangvk_device_wait_idle(nullptr) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "device_wait_idle rejects a NULL device");
                Check(slangvk_device_recover(nullptr) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "device_recover rejects a NULL device");
                Check(slangvk_device_read_pixels(nullptr, VK_NULL_HANDLE, kFormat, 0, 0,
                        VK_IMAGE_LAYOUT_UNDEFINED, nullptr, 0) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "device_read_pixels rejects NULL arguments");
                Check(slangvk_device_create(nullptr, nullptr) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "device_create rejects NULL arguments");

                // Image.
                Check(slangvk_image_create(nullptr, &imageOptions, nullptr) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "image_create rejects a NULL device");
                Check(slangvk_image_create(device, &imageOptions, nullptr) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "image_create rejects a NULL out pointer");
                Check(slangvk_image_update(nullptr, nullptr, 0) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "image_update rejects a NULL image");
                Check(slangvk_image_get_handles(nullptr, &view, &image, &format, &w, &h)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "image_get_handles rejects a NULL image");

                // Preset.
                Check(slangvk_preset_load(nullptr, "x.slangp", nullptr, nullptr)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "preset_load rejects a NULL device");
                Check(slangvk_preset_load(device, nullptr, nullptr, nullptr) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "preset_load rejects a NULL path");
                Check(slangvk_preset_pass_count(nullptr, &u32) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "preset_pass_count rejects a NULL preset");
                Check(slangvk_preset_parameter_count(nullptr, &u32) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "preset_parameter_count rejects a NULL preset");
                Check(slangvk_preset_parameter(nullptr, 0, &parameter) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "preset_parameter rejects a NULL preset");

                // Chain.
                Check(slangvk_chain_create(nullptr, nullptr, 0, 0, kFormat, nullptr)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "chain_create rejects NULL arguments");
                Check(slangvk_chain_create_ex(device, nullptr, &chainOptions, nullptr)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "chain_create_ex rejects a NULL preset");
                Check(slangvk_chain_resize(nullptr, 0, 0) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "chain_resize rejects a NULL chain");
                Check(slangvk_chain_set_parameter(nullptr, "x", 0.F) == SLANGVK_ERR_INVALID_ARGUMENT,
                    "chain_set_parameter rejects a NULL chain");
                Check(slangvk_chain_pass_output_size(nullptr, 0, &u32, &u32)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "chain_pass_output_size rejects a NULL chain");
                Check(slangvk_chain_render_swapchain(nullptr, &inputFrame, &swapchainFrame, &frameInfo)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "chain_render_swapchain rejects a NULL chain");
                Check(slangvk_chain_render_swapchain(blitChain, nullptr, &swapchainFrame, &frameInfo)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "chain_render_swapchain rejects a NULL input");
                Check(slangvk_chain_render_swapchain(blitChain, &inputFrame, nullptr, &frameInfo)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "chain_render_swapchain rejects a NULL target");
                Check(slangvk_chain_render_offscreen(nullptr, &inputFrame, &offscreenTarget, &frameInfo)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "chain_render_offscreen rejects a NULL chain");
                Check(slangvk_chain_render_offscreen(blitChain, &inputFrame, nullptr, &frameInfo)
                        == SLANGVK_ERR_INVALID_ARGUMENT,
                    "chain_render_offscreen rejects a NULL target");
                // The required view handles (SEC-07): the image handles are
                // optional (only some presets need them).
                Check(slangvk_chain_render_offscreen(blitChain, &inputFrame, &offscreenTarget, &frameInfo)
                        == SLANGVK_ERR_INVALID_ARGUMENT
                    && std::strstr(slangvk_last_error(), "input->view") != nullptr,
                    "chain_render_offscreen rejects a NULL input view, and says which handle");
                // A valid input view, so the target's handle is the one
                // reported.
                slangvk_input_frame goodInput;
                std::memset(&goodInput, 0, sizeof(goodInput));
                goodInput.view = inputView;
                goodInput.format = kFormat;
                goodInput.width = kWidth;
                goodInput.height = kHeight;
                Check(slangvk_chain_render_offscreen(blitChain, &goodInput, &offscreenTarget, &frameInfo)
                        == SLANGVK_ERR_INVALID_ARGUMENT
                    && std::strstr(slangvk_last_error(), "target->view") != nullptr,
                    "chain_render_offscreen rejects a NULL target view, and says which handle");
                Check(slangvk_chain_render_swapchain(blitChain, &goodInput, &swapchainFrame, &frameInfo)
                        == SLANGVK_ERR_INVALID_ARGUMENT
                    && std::strstr(slangvk_last_error(), "swapchain_image_view") != nullptr,
                    "chain_render_swapchain rejects a NULL swapchain view, and says which handle");
        }

        slangvk_preset *missing = nullptr;
        const slangvk_result missingResult =
            slangvk_preset_load(device, (dataDir + "/runner/missing.slangp").c_str(), nullptr, &missing);
        Check(missingResult != SLANGVK_OK && missing == nullptr && slangvk_last_error()[0] != '\0',
            "missing preset fails with a message");

        if (blitChain != nullptr) {
                slangvk_offscreen target;
                std::memset(&target, 0, sizeof(target));
                target.format = kFormat;
                target.width = kWidth;
                target.height = kHeight;
                slangvk_input_frame inputFrame;
                std::memset(&inputFrame, 0, sizeof(inputFrame));
                inputFrame.view = inputView;
                inputFrame.format = kFormat;
                inputFrame.width = kWidth;
                inputFrame.height = kHeight;
                inputFrame.rotation = 5; // out of range: hard invalid_argument
                Check(slangvk_chain_render_offscreen(blitChain, &inputFrame, &target, nullptr)
                        == SLANGVK_ERR_INVALID_ARGUMENT
                    && slangvk_last_error()[0] != '\0',
                    "out-of-range rotation is rejected");
        }
        Check(slangvk_device_wait_idle(device) == SLANGVK_OK && slangvk_last_error()[0] == '\0',
            "success clears last error again");

        // --- Teardown (documented order: chains -> presets -> images -> device)
        slangvk_chain_destroy(paramChain);
        slangvk_chain_destroy(twopassChain);
        slangvk_chain_destroy(blitChain);
        slangvk_preset_destroy(param);
        slangvk_preset_destroy(twopass);
        slangvk_preset_destroy(blit);
        slangvk_image_destroy(input);
        slangvk_device_destroy(device);

        std::printf("%s\n", failures == 0 ? "c_api: all checks passed" : "c_api: FAILURES");
        return failures == 0 ? 0 : 1;
}

#endif // SLANGVK_ENABLE_VK
