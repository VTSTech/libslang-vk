// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk — conformance suite (M4.04, plan §11): golden-image rendering
// and performance budgets, the two M3 leftovers.
//
// Two modes, one binary:
//
//   conformance golden <root> [<root> ...] [--manifest FILE] [--golden-dir DIR]
//                      [--update] [--frames N] [--size WxH] [--no-lut]
//                      [--max-report K]
//
//     Renders every manifest preset through the real chain (parse ->
//     translate -> compile -> LUT upload -> N frames) into a fixed-size
//     offscreen target with a deterministic procedural testcard input, then
//     compares the final frame against the committed golden PNG. A pixel
//     passes when every channel differs by <= max-delta; the image passes
//     when the mismatched-pixel fraction is <= max-mismatch-pct. Any FAIL
//     (or a golden missing without --update) exits 1.
//
//   conformance perf <root> [<root> ...] [--warmup N] [--timed N]
//                    [--budgets FILE] [--report FILE] [--only-from FILE]
//                    [--size WxH] [--no-lut] [--fail-on-broken]
//                    [--max-report K]
//
//     Loads every preset found under the roots (ScanPresets depth 12,
//     --only-from whitelist to restrict) and times --timed frames after
//     --warmup warmup frames. Chain::render is synchronous (one fenced
//     submit per frame), so wall-clock around render() is the honest
//     per-frame cost: submit + execute + fence wait. --budgets gates the
//     mean (exit 1 on breach); --report writes a ledger-style TAB file
//     suitable for committing and diffing across driver/hardware changes.
//
// Both modes skip cleanly (exit 0) when no Vulkan device is available —
// the ctest convention shared with corpus_sweep and the runner tests.
//
// Manifest format (tests/golden/manifest.txt): '# ' lines are comments;
// each entry line is TAB-separated:
//
//   <preset-path> \t <golden-name> [\t <max-delta> \t <max-mismatch-pct>]
//
//   preset-path   relative to one of the sweep roots (first root that
//                 contains it wins; that root becomes the shader root for
//                 the SEC-01 include sandbox), or an absolute path.
//   golden-name   PNG file inside the golden dir ("images" subdirectory).
//                 Derived from the preset path when omitted.
//   max-delta     per-channel tolerance 0..255 (default 2: identical back-
//                 ends are byte-exact, the slack absorbs driver-drift LSBs).
//   max-pct       max % of mismatched pixels (default 0.5).
//
// The last manifest header line is "# generated-by: <device name>" — the
// backend that produced the committed goldens. A different local device
// prints a WARN once (cross-backend comparisons are expected to drift
// further than the default tolerance on real GPUs; regenerate with
// --update when adopting a new reference backend).
//
// Budgets file format: '# ' comments, then TAB-separated
//
//   <preset-path> \t <max-ms-per-frame>
//
// Presets measured but absent from the budgets file are informational
// only; budget lines whose preset was not measured are reported as
// UNMEASURED and tolerated (the corpus-ledger gate philosophy: only
// regressions against a recorded expectation fail the run).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <slangvk/slangvk.hpp>

// Private stb copies (STB_IMAGE_STATIC, the shader_demo pattern): the
// library's lut.cpp already compiles the shared stb_image implementation
// into libslangvk.a, so the tool must not re-export any stb symbol.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#ifndef SLANGVK_GOLDEN_DIR
#define SLANGVK_GOLDEN_DIR "tests/golden"
#endif

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kInputWidth = 320;
constexpr uint32_t kInputHeight = 240;
constexpr uint32_t kDefaultOutWidth = 640;
constexpr uint32_t kDefaultOutHeight = 480;
constexpr int kDefaultFrames = 3;      // golden: feedback warm-up frames, last is compared
constexpr int kDefaultWarmup = 3;      // perf
constexpr int kDefaultTimed = 20;      // perf
constexpr int kDefaultMaxDelta = 2;    // golden compare tolerance (per channel)
constexpr double kDefaultMaxPct = 0.5; // golden compare tolerance (% pixels)
constexpr int kDefaultMaxReport = 25;

// ---------------------------------------------------------------- utilities

/** @brief The deterministic conformance input: colour bars, gradients,
 *  checker + grid, a noise band and a center disc. Purely procedural (the
 *  suite needs no assets) and byte-stable across runs and platforms. */
