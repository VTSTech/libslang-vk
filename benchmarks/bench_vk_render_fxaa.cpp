/*
 * bench_vk_render_fxaa.cpp
 * Benchmark for libslang-vk Vulkan render iteration performance with FXAA.
 * Uses libretro FXAA shader from Diablo project.
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

static void BM_VkRenderFXAA(benchmark::State& state) {
    const std::string dataDir = "tests";
    
    std::cout << "# FXAA Benchmark: Testing libretro FXAA shader" << std::endl;
    std::cout << "# Data dir: " << dataDir << std::endl;
    
    std::string preset_path = dataDir + "/data/runner/presets/fxaa.slangp";
    std::cout << "# Preset path: " << preset_path << std::endl;
    
    // Check if preset file exists
    std::filesystem::path preset_file(preset_path);
    if (!std::filesystem::exists(preset_file)) {
        std::cout << "# ERROR: Preset file does not exist: " << preset_path << std::endl;
        state.SkipWithError("Preset file not found");
        return;
    }
    std::cout << "# Preset file exists" << std::endl;

    // Set shader root directory for preset path resolution
    set_shader_root_dir(dataDir);

    // Create a device for the benchmarks
    slv::DeviceOptions deviceOpts;
    deviceOpts.validationLayers = false;
    deviceOpts.appName = "bench_fxaa_device";

    auto deviceResult = slv::Device::create(deviceOpts);
    if (!deviceResult) {
        state.SkipWithError("Failed to create device");
        return;
    }
    auto& device = *deviceResult;

    std::cout << "# Device created successfully" << std::endl;

    // Load the preset
    std::cout << "# Loading FXAA preset..." << std::endl;
    auto presetResult = slv::Preset::load(device, preset_path);
    if (!presetResult) {
        std::cout << "# ERROR: Failed to load preset: " << preset_path << std::endl;
        state.SkipWithError("Failed to load preset");
        return;
    }
    std::cout << "# FXAA preset loaded successfully" << std::endl;
    auto& preset = *presetResult;

    // Chain options
    slv::ChainOptions chainOpts;
    chainOpts.outputWidth = 640;
    chainOpts.outputHeight = 480;
    chainOpts.framesInFlight = 2;

    // Create the chain once outside the timing loop
    std::cout << "# Creating chain..." << std::endl;
    auto chainResult = slv::Chain::create(device, preset, chainOpts);
    if (!chainResult) {
        state.SkipWithError("Failed to create chain");
        return;
    }
    std::cout << "# Chain created successfully" << std::endl;
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
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_VkRenderFXAA)
    ->Iterations(5)
    ->Unit(benchmark::kMicrosecond)
    ->Name("BM_VkRender_FXAA");

BENCHMARK_MAIN();