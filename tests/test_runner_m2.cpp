// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk M2 unit test — full chain semantics on the Vulkan runner
// (plan §11 M2 exit criteria): feedback double-buffering, history rings,
// per-pass FBO formats, mipmap refill, include/cycle presets, FBO
// recreation on resize and live parameters. Uses the fork's field-preset
// canaries (tests/data/field-presets/test) plus a dedicated mip fixture.
//
// Skips cleanly (exit 0) when no Vulkan device is present — CI images
// provide lavapipe (mesa-vulkan-drivers); Windows CI has no ICD.
// Validation layers run whenever VK_LAYER_KHRONOS_validation is available;
// any ERROR-severity validation message fails the test.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
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

// Per-channel tolerance (float pipelines / accumulated rounding).
void CheckBytesTolerant(const std::vector<uint8_t> &got, const std::vector<uint8_t> &want, int tol,
    const std::string &label)
{
        if (got.size() != want.size()) {
                std::printf("FAIL %s (size %zu != %zu)\n", label.c_str(), got.size(), want.size());
                ++failures;
                return;
        }
        size_t bad = 0;
        size_t firstBad = 0;
        int worst = 0;
        for (size_t i = 0; i < got.size(); ++i) {
                const int d = std::abs(static_cast<int>(got[i]) - static_cast<int>(want[i]));
                if (d > tol) {
                        if (bad == 0)
                                firstBad = i;
                        ++bad;
                        worst = std::max(worst, d);
                }
        }
        if (bad != 0) {
                std::printf("FAIL %s (%zu bytes over +%d, worst %d, first byte %zu: got %u want %u)\n", label.c_str(),
                    bad, tol, worst, firstBad, got[firstBad], want[firstBad]);
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

// Channels rotated: distinct from MakePattern everywhere (history taps).
std::vector<uint8_t> MakePatternRotated(uint32_t width, uint32_t height)
{
        std::vector<uint8_t> pixels(width * height * 4);
        for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x) {
                        const size_t o = (static_cast<size_t>(y) * width + x) * 4;
                        pixels[o + 0] = static_cast<uint8_t>((y * 4) & 0xFF);
                        pixels[o + 1] = static_cast<uint8_t>((x + y) & 0xFF);
                        pixels[o + 2] = static_cast<uint8_t>((x * 4) & 0xFF);
                        pixels[o + 3] = 0xFF;
                }
        return pixels;
}

struct Fixture
{
        slv::Device *device = nullptr;
        std::optional<slv::Image> input;
        std::optional<slv::Image> output;

        Fixture(slv::Device &dev, const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height) : device(&dev)
        {
                slv::Image::CreateInfo in;
                in.desc = {width, height, VK_FORMAT_R8G8B8A8_UNORM};
                in.pixels = pixels.data();
                in.pixelBytes = pixels.size();
                if (auto made = slv::Image::create(dev, in))
                        input = std::move(*made);
                slv::Image::CreateInfo out;
                out.desc = {width, height, VK_FORMAT_R8G8B8A8_UNORM};
                if (auto madeOut = slv::Image::create(dev, out))
                        output = std::move(*madeOut);
        }

        slv::InputFrame Frame() const
        {
                slv::InputFrame f;
                f.view = input->view();
                f.image = input->image();
                f.format = VK_FORMAT_R8G8B8A8_UNORM;
                f.width = input->desc().width;
                f.height = input->desc().height;
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
        slv::DeviceOptions options;
        options.appName = "libslang-vk-m2-test";
        options.validationLayers = true;
        auto device = slv::Device::create(options);
        if (!device) {
                std::printf("SKIP runner_m2: no Vulkan device (%s)\n", device.error().message.c_str());
                return 0;
        }
        std::printf("m2 device: %s\n", device->properties().deviceName);

        const std::string dataDir = SLANGVK_TESTS_DATA_DIR;
        const std::string fieldDir = dataDir + "/field-presets/test";

        // Pin the shader root above BOTH fixture dirs: Preset::load otherwise
        // adopts the first preset's directory as root, and the SEC-01 sandbox
        // then rejects the other tree's presets as outside the root.
        slv::preset::SetShaderRootDir(dataDir);

        const std::vector<uint8_t> pattern = MakePattern(kWidth, kHeight);
        const std::vector<uint8_t> rotated = MakePatternRotated(kWidth, kHeight);
        Fixture fx64(*device, pattern, kWidth, kHeight);
        if (!fx64.input || !fx64.output) {
                std::printf("FAIL m2 fixtures (input/output image creation)\n");
                return 1;
        }

        // --- Negative control: PassOutput0 self-read must fail the LOAD ------
        {
                auto preset = slv::Preset::load(*device, fieldDir + "/feedback-noncausal.slangp");
                Check(!preset.has_value(), "feedback-noncausal preset is rejected at load");
                if (preset)
                        std::printf("     (unexpected success)\n");
                else
                        std::printf("     reason: %s\n", preset.error().message.c_str());
        }

        // --- Feedback canary: ping-pong mixes previous-frame output ----------
        // out(T) = mix(src, out(T-1), 0.5); out(0) = black (cleared buffer).
        {
                auto preset = slv::Preset::load(*device, fieldDir + "/feedback.slangp");
                Check(preset.has_value(), "feedback preset loads");
                if (preset) {
                        slv::ChainOptions co;
                        co.outputWidth = kWidth;
                        co.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, co);
                        Check(chain.has_value(), "feedback chain creates");
                        if (chain) {
                                std::vector<uint8_t> want(pattern.size());
                                // Frame 1: mix(src, black, 0.5) = src * 0.5; the shader
                                // writes alpha = 1.0 explicitly (cleared feedback is
                                // opaque black, not zero-alpha).
                                for (size_t i = 0; i < pattern.size(); ++i)
                                        want[i] = (i % 4 == 3) ? 255
                                            : static_cast<uint8_t>(static_cast<float>(pattern[i]) * 0.5F + 0.5F);
                                auto r1 = chain->render(fx64.Frame(), fx64.Target(), slv::FrameInfo{});
                                Check(r1.has_value(), "feedback frame 1 renders");
                                if (r1) {
                                        auto px = fx64.Readback();
                                        Check(px.has_value(), "feedback frame 1 readback");
                                        if (px)
                                                CheckBytesTolerant(*px, want, 1, "feedback frame 1 = src*0.5");
                                }
                                // Frame 2: mix(src, src*0.5, 0.5) = src*0.75 — proves the
                                // reader bound frame 1's buffer (a broken ping-pong that
                                // reads the write buffer still sees black here).
                                for (size_t i = 0; i < pattern.size(); ++i)
                                        want[i] = (i % 4 == 3) ? 255
                                            : static_cast<uint8_t>(static_cast<float>(pattern[i]) * 0.75F + 0.5F);
                                auto r2 = chain->render(fx64.Frame(), fx64.Target(), slv::FrameInfo{});
                                Check(r2.has_value(), "feedback frame 2 renders");
                                if (r2) {
                                        auto px = fx64.Readback();
                                        Check(px.has_value(), "feedback frame 2 readback");
                                        if (px)
                                                CheckBytesTolerant(*px, want, 2, "feedback frame 2 = src*0.75");
                                }
                        }
                }
        }

        // --- History canary: gap taps OriginalHistory1 + OriginalHistory3 ----
        // Ring depth 4 (max index 3); frames alternate pattern/rotated input.
        {
                auto preset = slv::Preset::load(*device, fieldDir + "/history.slangp");
                Check(preset.has_value(), "history preset loads");
                if (preset) {
                        slv::ChainOptions co;
                        co.outputWidth = kWidth;
                        co.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, co);
                        Check(chain.has_value(), "history chain creates");
                        if (chain) {
                                // Frame 1 (A): all slots seeded with A -> (A+A+A)/3 = A.
                                auto r1 = chain->render(fx64.Frame(), fx64.Target(), slv::FrameInfo{});
                                Check(r1.has_value(), "history frame 1 renders");
                                if (r1) {
                                        auto px = fx64.Readback();
                                        Check(px.has_value(), "history frame 1 readback");
                                        if (px)
                                                CheckBytesTolerant(*px, pattern, 1, "history frame 1 seeded = src");
                                }

                                // Frame 2 (B): h1 = A, h3 = A (warmup) -> (B+2A)/3.
                                Fixture fxB(*device, rotated, kWidth, kHeight);
                                std::vector<uint8_t> want2(pattern.size());
                                for (size_t i = 0; i < pattern.size(); ++i)
                                        want2[i] = static_cast<uint8_t>(
                                            (static_cast<float>(rotated[i]) + 2.0F * static_cast<float>(pattern[i]))
                                                / 3.0F
                                            + 0.5F);
                                auto r2 = chain->render(fxB.Frame(), fxB.Target(), slv::FrameInfo{});
                                Check(r2.has_value(), "history frame 2 renders");
                                if (r2) {
                                        auto px = fxB.Readback();
                                        Check(px.has_value(), "history frame 2 readback");
                                        if (px)
                                                CheckBytesTolerant(*px, want2, 2, "history frame 2 = (B+2A)/3");
                                }

                                // Frame 3 (B): h1 = B (frame 2), h3 = A (seed) — unchecked
                                // warmup frame; keeps the pattern/rotated alternation.
                                auto r3 = chain->render(fxB.Frame(), fxB.Target(), slv::FrameInfo{});
                                Check(r3.has_value(), "history frame 3 renders");

                                // Frame 4 (B): h1 = B (frame 3), h3 = A (frame 1) ->
                                // (2B+A)/3. Distinct from frame 2's (B+2A)/3, so this
                                // pins the T-N ring direction (a stale index binds
                                // frame 2/3 content and fails here).
                                std::vector<uint8_t> want4(pattern.size());
                                for (size_t i = 0; i < pattern.size(); ++i)
                                        want4[i] = static_cast<uint8_t>(
                                            (2.0F * static_cast<float>(rotated[i]) + static_cast<float>(pattern[i]))
                                                / 3.0F
                                            + 0.5F);
                                auto r4 = chain->render(fxB.Frame(), fxB.Target(), slv::FrameInfo{});
                                Check(r4.has_value(), "history frame 4 renders");
                                if (r4) {
                                        auto px = fxB.Readback();
                                        Check(px.has_value(), "history frame 4 readback");
                                        if (px)
                                                CheckBytesTolerant(*px, want4, 2, "history frame 4 = (2B+A)/3");
                                }
                        }
                }
        }

        // --- Format canary: 3-pass chain through default/float/srgb targets -
        // Pass 1 renders into a real R16_SFLOAT FBO (#pragma format, Vulkan
        // spelling; retroarch glslang_find_format accepts the same token).
        // A single-channel target stores R only — sampling it yields G=B=0,
        // A=1 (Vulkan "missing components" rule) — so the identity shaders
        // cannot preserve the input through pass 1: the golden is
        // (R, 0, 0, 255), R byte-exact via the UNORM->fp16->UNORM roundtrip
        // (8-bit/255 values survive fp16 to well under the tolerance-1).
        // The final pass writes the host target directly; its SRGB pragma is
        // a no-op there (host format wins). Before the Field round 4 format
        // table (crt-beans-s-video) the R16_SFLOAT token silently fell back
        // to RGBA8, which made this golden accidentally identical to input.
        {
                auto preset = slv::Preset::load(*device, fieldDir + "/format.slangp");
                Check(preset.has_value(), "format preset loads");
                if (preset) {
                        Check(preset->passCount() == 3, "format preset has 3 passes");
                        slv::ChainOptions co;
                        co.outputWidth = kWidth;
                        co.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, co);
                        Check(chain.has_value(), "format chain creates");
                        if (chain) {
                                auto r = chain->render(fx64.Frame(), fx64.Target(), slv::FrameInfo{});
                                Check(r.has_value(), "format chain renders (float FBO path)");
                                if (r) {
                                        auto px = fx64.Readback();
                                        Check(px.has_value(), "format readback");
                                        if (px) {
                                                std::vector<uint8_t> wantR16(pattern.size());
                                                for (size_t i = 0; i < wantR16.size(); i += 4) {
                                                        wantR16[i + 0] = pattern[i + 0];
                                                        wantR16[i + 1] = 0; // R16F: G not stored
                                                        wantR16[i + 2] = 0; // R16F: B not stored
                                                        wantR16[i + 3] = 0xFF; // missing alpha reads 1
                                                }
                                                CheckBytesTolerant(*px, wantR16, 1,
                                                    "format golden = (R,0,0,255) via R16F FBO");
                                        }
                                }
                        }
                }
        }

        // --- Include/cycle canary: r4 include set renders through the runner
        {
                auto preset = slv::Preset::load(*device, fieldDir + "/r4-include.slangp");
                Check(preset.has_value(), "r4-include preset loads (cross-stage + cycle)");
                if (preset) {
                        slv::ChainOptions co;
                        co.outputWidth = kWidth;
                        co.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, co);
                        Check(chain.has_value(), "r4-include chain creates");
                        if (chain) {
                                auto r = chain->render(fx64.Frame(), fx64.Target(), slv::FrameInfo{});
                                Check(r.has_value(), "r4-include renders");
                                if (r) {
                                        auto px = fx64.Readback();
                                        Check(px.has_value(), "r4-include readback");
                                        if (px) {
                                                // r4Tint: out = in * 0.5 + 0.25 (per channel, a passthrough).
                                                std::vector<uint8_t> want(pattern.size());
                                                for (size_t i = 0; i < pattern.size(); ++i) {
                                                        if (i % 4 == 3)
                                                                want[i] = pattern[i];
                                                        else
                                                                want[i] = static_cast<uint8_t>(
                                                                    static_cast<float>(pattern[i]) * 0.5F + 0.25F * 255.0F
                                                                    + 0.5F);
                                                }
                                                CheckBytesTolerant(*px, want, 1, "r4-include golden = tint(src)");
                                        }
                                }
                        }
                }
        }

        // --- Mipmap refill (F1): textureLod 6.0 on 64x64 = box average --------
        {
                auto preset = slv::Preset::load(*device, dataDir + "/runner/mip.slangp");
                Check(preset.has_value(), "mip preset loads");
                if (preset) {
                        slv::ChainOptions co;
                        co.outputWidth = kWidth;
                        co.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, co);
                        Check(chain.has_value(), "mip chain creates");
                        if (chain) {
                                auto r = chain->render(fx64.Frame(), fx64.Target(), slv::FrameInfo{});
                                Check(r.has_value(), "mip chain renders (input copy + chained outputs)");
                                if (r) {
                                        auto px = fx64.Readback();
                                        Check(px.has_value(), "mip readback");
                                        if (px) {
                                                // r = avg(4x) = 126, g = avg(4y) = 126, b = avg(x+y) = 63.
                                                std::vector<uint8_t> want(pattern.size());
                                                for (size_t i = 0; i < want.size(); i += 4) {
                                                        want[i + 0] = 126;
                                                        want[i + 1] = 126;
                                                        want[i + 2] = 63;
                                                        want[i + 3] = 255;
                                                }
                                                CheckBytesTolerant(*px, want, 2, "mip golden = box average");
                                        }
                                }
                        }
                }
        }

        // --- Resize: FBO + history-ring recreation on input-size change ------
        {
                auto preset = slv::Preset::load(*device, fieldDir + "/history.slangp");
                Check(preset.has_value(), "resize: history preset loads");
                if (preset) {
                        slv::ChainOptions co;
                        co.outputWidth = kWidth;
                        co.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, co);
                        Check(chain.has_value(), "resize: history chain creates");
                        if (chain) {
                                // Warm the ring at 64x64, then feed a 32x32 frame:
                                // the ring re-seeds, so the output is the new input.
                                auto r1 = chain->render(fx64.Frame(), fx64.Target(), slv::FrameInfo{});
                                Check(r1.has_value(), "resize: 64px frame renders");
                                Fixture fx32(*device, MakePatternRotated(32, 32), 32, 32);
                                if (!fx32.input || !fx32.output) {
                                        std::printf("FAIL resize 32px fixture\n");
                                        ++failures;
                                } else {
                                        auto r2 = chain->resize(32, 32);
                                        Check(r2.has_value(), "resize: chain.resize accepts new output size");
                                        auto r3 = chain->render(fx32.Frame(), fx32.Target(), slv::FrameInfo{});
                                        Check(r3.has_value(), "resize: 32px frame renders (FBO + ring recreated)");
                                        if (r3) {
                                                auto px = fx32.Readback();
                                                Check(px.has_value(), "resize: 32px readback");
                                                if (px) {
                                                        auto want = MakePatternRotated(32, 32);
                                                        CheckBytesTolerant(*px, want, 1, "resize: re-seeded ring = src");
                                                }
                                        }
                                }
                        }
                }
        }

        // --- Twopass still golden after the sampler-policy fix ---------------
        {
                auto preset = slv::Preset::load(*device, dataDir + "/runner/twopass.slangp");
                Check(preset.has_value(), "twopass preset loads");
                if (preset) {
                        slv::ChainOptions co;
                        co.outputWidth = kWidth;
                        co.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, co);
                        Check(chain.has_value(), "twopass chain creates");
                        if (chain) {
                                auto r = chain->render(fx64.Frame(), fx64.Target(), slv::FrameInfo{});
                                Check(r.has_value(), "twopass renders");
                                if (r) {
                                        auto px = fx64.Readback();
                                        Check(px.has_value(), "twopass readback");
                                        if (px)
                                                CheckBytes(*px, pattern, "twopass golden matches input");
                                }
                        }
                }
        }

        // --- Live parameters: setParameter() applies on the next render ------
        {
                auto preset = slv::Preset::load(*device, dataDir + "/runner/param.slangp");
                Check(preset.has_value(), "param preset loads");
                if (preset) {
                        slv::ChainOptions co;
                        co.outputWidth = kWidth;
                        co.outputHeight = kHeight;
                        auto chain = slv::Chain::create(*device, *preset, co);
                        Check(chain.has_value(), "param chain creates");
                        if (chain) {
                                auto setGain = [&](float v) { return chain->setParameter("GAIN", v); };
                                auto renderAndCheck = [&](float gain, const std::string &label) {
                                        auto r = chain->render(fx64.Frame(), fx64.Target(), slv::FrameInfo{});
                                        Check(r.has_value(), label + " renders");
                                        if (!r)
                                                return;
                                        auto px = fx64.Readback();
                                        Check(px.has_value(), label + " readback");
                                        if (!px)
                                                return;
                                        std::vector<uint8_t> want(pattern.size());
                                        for (size_t i = 0; i < pattern.size(); ++i)
                                                want[i] = static_cast<uint8_t>(
                                                    std::min(255.0F, static_cast<float>(pattern[i]) * gain) + 0.5F);
                                        CheckBytesTolerant(*px, want, 1, label);
                                };
                                renderAndCheck(2.0F, "param preset default GAIN=2");
                                auto s1 = setGain(1.0F);
                                Check(s1.has_value(), "setParameter(GAIN, 1.0) ok");
                                if (s1)
                                        renderAndCheck(1.0F, "param live GAIN=1");
                                auto s2 = chain->setParameter("gain", 3.0F);
                                Check(s2.has_value(), "setParameter(gain, 3.0) ok (case-insensitive)");
                                if (s2)
                                        renderAndCheck(3.0F, "param live gain=3");
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

        std::printf("%s\n", failures == 0 ? "runner_m2: all checks passed" : "runner_m2: FAILURES");
        return failures == 0 ? 0 : 1;
}

#endif // SLANGVK_ENABLE_VK