std::vector<uint8_t> MakeTestcard(uint32_t width, uint32_t height)
{
        std::vector<uint8_t> pixels(size_t(width) * height * 4, 0);
        const uint32_t barCount = 8;
        const uint32_t barHeight = height / 5;
        static const uint8_t bars[barCount][3] = {
            {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
            {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0}};
        for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                        size_t o = (size_t(y) * width + x) * 4;
                        uint8_t r = 0, g = 0, b = 0;
                        if (y < barHeight) {
                                const uint32_t bar = x * barCount / width;
                                r = bars[bar][0];
                                g = bars[bar][1];
                                b = bars[bar][2];
                        } else if (y < barHeight * 2) {
                                r = uint8_t((x * 255) / (width - 1));
                                g = uint8_t(((y - barHeight) * 255) / (barHeight - 1));
                                b = uint8_t(255 - r);
                        } else if (y < barHeight * 4) {
                                const bool checker = ((x / 16) + (y / 16)) % 2 == 0;
                                if (x < width / 2) {
                                        r = g = b = checker ? 255 : 0;
                                } else {
                                        const bool grid = (x % 16 == 0) || (y % 16 == 0);
                                        r = g = b = grid ? 0 : 128;
                                }
                        } else if (y < barHeight + (height - barHeight * 4)) {
                                // deterministic hash noise band
                                const uint32_t h = (x * 73856093u) ^ (y * 19349663u);
                                r = uint8_t((h >> 8) & 0xFF);
                                g = uint8_t((h >> 16) & 0xFF);
                                b = uint8_t((h >> 24) & 0xFF);
                        } else {
                                // bottom strip: 25/50/75/100% grey patches
                                const uint32_t patch = x * 4 / width;
                                const uint8_t v = uint8_t(64 * (patch + 1));
                                r = g = b = v;
                        }
                        pixels[o + 0] = r;
                        pixels[o + 1] = g;
                        pixels[o + 2] = b;
                        pixels[o + 3] = 0xFF;
                }
        }
        // center disc: white fill, 2px black rim, radius height/8
        const float cx = width * 0.5F, cy = height * 0.5F;
        const float fillR = height / 8.0F, rimR = fillR + 2.0F;
        const int y0 = std::max(0, int(cy - rimR - 1)), y1 = std::min(int(height - 1), int(cy + rimR + 1));
        const int x0 = std::max(0, int(cx - rimR - 1)), x1 = std::min(int(width - 1), int(cx + rimR + 1));
        for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                        const float dx = x - cx, dy = y - cy;
                        const float d = std::sqrt(dx * dx + dy * dy);
                        if (d <= rimR) {
                                const size_t o = (size_t(y) * width + size_t(x)) * 4;
                                const uint8_t v = d <= fillR ? 255 : 0;
                                pixels[o + 0] = pixels[o + 1] = pixels[o + 2] = v;
                        }
                }
        return pixels;
}

inline bool WritePngLocal(const std::string &path, const std::vector<uint8_t> &rgba, uint32_t width, uint32_t height)
{
        return stbi_write_png(path.c_str(), int(width), int(height), 4, rgba.data(), int(width) * 4) != 0;
}

std::string NowDateStamp()
{
        std::time_t now = std::time(nullptr);
        char stamp[16];
        std::tm utc {};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d", &utc);
        return stamp;
}

/** @brief Manifest/budget golden-name fallback: "a/b/c.slangp" -> "a_b_c.png". */
std::string SanitizeName(const std::string &path)
{
        std::string out = path;
        for (char &c : out) {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '.' || c == '-' || c == '_';
                if (!ok)
                        c = '_';
        }
        return out + ".png";
}

std::vector<std::string> SplitTabs(const std::string &line)
{
        std::vector<std::string> fields;
        size_t start = 0;
        while (true) {
                const size_t tab = line.find('\t', start);
                if (tab == std::string::npos) {
                        fields.push_back(line.substr(start));
                        break;
                }
                fields.push_back(line.substr(start, tab - start));
                start = tab + 1;
        }
        return fields;
}

// ----------------------------------------------------------------- options

struct Options {
        std::string mode;                  // "golden" | "perf"
        std::vector<std::string> roots;
        std::string manifest;              // golden
        std::string goldenDir;             // golden (PNG images live in <goldenDir>/images)
        bool update = false;               // golden: (re)write goldens from this run
        int frames = kDefaultFrames;       // golden
        int warmup = kDefaultWarmup;       // perf
        int timed = kDefaultTimed;         // perf
        std::string budgetsPath;           // perf
        std::string reportPath;            // perf
        std::string onlyFromPath;          // perf
        bool failOnBroken = false;         // perf
        bool lut = true;                   // full-path parity: LUT images uploaded by default
        uint32_t outWidth = kDefaultOutWidth;
        uint32_t outHeight = kDefaultOutHeight;
        int maxReport = kDefaultMaxReport;
};

