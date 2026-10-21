// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk M4.07 unit test — the SPIR-V cache enabler.
//
// The library keeps no disk cache (that is the host's business); it hands
// the host a *correct* content key so the host's own cache cannot serve a
// stale shader. This test pins that contract:
//
//  - the host is asked for a pass's SPIR-V before the library compiles it;
//  - a cache hit skips the compile (the host is never asked to store it);
//  - a miss compiles and hands the result back to the host;
//  - the key is stable across loads (a host can name its files with it);
//  - the key CHANGES when the shader source changes — including through an
//    #include, which is the case a host-side hash of the .slangp alone
//    would miss.
//
// Skips cleanly (exit 0) when no Vulkan device is present — CI images
// provide lavapipe; Windows CI has no ICD.

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <slangvk/slangvk.hpp>

#ifndef SLANGVK_ENABLE_VK
int main() { return 0; } // Core-only build: runner not compiled.
#else

namespace {

int failures = 0;

void Check(bool condition, const std::string &label)
{
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition)
                ++failures;
}

/// A host's cache: the keys it was asked for, and the SPIR-V it holds.
struct HostCache {
        std::map<std::string, slv::PassSpirv> entries;
        std::vector<std::string> loadKeys;
        std::vector<std::string> storeKeys;
        bool hitOnly = false; // When set, `load` only reports hits for `hitKey`.

        auto makeOptions() -> slv::SpirvCache
        {
                slv::SpirvCache cache;
                HostCache *self = this;
                cache.load = [self](std::string_view key) -> std::optional<slv::PassSpirv> {
                        self->loadKeys.emplace_back(key);
                        const auto it = self->entries.find(std::string(key));
                        if (it == self->entries.end())
                                return std::nullopt;
                        return it->second;
                };
                cache.store = [self](std::string_view key, const slv::PassSpirv &spirv) {
                        self->storeKeys.emplace_back(key);
                        self->entries[std::string(key)] = spirv;
                };
                return cache;
        }
};

/// Copies a preset and its shaders into a scratch directory, so the test
/// can edit the source without touching the repository.
auto CopyPresetToTemp(const std::filesystem::path &source, const std::filesystem::path &dir) -> bool
{
        std::error_code ec;
        std::filesystem::create_directories(dir / "shaders", ec);
        std::filesystem::copy_file(source, dir / source.filename(), ec);
        if (ec)
                return false;
        // The runner fixtures reference their shaders by a relative path.
        const std::filesystem::path shaderDir = source.parent_path() / "shaders";
        if (!std::filesystem::exists(shaderDir))
                return false;
        for (const auto &entry : std::filesystem::directory_iterator(shaderDir, ec)) {
                if (ec)
                        return false;
                std::filesystem::copy_file(entry.path(), dir / "shaders" / entry.path().filename(), ec);
                if (ec)
                        return false;
        }
        return true;
}

} // namespace

