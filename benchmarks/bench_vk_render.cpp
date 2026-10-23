/*
 * bench_vk_render.cpp
 * Benchmark for libslang-vk Vulkan render iteration performance.
 * Measures time per render frame through the Vulkan chain.
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

static void BM_VkRenderIteration(benchmark::State& state) {
    const std::string dataDir = "tests";

    // Set shader root directory for preset path resolution
    set_shader_root_dir(dataDir);

    std::string preset_path = dataDir + "/data/runner/presets/blit.slangp";

    // Create a device for the benchmarks
    slv::DeviceOptions deviceOpts;
    deviceOpts.validationLayers = false;
    deviceOpts.appName = "bench_render_device";

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

    // Create the chain once outside the timing loop
    auto chainResult = slv::Chain::create(device, preset, chainOpts);
    if (!chainResult) {
        state.SkipWithError("Failed to create chain");
        return;
    }
    auto& chain = *chainResult;

    for (auto _ : state) {
        // Run a single render iteration with empty input/output for benchmarking
        slv::InputFrame emptyInput{};
        slv::Offscreen emptyOutput{ .width = 640, .height = 480 };
        slv::FrameInfo frameInfo{ .frameCount = 1, .frameDeltaUs = 16666.0F };
        auto result = chain.render(emptyInput, emptyOutput, frameInfo);
        if (!result) {
            state.ResumeTiming();
            continue;
        }
        // Render completed
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_VkRenderIteration)
    ->Args({1})
    ->Iterations(5)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();