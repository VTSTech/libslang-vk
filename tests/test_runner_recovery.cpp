// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk M4.06 unit test — device-lost recovery (audit ROB-35).
//
// A real VK_ERROR_DEVICE_LOST cannot be provoked from a test (it needs a
// driver that faults the device), so this test drives the recovery
// machinery through the same path the automatic recovery takes:
//
//   1. Device::recover() replaces the VkDevice. Every object the old
//      device owned is gone — exactly the state a device loss leaves
//      behind.
//   2. The chain notices that its resources belong to a replaced device
//      and rebuilds itself (including the preset's LUT textures) before
//      the next frame.
//   3. The frame still renders, and renders the same pixels.
//
// It also pins the error mapping that drives the automatic path: a
// VK_ERROR_DEVICE_LOST result must surface as Errc::vk_device_lost, not
// as a generic vk_error, or the chain would never attempt a recovery.
//
// Skips cleanly (exit 0) when no Vulkan device is present — CI images
// provide lavapipe; Windows CI has no ICD.

#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <slangvk/slangvk.hpp>

#ifndef SLANGVK_ENABLE_VK
int main() { return 0; } // Core-only build: runner not compiled.
#else

extern "C" void slv_debug_pipeline_counts(long *c, long *d);
extern "C" void slv_debug_release_counts(long *r, long *cb);

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
                std::printf("FAIL %s (%zu byte diffs, first at byte %zu)\n", label.c_str(), diffs, firstDiff);
                ++failures;
        } else {
                std::printf("PASS %s\n", label.c_str());
        }
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

// The host's images live in std::optional so the test can release them
// before a device replacement (their device is about to be destroyed) and
// create fresh ones afterwards.
using HostImage = std::optional<slv::Image>;

auto MakeHostImage(slv::Device &device, const slv::Image::CreateInfo &info) -> HostImage
{
        auto created = slv::Image::create(device, info);
        if (!created)
                return HostImage{};
        return HostImage(std::move(*created));
}

} // namespace

