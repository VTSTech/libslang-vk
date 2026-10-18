// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk M3 unit test — LUT texture upload on the Vulkan runner
// (plan §11 M3): decode + RGBA8 upload, binding by declared name (F9
// case-insensitive) and positionally via User# (SPEC 4.5 declaration
// order), <LutName>Size uniform fill, at-upload mip chains (SPEC 3.5),
// git-lfs pointer diagnostics and the noLutImages compile-only path.
//
// Goldens are byte-exact on lavapipe (nearest sampling, 1:1 scale) except
// the explicit-LOD mip test, which averages four texels (±1 tolerance).
//
// Skips cleanly (exit 0) when no Vulkan device is present — CI images
// provide lavapipe (mesa-vulkan-drivers); Windows CI has no ICD.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

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

// Per-channel tolerance (mip level blits average four texels).
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

void WriteText(const std::filesystem::path &path, const std::string &content)
{
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << content;
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

void RenderGolden(slv::Device &device, Fixture &fx, const std::string &presetPath,
    const std::vector<uint8_t> &want, int tol, const std::string &label)
{
        auto preset = slv::Preset::load(device, presetPath);
        Check(preset.has_value(), label + ": preset loads");
        if (!preset)
                return;
        slv::ChainOptions co;
        co.outputWidth = fx.Target().width;
        co.outputHeight = fx.Target().height;
        auto chain = slv::Chain::create(device, *preset, co);
        Check(chain.has_value(), label + ": chain creates");
        if (!chain)
                return;
        auto rendered = chain->render(fx.Frame(), fx.Target(), slv::FrameInfo{});
        Check(rendered.has_value(), label + ": renders");
        if (!rendered)
                return;
        auto px = fx.Readback();
        Check(px.has_value(), label + ": readback");
        if (!px)
                return;
        if (tol == 0)
                CheckBytes(*px, want, label);
        else
                CheckBytesTolerant(*px, want, tol, label);
}

} // namespace

