// libslang-vk — corpus sweep (M3 conformance scaffolding, plan §11 M3).
//
// Walks one or more preset directories, loads every .slangp through the
// real library (parse -> translate -> glslang compile -> LUT upload) and
// optionally renders frames on the Vulkan device. Emits a per-preset
// ledger plus totals; the exit code follows --fail-on-broken.
//
// Usage:
//   corpus_sweep <root> [<root> ...] [--render] [--frames N]
//                [--lut] [--fail-on-broken] [--max-report K]
//
//   --render         execute each chain on the Vulkan device after load
//                    (needs an ICD; presets whose final pass does not
//                    resolve to the output size render through the M4
//                    letterbox present path)
//   --frames N       frames per chain in --render mode (default 2: the
//                    second frame exercises feedback readers)
//   --lut            upload LUT images (default: compile-only, noLutImages)
//   --fail-on-broken exit 1 when any preset fails to load or render
//   --max-report K   cap the printed failure list (default 25)
//
// The shader root follows the first preset directory of the sweep and is
// retargeted per root (SEC-01 sandbox resolves pack-internal references).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <slangvk/slangvk.hpp>

namespace {

struct SweepEntry {
        std::string relativePath;
        bool loaded = false;
        bool rendered = false;   // --render mode only
        std::string error;
        uint32_t passes = 0;
        uint32_t parameters = 0;
};

void PrintUsage()
{
        std::printf("usage: corpus_sweep <root> [<root> ...] [--render] [--frames N] [--lut] "
                    "[--fail-on-broken] [--max-report K]\n");
}

std::vector<uint8_t> MakePattern(uint32_t width, uint32_t height)
{
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x) {
                        const size_t o = (size_t(y) * width + x) * 4;
                        pixels[o + 0] = uint8_t((x * 7) & 0xFF);
                        pixels[o + 1] = uint8_t((y * 11) & 0xFF);
                        pixels[o + 2] = uint8_t((x + y) & 0xFF);
                        pixels[o + 3] = 0xFF;
                }
        return pixels;
}

} // namespace

