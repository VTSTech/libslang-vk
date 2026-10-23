/*
 * bench_preset_parser.cpp
 * Benchmark for libslang-vk preset parsing performance.
 * Measures time to parse .slangp presets from the test corpus.
 */

#include <slangvk/core/preset_parser.hpp>
#include <slangvk/slangvk.hpp>
#include <benchmark/benchmark.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

// Fixed size only - use Args to specify exact parameter values
static const std::string FIXED_DATA_DIR = []() {
    const char* d = getenv("SLANGVK_TESTS_DATA_DIR");
    return d ? std::string(d) : "tests/data";
}();

static void BM_PresetParser(benchmark::State& state) {
    const std::string data_dir = FIXED_DATA_DIR;
    // Preset corpus subdir (relative to tests/data), env-overridable.
    // Defaults to the runner presets shipped in the repo.
    const char* sub = getenv("SLANGVK_BENCH_PRESET_DIR");
    const std::string preset_dir = data_dir + "/" + (sub ? sub : "runner/presets");
    std::vector<std::string> presets;

    // Collect .slangp files (once, outside timing loop). Guard the iterator:
    // a missing directory throws out of the range-for and aborts the process.
    std::error_code ec;
    const std::filesystem::directory_iterator end{};
    for (std::filesystem::directory_iterator it(preset_dir, ec); !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".slangp") {
            presets.push_back(it->path().string());
        }
    }

    if (presets.empty()) {
        state.SkipWithError("No .slangp presets found in " + preset_dir);
        return;
    }

    // Deterministic pick: directory order is unspecified.
    std::sort(presets.begin(), presets.end());
    const std::string& preset_path = presets[0];
    slv::preset::Preset preset;
    std::string error;
    int64_t parseFail = 0;

    for (auto _ : state) {
        // Parse the preset structure
        if (!slv::preset::ParsePreset(preset_path, preset, error)) {
            if (++parseFail == 1)
                fprintf(stderr, "# first parse failure for %s: %s\n",
                        preset_path.c_str(), error.c_str());
            continue;
        }
        // Extract passes
        auto& passes = preset.passes;
        (void)passes; // prevent unused warning
    }

    if (parseFail) {
        state.counters["parse_fail"] = parseFail;
        state.SkipWithError("ParsePreset failed inside the loop; timings are invalid");
        return;
    }
    state.SetItemsProcessed(state.iterations());
}

// Fixed preset count only - use Args to specify exact parameter values
BENCHMARK(BM_PresetParser)
    ->ArgNames({"preset_count"})
    ->Args({1})  // Fixed: only preset count=1
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();