int main()
{
        slv::DeviceOptions options;
        options.appName = "libslang-vk-m3-test";
        options.validationLayers = true;
        auto device = slv::Device::create(options);
        if (!device) {
                std::printf("SKIP runner_m3: no Vulkan device (%s)\n", device.error().message.c_str());
                return 0;
        }
        std::printf("m3 device: %s\n", device->properties().deviceName);

        const std::string dataDir = SLANGVK_TESTS_DATA_DIR;
        const std::string lutDir = dataDir + "/runner3";
        slv::preset::SetShaderRootDir(dataDir);

        const std::vector<uint8_t> pattern = MakePattern(2, 2);
        Fixture fx(*device, pattern, 2, 2);
        if (!fx.input || !fx.output) {
                std::printf("FAIL m3 fixtures (input/output image creation)\n");
                return 1;
        }

        // tint.png: red / green / blue / yellow (row-major 2x2 RGBA8).
        const std::vector<uint8_t> tintGolden = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255};

        // --- LUT by declared name (nearest, 1:1): output == tint.png ---------
        RenderGolden(*device, fx, lutDir + "/lut.slangp", tintGolden, 0, "lut: named LUT 1:1");

        // --- User0 = FIRST declared texture ("zeta"), not the alphabetical ---
        // first ("alpha" -> avg.png would render near-black).
        RenderGolden(*device, fx, lutDir + "/lut_user.slangp", tintGolden, 0, "lut: User0 declaration order");

        // --- <LutName>Size fill: tint4 is 4x4 -> vec4(4, 4, 0.25, 0.25) ------
        // Encoded through the UNORM store: (4, 4, 64, 64) per texel.
        {
                std::vector<uint8_t> want(2 * 2 * 4);
                for (size_t i = 0; i < want.size(); i += 4) {
                        want[i + 0] = 4;
                        want[i + 1] = 4;
                        want[i + 2] = 64;
                        want[i + 3] = 64;
                }
                RenderGolden(*device, fx, lutDir + "/lut_size.slangp", want, 1, "lut: <tint4>Size uniform");
        }

        // --- Mipmap LUT: explicit LOD 1 of avg.png = 1x1 average --------------
        // (63.75, 0, 0, 255) -> stores as (64, 0, 0, 255); +-1 for blit rounding.
        {
                std::vector<uint8_t> want(2 * 2 * 4);
                for (size_t i = 0; i < want.size(); i += 4) {
                        want[i + 0] = 64;
                        want[i + 1] = 0;
                        want[i + 2] = 0;
                        want[i + 3] = 255;
                }
                RenderGolden(*device, fx, lutDir + "/lut_mip.slangp", want, 1, "lut: mipmap LOD 1 average");
        }

        // --- noLutImages: compile-only load; bind fails with a named error ----
        {
                slv::PresetLoadOptions opts;
                opts.noLutImages = true;
                auto preset = slv::Preset::load(*device, lutDir + "/lut.slangp", opts);
                Check(preset.has_value(), "noLutImages: preset loads compile-only");
                if (preset) {
                        slv::ChainOptions co;
                        co.outputWidth = 2;
                        co.outputHeight = 2;
                        auto chain = slv::Chain::create(*device, *preset, co);
                        Check(chain.has_value(), "noLutImages: chain creates");
                        if (chain) {
                                auto rendered = chain->render(fx.Frame(), fx.Target(), slv::FrameInfo{});
                                Check(!rendered.has_value(), "noLutImages: render fails explicitly");
                                if (rendered) {
                                        std::printf("     (unexpected success)\n");
                                } else {
                                        std::printf("     reason: %s\n", rendered.error().message.c_str());
                                        Check(rendered.error().message.find("noLutImages") != std::string::npos,
                                            "noLutImages: error names the load option");
                                }
                        }
                }
        }

        // --- Negative controls in a throwaway root (SEC-01 sandbox) ----------
        {
                const std::filesystem::path temp = std::filesystem::temp_directory_path() / "libslang_vk_test_m3";
                std::error_code ec;
                std::filesystem::remove_all(temp, ec);
                slv::preset::SetShaderRootDir(temp.string());

                WriteText(temp / "shaders" / "lut.slang",
                    "#pragma name p\n#pragma stage vertex\nvoid main() {}\n#pragma stage fragment\nvoid main() {}\n");
                WriteText(temp / "missing.slangp",
                    "textures = \"t\"\nt = \"nope.png\"\nshader0 = shaders/lut.slang\nscale_type0 = source\n");
                WriteText(temp / "pointer.slangp",
                    "textures = \"t\"\nt = \"pointer.png\"\nshader0 = shaders/lut.slang\nscale_type0 = source\n");
                WriteText(temp / "pointer.png",
                    "version https://git-lfs.github.com/spec/v1\noid sha256:0123\nsize 7\n");

                auto missing = slv::Preset::load(*device, temp / "missing.slangp");
                Check(!missing.has_value(), "missing LUT file fails the load");
                if (missing) {
                        std::printf("     (unexpected success)\n");
                } else {
                        std::printf("     reason: %s\n", missing.error().message.c_str());
                        Check(missing.error().message.find("cannot open") != std::string::npos,
                            "missing LUT error names the open failure");
                }

                auto pointer = slv::Preset::load(*device, temp / "pointer.slangp");
                Check(!pointer.has_value(), "git-lfs POINTER LUT fails the load");
                if (pointer) {
                        std::printf("     (unexpected success)\n");
                } else {
                        std::printf("     reason: %s\n", pointer.error().message.c_str());
                        Check(pointer.error().message.find("POINTER") != std::string::npos,
                            "POINTER error carries the SPEC 3.5 diagnostic");
                }

                std::filesystem::remove_all(temp, ec);
                slv::preset::SetShaderRootDir(dataDir);
        }

        const auto validation = device->validationErrors();
        Check(validation.empty(), "zero validation-layer errors");
        for (const std::string &message : validation)
                std::printf("     validation: %s\n", message.c_str());

        std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
        return failures == 0 ? 0 : 1;
}

#endif // SLANGVK_ENABLE_VK
