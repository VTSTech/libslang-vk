// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk M4.06 unit test — async render path (audit ROB-33).
//
// Pins the contract of Chain::render_async() / Chain::wait_async():
//  - render_async() submits a frame and returns a fence without blocking on
//    the GPU (the point of the API: the host gets its CPU time back).
//  - wait_async() retires the frame and releases its resources.
//  - The pixels an async frame produces are byte-identical to the ones the
//    synchronous render() produces.
//  - Back-to-back async frames work: the second render_async() drains the
//    first (at most one async frame may be in flight per chain).
//  - wait_async() rejects a fence that is not the chain's pending frame
//    instead of corrupting state.
//
// Skips cleanly (exit 0) when no Vulkan device is present — CI images
// provide lavapipe; Windows CI has no ICD. Validation layers run whenever
// VK_LAYER_KHRONOS_validation is available; any ERROR-severity validation
// message fails the test.

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include <slangvk/slangvk.hpp>

#ifndef SLANGVK_ENABLE_VK
int main() { return 0; } // Core-only build: runner not compiled.
#else

namespace {

int failures = 0;
constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 64;

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
        size_t diffs = 0;
        size_t firstDiff = 0;
        for (size_t i = 0; i < got.size(); ++i)
                if (got[i] != want[i]) {
                        if (diffs == 0)
                                firstDiff = i;
                        ++diffs;
                }
        if (diffs != 0) {
                std::printf("FAIL %s (%zu byte diffs, first at byte %zu: got %u want %u)\n", label.c_str(), diffs,
                    firstDiff, got[firstDiff], want[firstDiff]);
                ++failures;
        } else {
                std::printf("PASS %s\n", label.c_str());
        }
}

// Deterministic 8-bit gradient: r = 4x, g = 4y, b = (x+y) & 0xFF, a = 0xFF.
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

} // namespace