void PrintUsage()
{
        std::printf("usage:\n"
                    "  conformance golden <root> [...] [--manifest FILE] [--golden-dir DIR] [--update]\n"
                    "                     [--frames N] [--size WxH] [--no-lut] [--max-report K]\n"
                    "  conformance perf   <root> [...] [--warmup N] [--timed N] [--budgets FILE]\n"
                    "                     [--report FILE] [--only-from FILE] [--size WxH] [--no-lut]\n"
                    "                     [--fail-on-broken] [--max-report K]\n");
}

bool ParseSize(const std::string &text, uint32_t &w, uint32_t &h)
{
        const size_t x = text.find('x');
        if (x == std::string::npos || x == 0 || x + 1 >= text.size())
                return false;
        w = uint32_t(std::strtoul(text.c_str(), nullptr, 10));
        h = uint32_t(std::strtoul(text.c_str() + x + 1, nullptr, 10));
        return w > 0 && h > 0;
}

// ---------------------------------------------------------------- manifest

struct GoldenEntry {
        std::string path;     // manifest preset path (root-relative or absolute)
        std::string name;     // golden PNG file name (inside <goldenDir>/images)
        int maxDelta = kDefaultMaxDelta;
        double maxPct = kDefaultMaxPct;
        int lineNo = 0;
};

struct GoldenManifest {
        std::vector<GoldenEntry> entries;
        std::string generatedBy; // device name stamped in the header ("" when absent)
        size_t lines = 0;
};

GoldenManifest ReadManifest(const char *path)
{
        GoldenManifest manifest;
        FILE *file = std::fopen(path, "r");
        if (file == nullptr)
                return manifest;
        char line[4096];
        int lineNo = 0;
        while (std::fgets(line, sizeof(line), file) != nullptr) {
                ++lineNo;
                std::string text(line);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                        text.pop_back();
                if (text.empty() || text[0] == '#') { // any hash prefix comments
                        constexpr char kStamp[] = "# generated-by: ";
                        if (text.rfind(kStamp, 0) == 0)
                                manifest.generatedBy = text.substr(sizeof(kStamp) - 1);
                        continue;
                }
                const std::vector<std::string> fields = SplitTabs(text);
                if (fields.empty() || fields[0].empty())
                        continue;
                GoldenEntry entry;
                entry.path = fields[0];
                entry.name = fields.size() > 1 && !fields[1].empty() ? fields[1] : SanitizeName(fields[0]);
                if (fields.size() > 2 && !fields[2].empty())
                        entry.maxDelta = std::atoi(fields[2].c_str());
                if (fields.size() > 3 && !fields[3].empty())
                        entry.maxPct = std::atof(fields[3].c_str());
                entry.lineNo = lineNo;
                manifest.entries.push_back(std::move(entry));
        }
        std::fclose(file);
        manifest.lines = manifest.entries.size();
        return manifest;
}

/** @brief Rewrites the "# generated-by:" header line after --update. */
bool StampManifest(const char *path, const std::string &deviceName)
{
        FILE *in = std::fopen(path, "r");
        if (in == nullptr)
                return false;
        std::vector<std::string> lines;
        char line[4096];
        while (std::fgets(line, sizeof(line), in) != nullptr) {
                std::string text(line);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                        text.pop_back();
                lines.push_back(text);
        }
        std::fclose(in);
        const std::string stamp = "# generated-by: " + deviceName;
        // keep the stamp as the LAST header line: after the final '#' header
        // line, or at the top when the manifest carries no header.
        size_t insertAt = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
                if (!lines[i].empty() && lines[i][0] == '#') {
                        insertAt = i + 1;
                        continue;
                }
                if (!lines[i].empty())
                        break; // first data line reached
        }
        bool replaced = false;
        for (std::string &text : lines) {
                if (text.rfind("# generated-by: ", 0) == 0) {
                        text = stamp;
                        replaced = true;
                        break;
                }
        }
        if (!replaced)
                lines.insert(lines.begin() + std::min(insertAt, lines.size()), stamp);
        FILE *out = std::fopen(path, "w");
        if (out == nullptr)
                return false;
        for (const std::string &text : lines)
                std::fprintf(out, "%s\n", text.c_str());
        std::fclose(out);
        return true;
}

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
                if (!entry.empty() && entry[0] != '#')
                        only.insert(entry);
        }
        std::fclose(file);
        return only;
}