int main(int argc, char **argv)
{
        std::vector<std::string> roots;
        bool render = false;
        bool lut = false;
        bool failOnBroken = false;
        int frames = 2;
        int maxReport = 25;
        for (int i = 1; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--render")
                        render = true;
                else if (arg == "--lut")
                        lut = true;
                else if (arg == "--fail-on-broken")
                        failOnBroken = true;
                else if (arg == "--frames" && i + 1 < argc)
                        frames = std::atoi(argv[++i]);
                else if (arg == "--max-report" && i + 1 < argc)
                        maxReport = std::atoi(argv[++i]);
                else if (arg == "--help" || arg == "-h") {
                        PrintUsage();
                        return 0;
                } else if (!arg.empty() && arg[0] != '-')
                        roots.push_back(arg);
        }
        if (roots.empty()) {
                PrintUsage();
                return 2;
        }
        frames = std::max(1, frames);

        slv::DeviceOptions options;
        options.appName = "libslang-vk-corpus-sweep";
        options.validationLayers = true;
        auto device = slv::Device::create(options);
        if (!device) {
                std::printf("SKIP corpus_sweep: no Vulkan device (%s)\n", device.error().message.c_str());
                return 0;
        }
        std::printf("corpus_sweep device: %s\n", device->properties().deviceName);

        std::optional<slv::Image> inputImage;
        std::optional<slv::Image> outputImage;
        constexpr uint32_t kRenderWidth = 320;
        constexpr uint32_t kRenderHeight = 240;
        if (render) {
                const std::vector<uint8_t> pattern = MakePattern(kRenderWidth, kRenderHeight);
                slv::Image::CreateInfo in;
                in.desc = {kRenderWidth, kRenderHeight, VK_FORMAT_R8G8B8A8_UNORM};
                in.pixels = pattern.data();
                in.pixelBytes = pattern.size();
                auto input = slv::Image::create(*device, in);
                if (!input) {
                        std::printf("FAIL corpus_sweep: input image: %s\n", input.error().message.c_str());
                        return 1;
                }
                inputImage = std::move(*input);
                slv::Image::CreateInfo out;
                out.desc = {kRenderWidth, kRenderHeight, VK_FORMAT_R8G8B8A8_UNORM};
                auto output = slv::Image::create(*device, out);
                if (!output) {
                        std::printf("FAIL corpus_sweep: output image: %s\n", output.error().message.c_str());
                        return 1;
                }
                outputImage = std::move(*output);
        }

        std::vector<SweepEntry> ledger;
        for (const std::string &root : roots) {
                slv::preset::SetShaderRootDir(root);
                const std::vector<slv::preset::PresetEntry> presets = slv::preset::ScanPresets(root);
                std::printf("root %s: %zu presets\n", root.c_str(), presets.size());
                for (const slv::preset::PresetEntry &entry : presets) {
                        SweepEntry record;
                        record.relativePath = entry.relativePath;

                        slv::PresetLoadOptions loadOptions;
                        loadOptions.noLutImages = !lut;
                        auto preset = slv::Preset::load(*device, root + "/" + entry.relativePath, loadOptions);
                        if (!preset) {
                                record.error = preset.error().message;
                                ledger.push_back(std::move(record));
                                continue;
                        }
                        record.loaded = true;
                        record.passes = preset->passCount();
                        record.parameters = static_cast<uint32_t>(preset->parameterInfo().size());

                        if (render) {
                                slv::ChainOptions chainOptions;
                                chainOptions.outputWidth = kRenderWidth;
                                chainOptions.outputHeight = kRenderHeight;
                                auto chain = slv::Chain::create(*device, *preset, chainOptions);
                                if (!chain) {
                                        record.error = chain.error().message;
                                        ledger.push_back(std::move(record));
                                        continue;
                                }
                                slv::InputFrame frame;
                                frame.view = inputImage->view();
                                frame.image = inputImage->image();
                                frame.format = VK_FORMAT_R8G8B8A8_UNORM;
                                frame.width = kRenderWidth;
                                frame.height = kRenderHeight;
                                slv::Offscreen target;
                                target.view = outputImage->view();
                                target.image = outputImage->image();
                                target.format = VK_FORMAT_R8G8B8A8_UNORM;
                                target.width = kRenderWidth;
                                target.height = kRenderHeight;

                                bool ok = true;
                                for (int f = 0; f < frames && ok; ++f) {
                                        auto drawn = chain->render(frame, target, slv::FrameInfo{});
                                        if (!drawn) {
                                                ok = false;
                                                record.error = drawn.error().message;
                                        }
                                }
                                record.rendered = ok;
                        }
                        ledger.push_back(std::move(record));
                }
        }

        uint32_t loaded = 0;
        uint32_t failed = 0;
        uint32_t rendered = 0;
        for (const SweepEntry &record : ledger) {
                if (record.loaded)
                        ++loaded;
                else
                        ++failed;
                if (record.rendered)
                        ++rendered;
        }
        std::printf("\ncorpus_sweep: %zu presets — %u loaded, %u failed", ledger.size(), loaded, failed);
        if (render)
                std::printf(", %u rendered", rendered);
        std::printf("\n");

        uint32_t reported = 0;
        for (const SweepEntry &record : ledger) {
                if (reported >= maxReport)
                        break;
                if (record.loaded && (record.rendered || !render))
                        continue;
                ++reported;
                std::printf("  %s %s: %s\n", record.loaded ? "RENDER-FAIL" : "LOAD-FAIL",
                    record.relativePath.c_str(), record.error.c_str());
        }
        if (reported < ledger.size() && reported == maxReport)
                std::printf("  ... (%u more)\n", static_cast<uint32_t>(ledger.size()) - reported);

        const auto validation = device->validationErrors();
        if (!validation.empty()) {
                std::printf("VALIDATION ERRORS: %zu\n", validation.size());
                for (const std::string &message : validation)
                        std::printf("  %s\n", message.c_str());
                return 1;
        }
        if (failOnBroken && failed > 0)
                return 1;
        return 0;
}
