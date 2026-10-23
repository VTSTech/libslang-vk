/*
 * bench_vk_render.cpp
 * Benchmark for libslang-vk Vulkan render iteration performance.
 * Measures time per render frame through the Vulkan chain, rendering into
 * device-owned input/output images (the render() contract requires real
 * image views - a null view is a documented hard error).
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

static void BM_VkRenderIteration(benchmark::State& state) {
    // Data root from the build-defined test data dir (CWD-independent).
    const char* dd = getenv("SLANGVK_TESTS_DATA_DIR");
    const std::string dataDir = dd ? dd : "tests/data";

    // Set shader root directory for preset path resolution
    slv::preset::SetShaderRootDir(dataDir);

    std::string preset_path = dataDir + "/runner/presets/blit.slangp";

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
    auto& preset = *presetResult;

    // Chain options
    slv::ChainOptions chainOpts;
    chainOpts.outputWidth = kWidth;
    chainOpts.outputHeight = kHeight;

    // Create the chain once outside the timing loop
    auto chainResult = slv::Chain::create(device, preset, chainOpts);
    if (!chainResult) {
        state.SkipWithError("Failed to create chain: " + chainResult.error().message);
        return;
    }
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
        // Render completed
    }

    if (renderFail) {
        state.counters["render_fail"] = renderFail;
        state.SkipWithError("render() failed inside the loop: " + firstError);
        return;
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_VkRenderIteration)
    ->ArgName("frame")
    ->Args({1})
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
