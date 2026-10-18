// libslang-vk — corpus sweep (M3 conformance scaffolding, plan §11 M3).
//
// Walks one or more preset directories, loads every .slangp through the
// real library (parse -> translate -> glslang compile -> LUT upload) and
// optionally renders frames on the Vulkan device. Emits a per-preset
// ledger plus totals; the exit code follows --fail-on-broken and the
// --baseline regression gate.
//
// Usage:
//   corpus_sweep <root> [<root> ...] [--render] [--frames N]
//                [--lut] [--fail-on-broken] [--max-report K]
//                [--ledger FILE] [--baseline FILE] [--corpus-rev SHA]
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
//   --ledger FILE    write a machine-readable per-preset ledger (see below)
//   --baseline FILE  diff the run against a previous ledger: the no-
//                    regression gate (plan §10). Fails (exit 1) only on
//                    REGRESSIONS — presets that loaded in the baseline but
//                    fail now. Pre-existing broken presets, newly-added
//                    presets, removals and fixes are reported and tolerated.
//   --corpus-rev SHA record the swept corpus revision in the ledger header
//                    (the corpus.yml nightly passes `git rev-parse HEAD`)
//
// Machine ledger format (--ledger / --baseline): one '# '-prefixed header
// (tool, corpus revision, mode, UTC date, totals) then one TAB-separated
// line per preset:
//
//   LOAD\t<passes>\t<parameters>\t<relative/path.slangp>
//   FAIL\t0\t0\t<relative/path.slangp>
//
// Lines are sorted by path so consecutive ledgers diff cleanly. Error text
// stays on stdout only (it can embed tool-specific strings; the ledger
// carries statuses and counts). The regression gate compares statuses only.
//
// The shader root follows the first preset directory of the sweep and is
// retargeted per root (SEC-01 sandbox resolves pack-internal references).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
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

struct LedgerDiff {
        uint32_t regressions = 0;   // baseline LOAD -> now FAIL (the gate)
        uint32_t stillBroken = 0;   // FAIL -> FAIL (tolerated)
        uint32_t improved = 0;      // FAIL -> LOAD
        uint32_t added = 0;         // path not in baseline
        uint32_t addedBroken = 0;   // added and failing (review, not a regression)
        uint32_t gone = 0;          // baseline path absent now (corpus drift)
        std::vector<const SweepEntry *> regressionList;
};

void PrintUsage()
{
        std::printf("usage: corpus_sweep <root> [<root> ...] [--render] [--frames N] [--lut] "
                    "[--fail-on-broken] [--max-report K] [--ledger FILE] [--baseline FILE] "
                    "[--corpus-rev SHA] [--only-from FILE]\n");
}

/** @brief Loads a newline-separated whitelist of ledger-relative paths.
 *
 *  Enables chunked rescans: each invocation keeps <root> as the shader root
 *  (identical include-policy semantics to a full run) but only loads the
 *  presets whose relativePath appears in the list. Unknown paths and a
 *  missing file disable the filter, so a stale list degrades to a full
 *  sweep of the given roots instead of an empty ledger. */
