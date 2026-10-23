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
    const std::string dataDir = "tests";

    // Set shader root directory for preset path resolution
    set_shader_root_dir(dataDir);

    std::string preset_path = dataDir + "/data/runner/presets/blit.slangp";

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
    chainOpts.framesInFlight = 2;

    for (auto _ : state) {
        // Create the Vulkan chain (this creates the device, pipelines, FBOs, descriptor sets)
        auto chainResult = slv::Chain::create(device, preset, chainOpts);
        if (!chainResult) {
            state.ResumeTiming();
            continue;
        }
        // Chain is created - just measure creation, don't run render loop
        (void)chainResult; // prevent unused warning
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_VkChainCreate)
    ->Args({1})
    ->Iterations(10)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();