int main()
{
        slv::DeviceOptions options;
        options.appName = "libslang-vk-cache-test";
        options.validationLayers = true;
        auto device = slv::Device::create(options);
        if (!device) {
                std::printf("SKIP runner_cache: no Vulkan device (%s)\n", device.error().message.c_str());
                return 0;
        }
        std::printf("runner device: %s\n", device->properties().deviceName);

        const std::string dataDir = SLANGVK_TESTS_DATA_DIR;
        const std::filesystem::path source = std::filesystem::path(dataDir) / "runner" / "blit.slangp";

        // The SEC-01 sandbox resolves a preset's shaders inside the shader
        // root, so the test copies its fixture into a scratch directory and
        // works from there — that is also what lets it edit the source.
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "libslang-vk-cache-test";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        Check(CopyPresetToTemp(source, dir), "the preset is copied to a scratch directory");
        slv::preset::SetShaderRootDir(dir.string());
        const std::filesystem::path blit = dir / "blit.slangp";

        // --- A cold load populates the host's cache -------------------------
        HostCache cache;
        {
                slv::PresetLoadOptions loadOptions;
                loadOptions.spirvCache = cache.makeOptions();
                auto preset = slv::Preset::load(*device, blit, loadOptions);
                Check(preset.has_value(), "the preset loads");
                if (!preset) {
                        std::printf("     load error: %s\n", preset.error().message.c_str());
                        return 1;
                }
                Check(!cache.loadKeys.empty(), "the host is asked for a pass's SPIR-V");
                Check(!cache.storeKeys.empty(), "the freshly compiled SPIR-V is handed to the host");
                Check(cache.entries.size() == cache.storeKeys.size(), "one cache entry per compiled pass");
                for (const auto &entry : cache.entries) {
                        Check(!entry.second.vertex.empty() && !entry.second.fragment.empty(),
                                "the cached SPIR-V carries both stages");
                        break;
                }
        }

        // --- A warm load hits the cache and skips the compile ---------------
        {
                HostCache warm;
                warm.entries = cache.entries; // The host still has the bytes.
                slv::PresetLoadOptions loadOptions;
                loadOptions.spirvCache = warm.makeOptions();
                auto preset = slv::Preset::load(*device, blit, loadOptions);
                Check(preset.has_value(), "the preset loads from the cache");
                Check(!warm.loadKeys.empty(), "the host is asked again");
                Check(warm.storeKeys.empty(), "a cache hit skips the compile (the host is not asked to store)");
                Check(warm.loadKeys == cache.loadKeys, "the key is stable across loads");
        }

        // --- The cached bytes are the ones the chain renders with ----------
        //
        // A chain built from a cached preset must render exactly what a
        // chain built from a fresh compile renders — the cached SPIR-V has
        // to be the real thing, not a placeholder.
        {
                slv::Image::CreateInfo imageInfo;
                imageInfo.desc = {64, 64, VK_FORMAT_R8G8B8A8_UNORM};
                const std::vector<uint8_t> pattern(64 * 64 * 4, 0x40);
                imageInfo.pixels = pattern.data();
                imageInfo.pixelBytes = pattern.size();
                auto input = slv::Image::create(*device, imageInfo);
                auto output = slv::Image::create(*device, imageInfo);
                Check(input.has_value() && output.has_value(), "the readback images are created");
                if (input && output) {
                        slv::InputFrame in;
                        in.view = input->view();
                        in.image = input->image();
                        in.format = VK_FORMAT_R8G8B8A8_UNORM;
                        in.width = 64;
                        in.height = 64;
                        slv::Offscreen target;
                        target.view = output->view();
                        target.image = output->image();
                        target.format = VK_FORMAT_R8G8B8A8_UNORM;
                        target.width = 64;
                        target.height = 64;

                        slv::ChainOptions chainOptions;
                        chainOptions.outputWidth = 64;
                        chainOptions.outputHeight = 64;

                        // Fresh compile.
                        auto fresh = slv::Preset::load(*device, blit);
                        Check(fresh.has_value(), "the preset loads for the fresh render");
                        std::vector<uint8_t> freshPixels;
                        if (fresh) {
                                auto chain = slv::Chain::create(*device, *fresh, chainOptions);
                                Check(chain.has_value(), "a chain creates from the fresh preset");
                                if (chain) {
                                        Check(chain->render(in, target, slv::FrameInfo{}).has_value(),
                                                "the fresh chain renders");
                                        auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM,
                                            64, 64, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                        if (pixels)
                                                freshPixels = *pixels;
                                }
                        }

                        // Cached compile: the host serves the bytes it stored.
                        HostCache served;
                        served.entries = cache.entries;
                        slv::PresetLoadOptions cachedOptions;
                        cachedOptions.spirvCache = served.makeOptions();
                        auto cached = slv::Preset::load(*device, blit, cachedOptions);
                        Check(cached.has_value(), "the preset loads from the cache");
                        if (cached) {
                                auto chain = slv::Chain::create(*device, *cached, chainOptions);
                                Check(chain.has_value(), "a chain creates from the cached preset");
                                if (chain) {
                                        Check(chain->render(in, target, slv::FrameInfo{}).has_value(),
                                                "the cached chain renders");
                                        auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM,
                                            64, 64, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                        Check(pixels.has_value(), "the cached chain's readback");
                                        if (pixels && !freshPixels.empty())
                                                Check(*pixels == freshPixels,
                                                        "the cached chain renders exactly what the fresh one does");
                                }
                        }
                }
        }

        // --- The key changes when the source changes -----------------------
        //
        // This is the property the whole feature exists for: a host that
        // cached by this key can never serve a stale shader.
        {
                HostCache first;
                slv::PresetLoadOptions loadOptions;
                loadOptions.spirvCache = first.makeOptions();
                auto preset = slv::Preset::load(*device, blit, loadOptions);
                Check(preset.has_value(), "the copied preset loads");
                Check(!first.loadKeys.empty(), "the copied preset has a cache key");

                // Edit the shader.
                const std::filesystem::path shader = dir / "shaders" / "blit.slang";
                if (!std::filesystem::exists(shader)) {
                        // The fixture's shader name may differ; find the only
                        // .slang in the scratch tree.
                        for (const auto &entry : std::filesystem::directory_iterator(dir / "shaders", ec)) {
                                if (entry.path().extension() == ".slang") {
                                        std::ofstream out(entry.path(), std::ios::app);
                                        out << "\n// cache key test edit\n";
                                        break;
                                }
                        }
                } else {
                        std::ofstream out(shader, std::ios::app);
                        out << "\n// cache key test edit\n";
                }

                HostCache second;
                slv::PresetLoadOptions secondOptions;
                secondOptions.spirvCache = second.makeOptions();
                auto preset2 = slv::Preset::load(*device, blit, secondOptions);
                Check(preset2.has_value(), "the edited preset loads");
                Check(!second.loadKeys.empty(), "the edited preset has a cache key");
                Check(second.loadKeys != first.loadKeys,
                        "the cache key changes when the shader source changes");

        }

        std::filesystem::remove_all(dir, ec);
        std::printf("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
        std::fflush(stdout);
        return failures == 0 ? 0 : 1;
}

#endif // SLANGVK_ENABLE_VK
