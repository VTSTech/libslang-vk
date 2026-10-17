// libslang-vk M1 unit test — Vulkan runner headless execution (plan §11 M1
// exit criteria): single- and multi-pass offscreen render + golden blit
// round-trip on lavapipe or any real GPU.
//
// Skips cleanly (exit 0) when no Vulkan device is present — CI images
// provide lavapipe (mesa-vulkan-drivers); Windows CI has no ICD.
// Validation layers run whenever VK_LAYER_KHRONOS_validation is available;
// any ERROR-severity validation message fails the test.

#include <algorithm>
#include <cmath>
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
        // Device: headless create + validation layers when present.
        slv::DeviceOptions options;
        options.appName = "libslang-vk-runner-test";
        options.validationLayers = true;
        auto device = slv::Device::create(options);
        if (!device) {
                std::printf("SKIP runner_headless: no Vulkan device (%s)\n", device.error().message.c_str());
                return 0;
        }
        std::printf("runner device: %s\n", device->properties().deviceName);

        const std::string dataDir = SLANGVK_TESTS_DATA_DIR;

        // Input image + offscreen target.
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

        // --- M1 gate: golden blit round-trip (single pass) -------------------
        {
                auto preset = slv::Preset::load(*device, dataDir + "/runner/blit.slangp");
                Check(preset.has_value(), "blit preset loads");
                if (preset) {
                        slv::ChainOptions chainOptions;
                        chainOptions.outputWidth = kWidth;
                        chainOptions.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, chainOptions);
                        Check(chain.has_value(), "blit chain creates");
                        if (chain) {
                                const auto rendered = chain->render(makeInput(), makeOutput(), slv::FrameInfo{});
                                Check(rendered.has_value(), "blit renders headless");
                                if (rendered) {
                                        auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM,
                                            kWidth, kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                        Check(pixels.has_value(), "blit readback");
                                        if (pixels)
                                                CheckBytes(*pixels, pattern, "golden blit matches input");
                                }
                        }
                }
        }

        // --- Two-pass chain: FBO indirection + alias binding ------------------
        {
                auto preset = slv::Preset::load(*device, dataDir + "/runner/twopass.slangp");
                Check(preset.has_value(), "twopass preset loads");
                if (preset) {
                        slv::ChainOptions chainOptions;
                        chainOptions.outputWidth = kWidth;
                        chainOptions.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, chainOptions);
                        Check(chain.has_value(), "twopass chain creates");
                        if (chain) {
                                const auto rendered = chain->render(makeInput(), makeOutput(), slv::FrameInfo{});
                                Check(rendered.has_value(), "twopass renders headless");
                                if (rendered) {
                                        auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM,
                                            kWidth, kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                        Check(pixels.has_value(), "twopass readback");
                                        if (pixels)
                                                CheckBytes(*pixels, pattern, "twopass golden matches input");
                                }
                        }
                }
        }

        // --- Parameter fill: GAIN=2.0 override over the pragma default --------
        {
                auto preset = slv::Preset::load(*device, dataDir + "/runner/param.slangp");
                Check(preset.has_value(), "param preset loads");
                if (preset) {
                        Check(preset->parameterInfo().size() == 1
                                && preset->parameterInfo()[0].name == "GAIN"
                                && std::abs(preset->parameterInfo()[0].defaultValue - 2.0F) < 1e-6F,
                            "param metadata merges preset override");
                        slv::ChainOptions chainOptions;
                        chainOptions.outputWidth = kWidth;
                        chainOptions.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, chainOptions);
                        Check(chain.has_value(), "param chain creates");
                        if (chain) {
                                const auto rendered = chain->render(makeInput(), makeOutput(), slv::FrameInfo{});
                                Check(rendered.has_value(), "param renders headless");
                                if (rendered) {
                                        auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM,
                                            kWidth, kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                        Check(pixels.has_value(), "param readback");
                                        if (pixels) {
                                                // UNORM pipeline: byte*2/255 in, *255 out; values that would
                                                // exceed 255 clamp there. Float32 rounding stays far below
                                                // the +-1 byte tolerance used here.
                                                size_t bad = 0;
                                                size_t firstBad = 0;
                                                for (size_t i = 0; i < pattern.size(); ++i) {
                                                        const float wantF = std::min(255.0F, static_cast<float>(pattern[i]) * 2.0F);
                                                        const int want = static_cast<int>(wantF + 0.5F);
                                                        const int got = static_cast<int>((*pixels)[i]);
                                                        if (std::abs(got - want) > 1) {
                                                                if (bad == 0)
                                                                        firstBad = i;
                                                                ++bad;
                                                        }
                                                }
                                                if (bad != 0) {
                                                        std::printf("FAIL param gain doubles pixels (%zu diffs, first byte %zu: got %d)\n",
                                                            bad, firstBad, static_cast<int>((*pixels)[firstBad]));
                                                        ++failures;
                                                } else {
                                                        std::printf("PASS param gain doubles pixels\n");
                                                }
                                        }
                                }
                        }
                }
        }

        // --- max_fbo_width/height clamp (miss list #3): the pass FBO is
        // capped and downstream Source-scale resolves against the clamped
        // size ---------------------------------------------------------------
        {
                auto preset = slv::Preset::load(*device, dataDir + "/runner/maxfbo.slangp");
                Check(preset.has_value(), "maxfbo preset loads");
                if (preset) {
                        Check(preset->passCount() == 2, "maxfbo preset has two passes");
                        slv::ChainOptions chainOptions;
                        chainOptions.outputWidth = kWidth;
                        chainOptions.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, chainOptions);
                        Check(chain.has_value(), "maxfbo chain creates");
                        if (chain) {
                                const auto rendered = chain->render(makeInput(), makeOutput(), slv::FrameInfo{});
                                Check(rendered.has_value(), "maxfbo chain renders headless");
                                if (rendered) {
                                        const auto size0 = chain->passOutputSize(0);
                                        const auto size1 = chain->passOutputSize(1);
                                        Check(size0.has_value() && size0->first == 16 && size0->second == 16,
                                            "max_fbo clamps the 64x64 pass-0 FBO to 16x16");
                                        Check(size1.has_value() && size1->first == 16 && size1->second == 16,
                                            "max_fbo cap holds chain-wide: pass 1 resolves 2x the clamped "
                                            "pass-0 FBO (32) and is clamped back to 16");
                                }
                        }
                }
        }

        // --- Validation layers: any ERROR severity fails the run --------------
        const std::vector<std::string> validationErrors = device->validationErrors();
        if (!validationErrors.empty()) {
                std::printf("FAIL validation reported %zu error(s):\n", validationErrors.size());
                for (size_t i = 0; i < std::min<size_t>(validationErrors.size(), 5); ++i)
                        std::printf("  [validation] %s\n", validationErrors[i].c_str());
                ++failures;
        } else {
                std::printf("PASS validation layers clean\n");
        }

        std::printf("%s\n", failures == 0 ? "runner_headless: all checks passed" : "runner_headless: FAILURES");
        return failures == 0 ? 0 : 1;
}

#endif // SLANGVK_ENABLE_VK
