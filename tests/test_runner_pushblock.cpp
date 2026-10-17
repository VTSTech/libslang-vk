// libslang-vk runner regression test — the stock libretro shader shape.
//
// pins: global MVP UBO + push-constant block, push block READ by the
// fragment stage (see tests/data/runner/shaders/pushfrag.slang). Chain
// creation must succeed (the pipeline layout covers set 1/set 3 bindings
// 0 and 1) and the rendered frame must match the input: the shader scales
// by SourceSize.z * SourceSize.x == 1/w * w == 1 exactly at width 64.
//
// Skips cleanly (exit 0) when no Vulkan device is present — CI images
// provide lavapipe; Windows CI has no ICD.

#include <cmath>
#include <cstdint>
#include <cstdio>
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

// Same deterministic gradient as the M1/M2 canaries.
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
        options.appName = "libslang-vk-pushblock-test";
        options.validationLayers = true;
        auto device = slv::Device::create(options);
        if (!device) {
                std::printf("SKIP runner_pushblock: no Vulkan device (%s)\n", device.error().message.c_str());
                return 0;
        }
        std::printf("pushblock device: %s\n", device->properties().deviceName);

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

        auto preset = slv::Preset::load(*device, dataDir + "/runner/pushfrag.slangp");
        Check(preset.has_value(), "pushfrag preset loads");
        if (preset) {
                // The regression's exact failure point: Chain::create builds
                // the pass pipeline at load time.
                slv::ChainOptions chainOptions;
                chainOptions.outputWidth = kWidth;
                chainOptions.outputHeight = kHeight;
                auto chain = slv::Chain::create(*device, *preset, chainOptions);
                Check(chain.has_value(), "pushfrag chain creates (pipeline layout covers push binding)");
                if (chain) {
                        slv::InputFrame frame;
                        frame.view = input->view();
                        frame.format = VK_FORMAT_R8G8B8A8_UNORM;
                        frame.width = kWidth;
                        frame.height = kHeight;
                        slv::Offscreen target;
                        target.view = output->view();
                        target.format = VK_FORMAT_R8G8B8A8_UNORM;
                        target.width = kWidth;
                        target.height = kHeight;
                        const auto rendered = chain->render(frame, target, slv::FrameInfo{});
                        Check(rendered.has_value(), "pushfrag renders headless");
                        if (rendered) {
                                auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM, kWidth,
                                    kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                Check(pixels.has_value(), "pushfrag readback");
                                if (pixels) {
                                        // SourceSize.z * SourceSize.x == (1/64)*64 == 1.0 exactly.
                                        size_t bad = 0;
                                        size_t firstBad = 0;
                                        for (size_t i = 0; i < pattern.size(); ++i) {
                                                const int got = static_cast<int>((*pixels)[i]);
                                                if (std::abs(got - static_cast<int>(pattern[i])) > 1) {
                                                        if (bad == 0)
                                                                firstBad = i;
                                                        ++bad;
                                                }
                                        }
                                        if (bad != 0) {
                                                std::printf("FAIL pushfrag golden (%zu diffs, first byte %zu: got %d)\n",
                                                    bad, firstBad, static_cast<int>((*pixels)[firstBad]));
                                                ++failures;
                                        } else {
                                                std::printf("PASS pushfrag golden matches input\n");
                                        }
                                }
                        }
                }
        }

        const std::vector<std::string> validationErrors = device->validationErrors();
        if (!validationErrors.empty()) {
                std::printf("FAIL validation reported %zu error(s):\n", validationErrors.size());
                for (size_t i = 0; i < validationErrors.size() && i < 5; ++i)
                        std::printf("  [validation] %s\n", validationErrors[i].c_str());
                ++failures;
        } else {
                std::printf("PASS validation layers clean\n");
        }

        std::printf("%s\n", failures == 0 ? "runner_pushblock: all checks passed" : "runner_pushblock: FAILURES");
        return failures == 0 ? 0 : 1;
}

#endif // SLANGVK_ENABLE_VK
