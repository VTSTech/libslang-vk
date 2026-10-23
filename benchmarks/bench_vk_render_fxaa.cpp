/*
 * bench_vk_render_fxaa.cpp
 * Benchmark for libslang-vk Vulkan render iteration performance with FXAA.
 * Uses the bundled libretro-style FXAA preset (tests/data/runner/presets),
 * rendering into device-owned input/output images.
 */

#include <slangvk/core/preset_parser.hpp>
#include <slangvk/runner.hpp>
#include <slangvk/slangvk.hpp>
#include <benchmark/benchmark.h>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

static constexpr uint32_t kWidth = 640;
static constexpr uint32_t kHeight = 480;

// Deterministic RGBA8 pattern (gradient + checker mix), generated once.
static std::vector<uint8_t> MakePattern(uint32_t w, uint32_t h) {
    std::vector<uint8_t> pixels(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &pixels[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(x * 255u / (w - 1));
            p[1] = uint8_t(y * 255u / (h - 1));
            p[2] = uint8_t(((x / 8) ^ (y / 8)) & 1 ? 255 : 0);
            p[3] = 255;
        }
    }
    return pixels;
}

static void BM_VkRenderFXAA(benchmark::State& state) {
    // Data root from the build-defined test data dir (CWD-independent).
    const char* dd = getenv("SLANGVK_TESTS_DATA_DIR");
    const std::string dataDir = dd ? dd : "tests/data";

    std::cout << "# FXAA Benchmark: testing the bundled FXAA preset" << std::endl;
    std::cout << "# Data dir: " << dataDir << std::endl;

    std::string preset_path = dataDir + "/runner/presets/fxaa.slangp";
    std::cout << "# Preset path: " << preset_path << std::endl;

    if (!std::filesystem::exists(preset_path)) {
        state.SkipWithError("Preset file not found: " + preset_path);
        return;
    }

    // Set shader root directory for preset path resolution
    slv::preset::SetShaderRootDir(dataDir);

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
    std::cout << "# Device created: " << device.properties().deviceName << std::endl;

    // Device-owned input image with a real pattern (render() requires a
    // valid view; null is a hard error per the M4.07 contract).
    const std::vector<uint8_t> pattern = MakePattern(kWidth, kHeight);
    slv::Image::CreateInfo inputInfo;
    inputInfo.desc = {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM};
    inputInfo.pixels = pattern.data();
    inputInfo.pixelBytes = pattern.size();
    auto input = slv::Image::create(device, inputInfo);
    if (!input) {
        state.SkipWithError("Failed to create input image: " + input.error().message);
        return;
    }

    slv::Image::CreateInfo outputInfo;
    outputInfo.desc = {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM};
    auto output = slv::Image::create(device, outputInfo);
    if (!output) {
        state.SkipWithError("Failed to create output image: " + output.error().message);
        return;
    }

    // Load the preset
    auto presetResult = slv::Preset::load(device, preset_path);
    if (!presetResult) {
        state.SkipWithError("Failed to load preset: " + presetResult.error().message);
        return;
    }
    std::cout << "# FXAA preset loaded successfully" << std::endl;
    auto& preset = *presetResult;

    // Chain options
    slv::ChainOptions chainOpts;
    chainOpts.outputWidth = kWidth;
    chainOpts.outputHeight = kHeight;
    chainOpts.framesInFlight = 2;

    // Create the chain once outside the timing loop
    std::cout << "# Creating chain..." << std::endl;
    auto chainResult = slv::Chain::create(device, preset, chainOpts);
    if (!chainResult) {
        state.SkipWithError("Failed to create chain: " + chainResult.error().message);
        return;
    }
    std::cout << "# Chain created successfully" << std::endl;
    auto& chain = *chainResult;

    // Per-frame input/output descriptors
    slv::InputFrame inputFrame;
    inputFrame.view = input->view();
    inputFrame.image = input->image();
    inputFrame.format = VK_FORMAT_R8G8B8A8_UNORM;
    inputFrame.width = kWidth;
    inputFrame.height = kHeight;
    slv::Offscreen offscreen;
    offscreen.view = output->view();
    offscreen.format = VK_FORMAT_R8G8B8A8_UNORM;
    offscreen.width = kWidth;
    offscreen.height = kHeight;
    slv::FrameInfo frameInfo{ .frameCount = 1, .frameDeltaUs = 16666.0F };

    int64_t renderFail = 0;
    std::string firstError;
    for (auto _ : state) {
        auto result = chain.render(inputFrame, offscreen, frameInfo);
        if (!result) {
            if (++renderFail == 1) firstError = result.error().message;
            continue;
        }
    }

    if (renderFail) {
        state.counters["render_fail"] = renderFail;
        state.SkipWithError("render() failed inside the loop: " + firstError);
        return;
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_VkRenderFXAA)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