std::map<std::string, double> ReadBudgets(const char *path)
{
        std::map<std::string, double> budgets;
        FILE *file = std::fopen(path, "r");
        if (file == nullptr)
                return budgets;
        char line[4096];
        while (std::fgets(line, sizeof(line), file) != nullptr) {
                std::string text(line);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                        text.pop_back();
                if (text.empty() || text[0] == '#')
                        continue;
                const std::vector<std::string> fields = SplitTabs(text);
                if (fields.size() < 2 || fields[0].empty())
                        continue;
                budgets[fields[0]] = std::atof(fields[1].c_str());
        }
        std::fclose(file);
        return budgets;
}

// ----------------------------------------------------------------- compare

struct CompareStats {
        size_t total = 0;
        size_t mismatched = 0; // pixels with any channel |diff| > maxDelta
        int worst = 0;         // max |diff| over all channels
        double meanDelta = 0.0;
        [[nodiscard]] double mismatchPct() const { return total == 0 ? 0.0 : 100.0 * double(mismatched) / double(total); }
};

CompareStats CompareRgba(const std::vector<uint8_t> &got, const std::vector<uint8_t> &want, int maxDelta)
{
        CompareStats stats;
        const size_t count = std::min(got.size(), want.size()) / 4;
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
                bool pixelBad = false;
                for (int c = 0; c < 4; ++c) {
                        const int diff = std::abs(int(got[i * 4 + c]) - int(want[i * 4 + c]));
                        sum += double(diff);
                        stats.worst = std::max(stats.worst, diff);
                        if (diff > maxDelta)
                                pixelBad = true;
                }
                if (pixelBad)
                        ++stats.mismatched;
        }
        stats.total = count;
        if (count != 0)
                stats.meanDelta = sum / (double(count) * 4.0);
        return stats;
}

// ------------------------------------------------------------------ render

struct RenderResult {
        bool ok = false;
        std::string error;
        std::vector<uint8_t> pixels; // final frame RGBA8 (golden mode)
        uint32_t passes = 0;
        uint32_t parameters = 0;
        double meanMs = 0.0, minMs = 0.0, maxMs = 0.0;
};

/** @brief Loads the preset, builds the chain at the fixed target size and
 *  renders `frames` frames of the testcard. Golden mode reads the final
 *  frame back; perf mode times every frame (Chain::render fences per
 *  frame, so steady_clock around it captures the true per-frame cost). */
RenderResult RenderEntry(slv::Device &device, slv::Image &input, slv::Image &output, const std::string &presetPath,
    const Options &options, bool readback)
{
        RenderResult result;
        slv::PresetLoadOptions loadOptions;
        loadOptions.noLutImages = !options.lut;
        auto preset = slv::Preset::load(device, presetPath, loadOptions);
        if (!preset) {
                result.error = preset.error().message;
                return result;
        }
        result.passes = preset->passCount();
        result.parameters = static_cast<uint32_t>(preset->parameterInfo().size());

        slv::ChainOptions chainOptions;
        chainOptions.outputWidth = options.outWidth;
        chainOptions.outputHeight = options.outHeight;
        auto chain = slv::Chain::create(device, *preset, chainOptions);
        if (!chain) {
                result.error = chain.error().message;
                return result;
        }

        slv::InputFrame frame;
        frame.view = input.view();
        frame.image = input.image();
        frame.format = input.desc().format;
        frame.width = input.desc().width;
        frame.height = input.desc().height;
        slv::Offscreen target;
        target.view = output.view();
        target.image = output.image();
        target.format = output.desc().format;
        target.width = output.desc().width;
        target.height = output.desc().height;

        using Clock = std::chrono::steady_clock;
        double sum = 0.0;
        double minMs = 0.0, maxMs = 0.0;
        const int total = readback ? options.frames : options.warmup + options.timed;
        for (int f = 0; f < total; ++f) {
                const auto t0 = Clock::now();
                auto drawn = chain->render(frame, target, slv::FrameInfo{uint64_t(f)});
                if (!drawn) {
                        result.error = drawn.error().message;
                        return result;
                }
                const double ms = double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count())
                    / 1.0e6;
                if (readback) {
                        sum += ms; // reported but informational in golden mode
                } else if (f >= options.warmup) {
                        sum += ms;
                        minMs = f == options.warmup ? ms : std::min(minMs, ms);
                        maxMs = std::max(maxMs, ms);
                }
        }
        if (!readback && options.timed > 0) {
                result.meanMs = sum / double(options.timed);
                result.minMs = minMs;
                result.maxMs = maxMs;
        }
        (void)device.waitIdle();

        if (readback) {
                auto pixels = device.readPixels(output.image(), output.desc().format, output.desc().width,
                    output.desc().height, output.layout());
                if (!pixels) {
                        result.error = pixels.error().message;
                        return result;
                }
                result.pixels = std::move(*pixels);
        }
        result.ok = true;
        return result;
}