std::unordered_set<std::string> LoadOnlyList(const char *path)
{
        std::unordered_set<std::string> only;
        if (path == nullptr)
                return only;
        FILE *file = std::fopen(path, "r");
        if (file == nullptr)
                return only;
        char line[4096];
        while (std::fgets(line, sizeof(line), file) != nullptr) {
                std::string entry(line);
                while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r'))
                        entry.pop_back();
                if (!entry.empty())
                        only.insert(entry);
        }
        std::fclose(file);
        return only;
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

// Baselines and fresh ledgers share one reader: '# ' lines are comments,
// everything else must be TAB-separated LOAD/FAIL records; malformed lines
// are skipped so a hand-edited or truncated baseline degrades to "not
// tracked" instead of failing the whole gate.
std::map<std::string, bool> ReadLedger(const char *path)
{
        std::map<std::string, bool> statuses;
        FILE *file = std::fopen(path, "r");
        if (file == nullptr)
                return statuses;
        char line[4096];
        while (std::fgets(line, sizeof(line), file) != nullptr) {
                std::string text(line);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                        text.pop_back();
                if (text.empty() || text.rfind("# ", 0) == 0)
                        continue;
                const size_t firstTab = text.find('\t');
                if (firstTab == std::string::npos)
                        continue;
                const std::string status = text.substr(0, firstTab);
                const size_t secondTab = text.find('\t', firstTab + 1);
                const size_t thirdTab = secondTab == std::string::npos
                    ? std::string::npos
                    : text.find('\t', secondTab + 1);
                if (thirdTab == std::string::npos)
                        continue;
                const std::string presetPath = text.substr(thirdTab + 1);
                if (presetPath.empty())
                        continue;
                statuses[presetPath] = (status == "LOAD");
        }
        std::fclose(file);
        return statuses;
}

LedgerDiff DiffLedgers(const std::vector<SweepEntry> &ledger, const std::map<std::string, bool> &baseline)
{
        LedgerDiff diff;
        std::map<std::string, bool> remaining(baseline);
        for (const SweepEntry &record : ledger) {
                const auto it = remaining.find(record.relativePath);
                if (it == remaining.end()) {
                        ++diff.added;
                        if (!record.loaded)
                                ++diff.addedBroken;
                        continue;
                }
                const bool wasLoaded = it->second;
                remaining.erase(it);
                if (wasLoaded && !record.loaded) {
                        ++diff.regressions;
                        diff.regressionList.push_back(&record);
                } else if (!wasLoaded && record.loaded) {
                        ++diff.improved;
                } else if (!wasLoaded && !record.loaded) {
                        ++diff.stillBroken;
                }
        }
        diff.gone = static_cast<uint32_t>(remaining.size());
        return diff;
}

bool WriteLedgerFile(const char *path, const std::vector<SweepEntry> &ledger, const std::string &corpusRev,
    bool render, uint32_t loaded, uint32_t failed)
{
        std::vector<const SweepEntry *> sorted;
        sorted.reserve(ledger.size());
        for (const SweepEntry &record : ledger)
                sorted.push_back(&record);
        std::sort(sorted.begin(), sorted.end(),
            [](const SweepEntry *a, const SweepEntry *b) { return a->relativePath < b->relativePath; });

        std::FILE *file = std::fopen(path, "w");
        if (file == nullptr)
                return false;
        std::time_t now = std::time(nullptr);
        char dateStamp[16];
        std::tm utc {};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::strftime(dateStamp, sizeof(dateStamp), "%Y-%m-%d", &utc);
        std::fprintf(file, "# libslang-vk corpus ledger (corpus_sweep)\n");
        std::fprintf(file, "# corpus revision: %s\n", corpusRev.empty() ? "unrecorded" : corpusRev.c_str());
        std::fprintf(file, "# mode: %s\n", render ? "render" : "compile-only (noLutImages)");
        std::fprintf(file, "# generated: %s\n", dateStamp);
        std::fprintf(file, "# totals: %zu presets, %u loaded, %u failed\n", ledger.size(), loaded, failed);
        for (const SweepEntry *record : sorted) {
                std::fprintf(file, "%s\t%u\t%u\t%s\n", record->loaded ? "LOAD" : "FAIL", record->passes,
                    record->parameters, record->relativePath.c_str());
        }
        std::fclose(file);
        return true;
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
        const char *ledgerPath = nullptr;
        const char *baselinePath = nullptr;
        const char *onlyFromPath = nullptr;
        std::string corpusRev;
        for (int i = 1; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--render")
                        render = true;
                else if (arg == "--lut")
                        lut = true;
                else if (arg == "--fail-on-broken")
                        failOnBroken = true;
                else if (arg == "--ledger" && i + 1 < argc)
                        ledgerPath = argv[++i];
                else if (arg == "--baseline" && i + 1 < argc)
                        baselinePath = argv[++i];
                else if (arg == "--corpus-rev" && i + 1 < argc)
                        corpusRev = argv[++i];
                else if (arg == "--only-from" && i + 1 < argc)
                        onlyFromPath = argv[++i];
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
        // Chunked-rescan whitelist (--only-from): empty set = no filter.
        const std::unordered_set<std::string> onlyPaths = LoadOnlyList(onlyFromPath);

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
                // Full-depth scan: the stock corpus keeps presets up to 7
                // levels deep (the browser's default cap of 3 covers 1253 of
                // 2658 — the ledger must see them all).
                const std::vector<slv::preset::PresetEntry> presets = slv::preset::ScanPresets(root, 12);
                std::printf("root %s: %zu presets\n", root.c_str(), presets.size());
                for (const slv::preset::PresetEntry &entry : presets) {
                        if (!onlyPaths.empty() && onlyPaths.find(entry.relativePath) == onlyPaths.end())
                                continue;
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

        // No-regression gate (plan §10): with a baseline, only baseline-ok ->
        // now-broken transitions fail the run; every other drift class is
        // reported for review and tolerated.
        int gate = 0;
        if (baselinePath != nullptr) {
                const std::map<std::string, bool> baseline = ReadLedger(baselinePath);
                if (baseline.empty()) {
                        std::printf("\nbaseline %s: EMPTY OR UNREADABLE — gate not evaluable\n", baselinePath);
                        gate = 1;
                } else {
                        const LedgerDiff diff = DiffLedgers(ledger, baseline);
                        std::printf("\nbaseline diff vs %s (%zu tracked): regressions %u, still-broken %u, "
                                    "improved %u, new %u (%u broken), gone %u\n",
                            baselinePath, baseline.size(), diff.regressions, diff.stillBroken, diff.improved,
                            diff.added, diff.addedBroken, diff.gone);
                        uint32_t shown = 0;
                        for (const SweepEntry *record : diff.regressionList) {
                                if (shown >= maxReport)
                                        break;
                                ++shown;
                                std::printf("  REGRESSION %s: %s\n", record->relativePath.c_str(),
                                    record->error.c_str());
                        }
                        if (diff.regressions > shown)
                                std::printf("  ... (%u more regressions)\n", diff.regressions - shown);
                        if (diff.regressions > 0) {
                                std::printf("no-regression gate: FAILED (%u preset(s) that loaded in the "
                                            "baseline now fail)\n",
                                    diff.regressions);
                                gate = 1;
                        } else {
                                std::printf("no-regression gate: PASSED\n");
                        }
                }
        }

        if (ledgerPath != nullptr && !WriteLedgerFile(ledgerPath, ledger, corpusRev, render, loaded, failed))
                std::printf("WARNING: could not write ledger %s\n", ledgerPath);

        const auto validation = device->validationErrors();
        if (!validation.empty()) {
                std::printf("VALIDATION ERRORS: %zu\n", validation.size());
                for (const std::string &message : validation)
                        std::printf("  %s\n", message.c_str());
                return 1;
        }
        if (gate != 0)
                return 1;
        if (failOnBroken && failed > 0)
                return 1;
        return 0;
}
