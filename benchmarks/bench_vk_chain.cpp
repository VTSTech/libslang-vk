/*
 * bench_vk_chain.cpp
 * Benchmark for libslang-vk Vulkan chain creation performance.
 * Measures time to create Vulkan pipelines, FBOs, and descriptor sets.
 */

#include <slangvk/core/preset_parser.hpp>
#include <slangvk/runner.hpp>
#include <slangvk/slangvk.hpp>
#include <benchmark/benchmark.h>
#include <chrono>
#include <iostream>
#include <string>
#include <filesystem>

// Set shader root dir for preset path resolution (core function, no Vulkan dep)
static void set_shader_root_dir(const std::string& dir) {
    slv::preset::SetShaderRootDir(dir);
}

static void BM_VkChainCreate(benchmark::State& state) {
    // Data root from the build-defined test data dir (CWD-independent).
    const char* dd = getenv("SLANGVK_TESTS_DATA_DIR");
    const std::string dataDir = dd ? dd : "tests/data";

    // Set shader root directory for preset path resolution
    set_shader_root_dir(dataDir);

    std::string preset_path = dataDir + "/runner/presets/blit.slangp";

    // Create a device for the benchmarks
    slv::DeviceOptions deviceOpts;
    deviceOpts.validationLayers = false;
    deviceOpts.appName = "bench_chain_device";

    auto deviceResult = slv::Device::create(deviceOpts);
    if (!deviceResult) {
        state.SkipWithError("Failed to create device");
        return;
    }
    auto& device = *deviceResult;

    // Load the preset
    auto presetResult = slv::Preset::load(device, preset_path);
    if (!presetResult) {
        state.SkipWithError("Failed to load preset");
        return;
    }
    auto& preset = *presetResult;

    // Chain options
    slv::ChainOptions chainOpts;
    chainOpts.outputWidth = 640;
    chainOpts.outputHeight = 480;
    chainOpts.framesInFlight = 2;

    int64_t createFail = 0;
    std::string firstError;
    for (auto _ : state) {
        // Create the Vulkan chain (this creates the device, pipelines, FBOs, descriptor sets)
        auto chainResult = slv::Chain::create(device, preset, chainOpts);
        if (!chainResult) {
            if (++createFail == 1)
                firstError = chainResult.error().message;
            continue;
        }
        // Chain is created - just measure creation, don't run render loop
        (void)chainResult; // prevent unused warning
    }

    if (createFail) {
        state.counters["create_fail"] = createFail;
        state.SkipWithError("Chain::create failed inside the loop: " + firstError);
        return;
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_VkChainCreate)
    ->ArgName("chain_count")
    ->Args({1})
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();