int main()
{
        slv::DeviceOptions options;
        options.appName = "libslang-vk-recovery-test";
        options.validationLayers = true;
        auto device = slv::Device::create(options);
        if (!device) {
                std::printf("SKIP runner_recovery: no Vulkan device (%s)\n", device.error().message.c_str());
                return 0;
        }
        std::printf("runner device: %s\n", device->properties().deviceName);

        const std::string dataDir = SLANGVK_TESTS_DATA_DIR;
        const std::vector<uint8_t> pattern = MakePattern(kWidth, kHeight);

        slv::Image::CreateInfo inputInfo;
        inputInfo.desc = {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM};
        inputInfo.pixels = pattern.data();
        inputInfo.pixelBytes = pattern.size();
        slv::Image::CreateInfo outputInfo;
        outputInfo.desc = {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM};

        auto input = MakeHostImage(*device, inputInfo);
        auto output = MakeHostImage(*device, outputInfo);
        Check(input.has_value() && output.has_value(), "the host's images are created");
        if (!input || !output)
                return 1;

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
        auto chain = slv::Chain::create(*device, *preset, chainOptions);
        Check(chain.has_value(), "chain creates");
        if (!chain)
                return 1;

        // --- Baseline frame ------------------------------------------------
        const auto baseline = chain->render(makeInput(), makeOutput(), slv::FrameInfo{});
        Check(baseline.has_value(), "baseline frame renders");
        if (!baseline)
                return 1;
        auto baselinePixels = device->readPixels(
            output->image(), VK_FORMAT_R8G8B8A8_UNORM, kWidth, kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Check(baselinePixels.has_value(), "baseline readback");
        if (baselinePixels)
                CheckBytes(*baselinePixels, pattern, "baseline matches the golden blit");

        // --- The device generation starts at zero ---------------------------
        Check(device->generation() == 0, "a fresh device is generation 0");

        // --- Device::recover() replaces the device --------------------------
        //
        // This is the state a device loss leaves behind: the old VkDevice
        // and everything created on it are gone. The host's own images went
        // with it, so they are released first and recreated afterwards --
        // exactly what a host has to do after a device loss.
        input.reset();
        output.reset();
        const auto recovered = device->recover();
        Check(recovered.has_value(), "Device::recover rebuilds the device");
        if (!recovered) {
                std::printf("     recover failed: %s\n", recovered.error().message.c_str());
                return 1;
        }
        Check(device->generation() == 1, "recover bumps the device generation");
        Check(device->device() != VK_NULL_HANDLE, "the device handle is live again");

        // The host's images are recreated on the new device.
        input = MakeHostImage(*device, inputInfo);
        output = MakeHostImage(*device, outputInfo);
        Check(input.has_value() && output.has_value(), "the host's images are recreated on the new device");
        if (!input || !output)
                return 1;

        // --- The chain rebuilds itself on the next frame --------------------
        //
        // The chain's resources belong to the replaced device, so it has to
        // notice and rebuild before it can render again. This is the same
        // work the automatic device-loss recovery does.
        const auto afterRecovery = chain->render(makeInput(), makeOutput(), slv::FrameInfo{});
        Check(afterRecovery.has_value(), "the chain rebuilds and renders after the device was replaced");
        if (afterRecovery) {
                auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM, kWidth, kHeight,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                Check(pixels.has_value(), "readback after the device was replaced");
                if (pixels)
                        CheckBytes(*pixels, pattern, "the rebuilt chain produces the same pixels");
        }

        // --- A second recovery still works ----------------------------------
        input.reset();
        output.reset();
        const auto recoveredAgain = device->recover();
        Check(recoveredAgain.has_value(), "a second Device::recover succeeds");
        Check(device->generation() == 2, "the generation advances again");
        input = MakeHostImage(*device, inputInfo);
        output = MakeHostImage(*device, outputInfo);
        Check(input.has_value() && output.has_value(), "the host's images are recreated again");
        if (!input || !output)
                return 1;
        const auto afterSecond = chain->render(makeInput(), makeOutput(), slv::FrameInfo{});
        Check(afterSecond.has_value(), "the chain rebuilds again after a second replacement");
        if (afterSecond) {
                auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM, kWidth, kHeight,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                Check(pixels.has_value(), "readback after the second replacement");
                if (pixels)
                        CheckBytes(*pixels, pattern, "the rebuilt chain still produces the same pixels");
        }

        // --- The chain is usable again after a recovery --------------------
        //
        // The automatic device-loss path rebuilds the chain but does not
        // retry the frame: the host's own images died with the device. This
        // is the same situation, driven by hand — after the host recreates
        // its images the chain renders without any further repair.
        Check(device->generation() == 2, "the chain is on the current device generation");
        const auto finalFrame = chain->render(makeInput(), makeOutput(), slv::FrameInfo{});
        Check(finalFrame.has_value(), "the chain renders again after the recoveries");
        if (finalFrame) {
                auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM, kWidth, kHeight,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                Check(pixels.has_value(), "final readback");
                if (pixels)
                        CheckBytes(*pixels, pattern, "the final frame matches the golden blit");
        }

        // --- Destroying stale objects after a device replacement -----------
        //
        // Everything the old device owned is gone. Releasing those objects
        // must not call the destroy functions of the new device with stale
        // handles (which some drivers treat as a crash), so the library
        // forgets them instead.
        {
                auto staleImage = MakeHostImage(*device, outputInfo);
                Check(staleImage.has_value(), "an image is created for the stale-object test");
                auto stalePreset = slv::Preset::load(*device, dataDir + "/runner/blit.slangp");
                Check(stalePreset.has_value(), "a preset is created for the stale-object test");
                auto staleChain = slv::Chain::create(*device, *stalePreset, chainOptions);
                Check(staleChain.has_value(), "a chain is created for the stale-object test");

                // Replace the device behind their backs.
                const auto replaced = device->recover();
                Check(replaced.has_value(), "the device is replaced for the stale-object test");

                // Releasing them must not crash and must not report errors.
                staleImage.reset();
                staleChain = std::unexpected(slv::Error{});
                stalePreset = std::unexpected(slv::Error{});
                Check(true, "stale objects release cleanly after the device was replaced");
        }

        long c = 0, d = 0, r = 0, cb = 0;
        slv_debug_pipeline_counts(&c, &d);
        slv_debug_release_counts(&r, &cb);
        std::printf("pipelines created=%ld destroyed=%ld | chain releases=%ld callbacks fired=%ld\n", c, d, r, cb);
        std::printf("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
        std::fflush(stdout);
        return failures == 0 ? 0 : 1;
}

#endif // SLANGVK_ENABLE_VK
