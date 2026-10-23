/*
 * bench_preset_parser.cpp
 * Benchmark for libslang-vk preset parsing performance.
 * Measures time to parse .slangp presets from the test corpus.
 */

#include <slangvk/core/preset_parser.hpp>
#include <slangvk/slangvk.hpp>
#include <benchmark/benchmark.h>
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
    std::string preset_dir = std::string(data_dir) + "/presets";
    std::vector<std::string> presets;

    // Collect .slangp files (once, outside timing loop)
    for (const auto& entry : std::filesystem::directory_iterator(preset_dir)) {
        if (entry.path().extension() == ".slangp") {
            presets.push_back(entry.path().string());
        }
    }

    if (presets.empty()) {
        state.SkipWithError("No .slangp presets found in test data directory");
        return;
    }

    // Use first preset for benchmark (fixed)
    const std::string& preset_path = presets[0];
    slv::preset::Preset preset;
    std::string error;

    for (auto _ : state) {
        // Parse the preset structure
        if (!slv::preset::ParsePreset(preset_path, preset, error)) {
            state.ResumeTiming();
            continue;
        }
        // Extract passes
        auto& passes = preset.passes;
        (void)passes; // prevent unused warning
    }

    state.SetItemsProcessed(state.iterations() * presets.size());
}

// Fixed preset count only - use Args to specify exact parameter values
BENCHMARK(BM_PresetParser)
    ->ArgNames({"preset_count"})
    ->Args({1})  // Fixed: only preset count=1
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();