// ------------------------------------------------------------- golden mode

int RunGolden(slv::Device &device, const Options &options, slv::Image &input, slv::Image &output)
{
        const GoldenManifest manifest = ReadManifest(options.manifest.c_str());
        if (manifest.entries.empty()) {
                std::printf("FAIL conformance golden: manifest %s empty or unreadable\n", options.manifest.c_str());
                return 1;
        }

        const std::string deviceName = device.properties().deviceName;
        if (!manifest.generatedBy.empty() && manifest.generatedBy != deviceName)
                std::printf("WARN: goldens generated on \"%s\", running on \"%s\" — cross-backend drift beyond "
                            "the manifest tolerances is expected; regenerate with --update if adopting this "
                            "backend\n",
                    manifest.generatedBy.c_str(), deviceName.c_str());

        // Group entries by the shader root they resolve to: declared roots
        // first (first root containing the preset wins — that root drives the
        // SEC-01 include sandbox), then one group per absolute path's parent
        // directory. Grouping minimizes SetShaderRootDir churn.
        std::vector<std::pair<std::string, std::vector<size_t>>> groups;
        for (const std::string &root : options.roots)
                groups.push_back({root, {}});
        std::vector<size_t> unresolved;
        for (size_t i = 0; i < manifest.entries.size(); ++i) {
                const GoldenEntry &entry = manifest.entries[i];
                if (fs::path(entry.path).is_absolute()) {
                        const std::string parent = fs::path(entry.path).parent_path().string();
                        bool merged = false;
                        for (auto &group : groups) {
                                if (group.first == parent) {
                                        group.second.push_back(i);
                                        merged = true;
                                        break;
                                }
                        }
                        if (!merged)
                                groups.push_back({parent, {i}});
                        continue;
                }
                bool found = false;
                for (size_t r = 0; r < options.roots.size(); ++r) {
                        if (fs::exists(fs::path(options.roots[r]) / entry.path)) {
                                groups[r].second.push_back(i);
                                found = true;
                                break;
                        }
                }
                if (!found)
                        unresolved.push_back(i);
        }

        const std::string imagesDir = (fs::path(options.goldenDir) / "images").string();
        if (options.update)
                std::filesystem::create_directories(imagesDir);

        uint32_t passed = 0, failed = 0, generated = 0;
        std::vector<std::string> failures;

        for (const auto &[rootPath, indices] : groups) {
                if (indices.empty())
                        continue;
                slv::preset::SetShaderRootDir(rootPath);
                for (size_t i : indices) {
                        const GoldenEntry &entry = manifest.entries[i];
                        const std::string presetPath = fs::path(entry.path).is_absolute()
                            ? entry.path
                            : (fs::path(rootPath) / entry.path).string();
                        const std::string goldenPath = (fs::path(imagesDir) / entry.name).string();
                        const bool haveGolden = fs::exists(goldenPath);
                        if (!haveGolden && !options.update) {
                                ++failed;
                                failures.push_back("MISSING      " + entry.path + " (no golden " + entry.name
                                    + "; run --update to generate)");
                                continue;
                        }
                        const RenderResult result = RenderEntry(device, input, output, presetPath, options, true);
                        if (!result.ok) {
                                ++failed;
                                failures.push_back("RENDER-FAIL  " + entry.path + ": " + result.error);
                                continue;
                        }
                        if (options.update || !haveGolden) {
                                if (WritePngLocal(goldenPath, result.pixels, options.outWidth, options.outHeight)) {
                                        ++generated;
                                        std::printf("  GENERATED %s -> %s\n", entry.path.c_str(),
                                            entry.name.c_str());
                                } else {
                                        ++failed;
                                        failures.push_back("WRITE-FAIL   " + goldenPath);
                                }
                                continue;
                        }
                        int gw = 0, gh = 0, comps = 0;
                        uint8_t *golden = stbi_load(goldenPath.c_str(), &gw, &gh, &comps, 4);
                        if (golden == nullptr) {
                                ++failed;
                                failures.push_back("GOLDEN-FAIL  " + entry.path + ": cannot decode " + goldenPath);
                                continue;
                        }
                        const size_t goldenBytes = size_t(gw) * gh * 4;
                        std::vector<uint8_t> want(golden, golden + goldenBytes);
                        stbi_image_free(golden);
                        if (result.pixels.size() != goldenBytes) {
                                ++failed;
                                std::printf("  SIZE-FAIL  %s: rendered %ux%u, golden %dx%d\n", entry.path.c_str(),
                                    options.outWidth, options.outHeight, gw, gh);
                                failures.push_back("SIZE-FAIL    " + entry.path);
                                continue;
                        }
                        const CompareStats stats = CompareRgba(result.pixels, want, entry.maxDelta);
                        const double pct = stats.mismatchPct();
                        if (stats.mismatched == 0 || pct <= entry.maxPct) {
                                ++passed;
                                std::printf("  PASS       %s  (worst %d, mean %.3f, mismatch %.3f%% <= %.2f%%)\n",
                                    entry.path.c_str(), stats.worst, stats.meanDelta, pct, entry.maxPct);
                        } else {
                                ++failed;
                                std::printf("  DIFF       %s  (worst %d, mean %.3f, mismatch %.3f%% > %.2f%%, "
                                            "delta %d)\n",
                                    entry.path.c_str(), stats.worst, stats.meanDelta, pct, entry.maxPct,
                                    entry.maxDelta);
                                failures.push_back("DIFF         " + entry.path);
                        }
                }
        }

        for (size_t i : unresolved) {
                ++failed;
                failures.push_back("NOT-FOUND    " + manifest.entries[i].path + " (under no root)");
        }

        std::printf("\nconformance golden: %zu entries — %u passed, %u failed", manifest.entries.size(), passed,
            failed);
        if (options.update)
                std::printf(", %u generated", generated);
        std::printf("\n");
        for (int i = 0; i < int(failures.size()) && i < options.maxReport; ++i)
                std::printf("  %s\n", failures[i].c_str());
        if (int(failures.size()) > options.maxReport)
                std::printf("  ... (%zu more)\n", failures.size() - size_t(options.maxReport));

        if (options.update && failed == 0) {
                if (StampManifest(options.manifest.c_str(), deviceName))
                        std::printf("manifest stamp: %s\n", deviceName.c_str());
        }
        return failed == 0 ? 0 : 1;
}

