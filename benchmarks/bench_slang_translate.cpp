/*
 * bench_slang_translate.cpp
 * Benchmark for libslang-vk slang translation performance.
 * Measures time to translate .slang shaders to GLSL.
 */

#include <slangvk/core/slang_translate.hpp>
#include <slangvk/slangvk.hpp>
#include <benchmark/benchmark.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>
#include <filesystem>

// Fixed size only - use Args to specify exact parameter values
static const std::string FIXED_SLANG_DIR = []() {
    const char* d = getenv("SLANGVK_TESTS_DATA_DIR");
    return d ? std::string(d) : "tests/data";
}();

static void BM_SlangTranslate(benchmark::State& state) {
    const std::string slang_dir = FIXED_SLANG_DIR;
    // Shader corpus subdir (relative to tests/data), env-overridable.
    const char* sub = getenv("SLANGVK_BENCH_SHADER_DIR");
    const std::string shader_dir = slang_dir + "/" + (sub ? sub : "runner/shaders");
    std::vector<std::string> slang_files;

    // Collect .slang files (once). Guard the iterator: a missing directory
    // throws out of the range-for and aborts the process.
    std::error_code ec;
    const std::filesystem::directory_iterator end{};
    for (std::filesystem::directory_iterator it(shader_dir, ec); !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".slang") {
            slang_files.push_back(it->path().string());
        }
    }

    if (slang_files.empty()) {
        state.SkipWithError("No .slang files found in " + shader_dir);
        return;
    }

    // Deterministic pick: directory order is unspecified.
    std::sort(slang_files.begin(), slang_files.end());
    // Default to a shader that translates standalone (no preset-context
    // aliases); env-overridable. Falls back to the sorted-first file.
    const char* pick = getenv("SLANGVK_BENCH_SHADER");
    std::string want = pick ? pick : "blit.slang";
    std::string slang_path;
    for (const auto& f : slang_files) {
        if (std::filesystem::path(f).filename() == want) { slang_path = f; break; }
    }
    if (slang_path.empty()) slang_path = slang_files[0];

    // Read the slang file
    std::string slang_src;
    {
        std::ifstream f(slang_path);
        slang_src = std::string(std::istreambuf_iterator<char>(f), {});
    }

    slv::preset::SlangTranslation translation;
    int64_t translateFail = 0;
    for (auto _ : state) {
        // Translate slang to GLSL
        std::unordered_set<std::string> knownTexNames;
        translation.sourceSize = slang_src.size();
        translation.sourceHash = 0;
        translation.mergedSize = 0;
        translation.mergedHash = 0;
        translation.error.clear();
        translation.warnings.clear();

        if (!slv::preset::TranslateSlang(slang_path, knownTexNames, 0, 1, translation)) {
            if (++translateFail == 1)
                fprintf(stderr, "# first translate failure for %s: %s\n",
                        slang_path.c_str(), translation.error.c_str());
            continue;
        }
        // Translation result is in translation; for this benchmark we just measure time
        (void)translation; // prevent unused warning
    }

    if (translateFail) {
        state.counters["translate_fail"] = translateFail;
        state.SkipWithError("TranslateSlang failed inside the loop; timings are invalid");
        return;
    }
    state.SetItemsProcessed(state.iterations());
}

// Fixed file count only - use Args to specify exact parameter values
BENCHMARK(BM_SlangTranslate)
    ->ArgNames({"file_count"})
    ->Args({1})  // Fixed: only test 1 file
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();