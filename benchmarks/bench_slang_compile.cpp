/*
 * bench_slang_compile.cpp
 * Benchmark for libslang-vk slang compilation performance.
 * Measures time to compile .slang shaders to SPIR-V via glslang.
 */

#include <slangvk/core/slang_compile.hpp>
#include <slangvk/core/slang_translate.hpp>
#include <slangvk/slangvk.hpp>
#include <benchmark/benchmark.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

// Fixed size only - use Args to specify exact parameter values
static const std::string FIXED_SLANG_DIR = []() {
    const char* d = getenv("SLANGVK_TESTS_DATA_DIR");
    return d ? std::string(d) : "tests/data";
}();

static void BM_SlangCompile(benchmark::State& state) {
    const std::string slang_dir = FIXED_SLANG_DIR;
    std::vector<std::string> slang_files;

    // Collect .slang files (once, outside timing loop for simplicity)
    for (const auto& entry : std::filesystem::directory_iterator(slang_dir)) {
        if (entry.path().extension() == ".slang") {
            slang_files.push_back(entry.path().string());
        }
    }

    if (slang_files.empty()) {
        state.SkipWithError("No .slang files found in test data directory");
        return;
    }

    // Use first slang file for benchmark (fixed)
    const std::string& slang_path = slang_files[0];

    // Read the slang file
    std::string slang_src;
    {
        std::ifstream f(slang_path);
        slang_src = std::string(std::istreambuf_iterator<char>(f), {});
    }

    slv::preset::SlangTranslation translation;
    for (auto _ : state) {
        // Translate slang to GLSL metadata
        std::unordered_set<std::string> knownTexNames;
        translation.sourceSize = slang_src.size();
        translation.sourceHash = 0;
        translation.mergedSize = 0;
        translation.mergedHash = 0;
        translation.error.clear();
        translation.warnings.clear();

        if (!slv::preset::TranslateSlang(slang_path, knownTexNames, 0, 1, translation)) {
            state.ResumeTiming();
            continue;
        }

        // Compile the translated pass to SPIR-V
        slv::preset::PassSpirv spirv;
        std::string compileError;
        if (!slv::preset::CompilePass(translation, spirv, compileError)) {
            state.ResumeTiming();
            continue;
        }
        (void)spirv; // prevent unused warning
    }

    state.SetItemsProcessed(state.iterations() * slang_files.size());
}

// Fixed file count only - use Args to specify exact parameter values
BENCHMARK(BM_SlangCompile)
    ->ArgNames({"file_count"})
    ->Args({1})  // Fixed: only test 1 file
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();