// --------------------------------------------------------------- perf mode

struct PerfRecord {
        std::string relativePath;
        bool ok = false;
        std::string error;
        uint32_t passes = 0, parameters = 0;
        double meanMs = 0.0, minMs = 0.0, maxMs = 0.0;
};

int RunPerf(slv::Device &device, const Options &options, slv::Image &input, slv::Image &output)
{
        const std::unordered_set<std::string> only = LoadOnlyList(
            options.onlyFromPath.empty() ? nullptr : options.onlyFromPath.c_str());
        const std::map<std::string, double> budgets = ReadBudgets(
            options.budgetsPath.empty() ? nullptr : options.budgetsPath.c_str());
        if (!options.budgetsPath.empty() && budgets.empty())
                std::printf("WARNING: budgets file %s empty or unreadable — the budget gate cannot fail\n",
                    options.budgetsPath.c_str());

        std::vector<PerfRecord> records;
        for (const std::string &root : options.roots) {
                slv::preset::SetShaderRootDir(root);
                const std::vector<slv::preset::PresetEntry> presets = slv::preset::ScanPresets(root, 12);
                std::printf("root %s: %zu presets\n", root.c_str(), presets.size());
                for (const slv::preset::PresetEntry &entry : presets) {
                        if (!only.empty() && only.find(entry.relativePath) == only.end())
                                continue;
                        PerfRecord record;
                        record.relativePath = entry.relativePath;
                        const RenderResult result =
                            RenderEntry(device, input, output, root + "/" + entry.relativePath, options, false);
                        if (result.ok) {
                                record.ok = true;
                                record.passes = result.passes;
                                record.parameters = result.parameters;
                                record.meanMs = result.meanMs;
                                record.minMs = result.minMs;
                                record.maxMs = result.maxMs;
                        } else {
                                record.error = result.error;
                        }
                        records.push_back(std::move(record));
                }
        }

        uint32_t measured = 0, broken = 0, budgetFails = 0;
        for (const PerfRecord &record : records) {
                if (record.ok) {
                        ++measured;
                        const auto it = budgets.find(record.relativePath);
                        std::printf("  %-60s %8.3f ms/frame (min %7.3f max %8.3f  %7.1f fps)", record.relativePath.c_str(),
                            record.meanMs, record.minMs, record.maxMs,
                            record.meanMs > 0.0 ? 1000.0 / record.meanMs : 0.0);
                        if (it != budgets.end()) {
                                if (record.meanMs > it->second) {
                                        ++budgetFails;
                                        std::printf("  BUDGET-FAIL (> %.3f, +%.1f%%)", it->second,
                                            it->second > 0.0 ? 100.0 * (record.meanMs - it->second) / it->second : 0.0);
                                } else {
                                        std::printf("  budget %.3f OK (%.0f%% headroom)", it->second,
                                            it->second > 0.0 ? 100.0 * (it->second - record.meanMs) / it->second : 0.0);
                                }
                        }
                        std::printf("\n");
                } else {
                        ++broken;
                }
        }
        uint32_t shown = 0;
        for (const PerfRecord &record : records) {
                if (record.ok || shown >= options.maxReport)
                        continue;
                ++shown;
                std::printf("  BROKEN %s: %s\n", record.relativePath.c_str(), record.error.c_str());
        }
        if (broken > shown)
                std::printf("  ... (%u more broken)\n", broken - shown);

        std::printf("\nconformance perf: %zu measured, %u broken, %u budget fail(s), device %s\n", size_t(measured),
            broken, budgetFails, device.properties().deviceName);

        if (!options.budgetsPath.empty()) {
                uint32_t unmeasured = 0;
                std::map<std::string, double> measuredPaths;
                for (const PerfRecord &record : records)
                        if (record.ok)
                                measuredPaths[record.relativePath] = record.meanMs;
                for (const auto &[path, budget] : budgets) {
                        if (measuredPaths.find(path) == measuredPaths.end()) {
                                ++unmeasured;
                                std::printf("  UNMEASURED %s (budget %.3f — preset missing or broken)\n", path.c_str(),
                                    budget);
                        }
                }
                if (unmeasured != 0)
                        std::printf("  %u budget entr%s not measured (tolerated)\n", unmeasured,
                            unmeasured == 1 ? "y" : "ies");
        }

        if (!options.reportPath.empty()) {
                std::vector<const PerfRecord *> sorted;
                for (const PerfRecord &record : records)
                        if (record.ok)
                                sorted.push_back(&record);
                std::sort(sorted.begin(), sorted.end(),
                    [](const PerfRecord *a, const PerfRecord *b) { return a->relativePath < b->relativePath; });
                if (FILE *file = std::fopen(options.reportPath.c_str(), "w"); file != nullptr) {
                        std::fprintf(file, "# libslang-vk perf report (conformance perf)\n");
                        std::fprintf(file, "# device: %s\n", device.properties().deviceName);
                        std::fprintf(file, "# input: %ux%u  output: %ux%u  lut: %s\n", kInputWidth, kInputHeight,
                            options.outWidth, options.outHeight, options.lut ? "on" : "off");
                        std::fprintf(file, "# warmup: %d  timed: %d  generated: %s\n", options.warmup, options.timed,
                            NowDateStamp().c_str());
                        std::fprintf(file, "# path \\t mean-ms \\t min-ms \\t max-ms \\t fps \\t passes \\t parameters\n");
                        for (const PerfRecord *record : sorted) {
                                std::fprintf(file, "%s\t%.4f\t%.4f\t%.4f\t%.2f\t%u\t%u\n", record->relativePath.c_str(),
                                    record->meanMs, record->minMs, record->maxMs,
                                    record->meanMs > 0.0 ? 1000.0 / record->meanMs : 0.0, record->passes,
                                    record->parameters);
                        }
                        std::fclose(file);
                        std::printf("report written: %s\n", options.reportPath.c_str());
                } else {
                        std::printf("WARNING: could not write report %s\n", options.reportPath.c_str());
                }
        }

        const auto validation = device.validationErrors();
        if (!validation.empty()) {
                std::printf("VALIDATION ERRORS: %zu\n", validation.size());
                for (const std::string &message : validation)
                        std::printf("  %s\n", message.c_str());
                return 1;
        }
        if (budgetFails > 0)
                return 1;
        if (options.failOnBroken && broken > 0)
                return 1;
        return 0;
}

} // namespace