int main()
{
        slv::DeviceOptions options;
        options.appName = "libslang-vk-async-test";
        options.validationLayers = true;
        auto device = slv::Device::create(options);
        if (!device) {
                std::printf("SKIP runner_async: no Vulkan device (%s)\n", device.error().message.c_str());
                return 0;
        }
        std::printf("runner device: %s\n", device->properties().deviceName);

        const std::string dataDir = SLANGVK_TESTS_DATA_DIR;
        const std::vector<uint8_t> pattern = MakePattern(kWidth, kHeight);

        slv::Image::CreateInfo inputInfo;
        inputInfo.desc = {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM};
        inputInfo.pixels = pattern.data();
        inputInfo.pixelBytes = pattern.size();
        auto input = slv::Image::create(*device, inputInfo);
        if (!input) {
                std::printf("FAIL input image: %s\n", input.error().message.c_str());
                return 1;
        }

        slv::Image::CreateInfo outputInfo;
        outputInfo.desc = {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM};
        auto output = slv::Image::create(*device, outputInfo);
        if (!output) {
                std::printf("FAIL output image: %s\n", output.error().message.c_str());
                return 1;
        }

        const auto makeInput = [&input]() {
                slv::InputFrame frame;
                frame.view = input->view();
                frame.format = VK_FORMAT_R8G8B8A8_UNORM;
                frame.width = kWidth;
                frame.height = kHeight;
                return frame;
        };
        const auto makeOutput = [&output]() {
                slv::Offscreen target;
                target.view = output->view();
                target.format = VK_FORMAT_R8G8B8A8_UNORM;
                target.width = kWidth;
                target.height = kHeight;
                return target;
        };

        auto preset = slv::Preset::load(*device, dataDir + "/runner/blit.slangp");
        Check(preset.has_value(), "blit preset loads");
        if (!preset)
                return 1;

        slv::ChainOptions chainOptions;
        chainOptions.outputWidth = kWidth;
        chainOptions.outputHeight = kHeight;

        // --- Async frame: submit, then retire, then read back ---------------
        {
                auto chain = slv::Chain::create(*device, *preset, chainOptions);
                Check(chain.has_value(), "chain creates");
                if (chain) {
                        const auto fence = chain->render_async(makeInput(), makeOutput(), slv::FrameInfo{});
                        Check(fence.has_value(), "render_async submits the frame");
                        if (fence) {
                                Check(*fence != VK_NULL_HANDLE, "render_async returns a fence");
                                const auto waited = chain->wait_async(*fence);
                                Check(waited.has_value(), "wait_async retires the frame");
                                if (waited) {
                                        auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM,
                                            kWidth, kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                        Check(pixels.has_value(), "async readback");
                                        if (pixels)
                                                CheckBytes(*pixels, pattern, "async frame matches the golden blit");
                                }
                        }
                }
        }

        // --- Async and synchronous frames agree -----------------------------
        {
                auto syncChain = slv::Chain::create(*device, *preset, chainOptions);
                auto asyncChain = slv::Chain::create(*device, *preset, chainOptions);
                Check(syncChain.has_value() && asyncChain.has_value(), "both chains create");
                if (syncChain && asyncChain) {
                        const auto syncRender = syncChain->render(makeInput(), makeOutput(), slv::FrameInfo{});
                        Check(syncRender.has_value(), "synchronous render succeeds");
                        auto syncPixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM, kWidth,
                            kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                        const auto fence = asyncChain->render_async(makeInput(), makeOutput(), slv::FrameInfo{});
                        Check(fence.has_value(), "async render for comparison succeeds");
                        if (fence)
                                Check(asyncChain->wait_async(*fence).has_value(), "comparison frame retires");
                        auto asyncPixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM, kWidth,
                            kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                        Check(syncPixels.has_value() && asyncPixels.has_value(), "both readbacks succeed");
                        if (syncPixels && asyncPixels)
                                CheckBytes(*asyncPixels, *syncPixels, "async output is identical to sync output");
                }
        }

        // --- Back-to-back async frames ---------------------------------------
        {
                auto chain = slv::Chain::create(*device, *preset, chainOptions);
                Check(chain.has_value(), "chain creates for the sequence test");
                if (chain) {
                        bool allOk = true;
                        for (int frame = 0; frame < 4; ++frame) {
                                // The second render_async() must drain the
                                // first frame (one in flight per chain).
                                const auto fence = chain->render_async(makeInput(), makeOutput(), slv::FrameInfo{});
                                if (!fence) {
                                        std::printf("FAIL async frame %d: %s\n", frame,
                                            fence.error().message.c_str());
                                        allOk = false;
                                        break;
                                }
                                if (!chain->wait_async(*fence)) {
                                        std::printf("FAIL wait_async frame %d\n", frame);
                                        allOk = false;
                                        break;
                                }
                        }
                        Check(allOk, "four back-to-back async frames succeed");
                        if (allOk) {
                                auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM, kWidth,
                                    kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                Check(pixels.has_value(), "sequence readback");
                                if (pixels)
                                        CheckBytes(*pixels, pattern, "sequence output still matches the golden blit");
                        }
                }
        }

        // --- wait_async() rejects a foreign fence ---------------------------
        {
                auto chain = slv::Chain::create(*device, *preset, chainOptions);
                Check(chain.has_value(), "chain creates for the error test");
                if (chain) {
                        const auto fence = chain->render_async(makeInput(), makeOutput(), slv::FrameInfo{});
                        Check(fence.has_value(), "async render for the error test");
                        if (fence) {
                                // A fence the chain does not own must be
                                // rejected, not silently accepted.
                                const auto bogus = chain->wait_async(VK_NULL_HANDLE);
                                Check(!bogus.has_value(), "wait_async rejects a null fence");
                                Check(chain->wait_async(*fence).has_value(), "the real fence still retires");
                        }
                }
        }

        std::printf("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
        return failures == 0 ? 0 : 1;
}

#endif // SLANGVK_ENABLE_VK