int main(int argc, char **argv)
{
        Options options;
        if (argc < 2) {
                PrintUsage();
                return 2;
        }
        options.mode = argv[1];
        if (options.mode != "golden" && options.mode != "perf") {
                PrintUsage();
                return 2;
        }
        for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                const auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
                if (arg == "--manifest" && options.mode == "golden") {
                        if (const char *v = next())
                                options.manifest = v;
                } else if (arg == "--golden-dir" && options.mode == "golden") {
                        if (const char *v = next())
                                options.goldenDir = v;
                } else if (arg == "--update" && options.mode == "golden") {
                        options.update = true;
                } else if (arg == "--frames" && options.mode == "golden") {
                        if (const char *v = next())
                                options.frames = std::max(1, std::atoi(v));
                } else if (arg == "--warmup" && options.mode == "perf") {
                        if (const char *v = next())
                                options.warmup = std::max(0, std::atoi(v));
                } else if (arg == "--timed" && options.mode == "perf") {
                        if (const char *v = next())
                                options.timed = std::max(1, std::atoi(v));
                } else if (arg == "--budgets" && options.mode == "perf") {
                        if (const char *v = next())
                                options.budgetsPath = v;
                } else if (arg == "--report" && options.mode == "perf") {
                        if (const char *v = next())
                                options.reportPath = v;
                } else if (arg == "--only-from" && options.mode == "perf") {
                        if (const char *v = next())
                                options.onlyFromPath = v;
                } else if (arg == "--fail-on-broken" && options.mode == "perf") {
                        options.failOnBroken = true;
                } else if (arg == "--no-lut") {
                        options.lut = false;
                } else if (arg == "--size") {
                        if (const char *v = next())
                                (void)ParseSize(v, options.outWidth, options.outHeight);
                } else if (arg == "--max-report") {
                        if (const char *v = next())
                                options.maxReport = std::max(0, std::atoi(v));
                } else if (arg == "--help" || arg == "-h") {
                        PrintUsage();
                        return 0;
                } else if (!arg.empty() && arg[0] != '-') {
                        options.roots.push_back(arg);
                }
        }
        if (options.roots.empty()) {
                PrintUsage();
                return 2;
        }
        if (options.manifest.empty())
                options.manifest = std::string(SLANGVK_GOLDEN_DIR) + "/manifest.txt";
        if (options.goldenDir.empty())
                options.goldenDir = SLANGVK_GOLDEN_DIR;

        slv::DeviceOptions deviceOptions;
        deviceOptions.appName = "libslang-vk-conformance";
        deviceOptions.validationLayers = true;
        auto device = slv::Device::create(deviceOptions);
        if (!device) {
                std::printf("SKIP conformance %s: no Vulkan device (%s)\n", options.mode.c_str(),
                    device.error().message.c_str());
                return 0;
        }
        std::printf("conformance %s device: %s\n", options.mode.c_str(), device->properties().deviceName);

        const std::vector<uint8_t> testcard = MakeTestcard(kInputWidth, kInputHeight);
        slv::Image::CreateInfo in;
        in.desc = {kInputWidth, kInputHeight, VK_FORMAT_R8G8B8A8_UNORM};
        in.pixels = testcard.data();
        in.pixelBytes = testcard.size();
        auto input = slv::Image::create(*device, in);
        if (!input) {
                std::printf("FAIL conformance: input image: %s\n", input.error().message.c_str());
                return 1;
        }
        slv::Image::CreateInfo out;
        out.desc = {options.outWidth, options.outHeight, VK_FORMAT_R8G8B8A8_UNORM};
        auto output = slv::Image::create(*device, out);
        if (!output) {
                std::printf("FAIL conformance: output image: %s\n", output.error().message.c_str());
                return 1;
        }

        const int rc = options.mode == "golden" ? RunGolden(*device, options, *input, *output)
                                                : RunPerf(*device, options, *input, *output);
        return rc;
}
