// libslang-vk example — shader_demo
//
// An end-to-end host implementing the library's full contract: it loads
// .slangp presets, builds Vulkan chains and renders frames into them, and
// switches presets, input sources and parameters live. The hybrid form
// mirrors how real embedders use libslang-vk:
//
//   windowed — when the build has GLFW (default) and a display is present:
//       the demo owns the window/surface/swapchain and plays the M4 host
//       contract (acquire -> Chain::render -> present) while a terminal
//       menu drives preset switching.
//   headless — otherwise (CI, servers, lavapipe): the same terminal menu
//       renders --frames warmup frames offscreen and writes the final frame
//       as a PNG (warmup exercises history/feedback readers).
//
// Usage:
//   shader_demo [options] [path ...]
//     path                  .slangp file or directory to scan (repeatable)
//     --list                print the discovered presets and exit (no device)
//     --no-menu             load + render + dump every discovered preset, exit
//     --frames N            frames per headless render (default 6)
//     --out DIR             PNG output directory (default shader_demo_output)
//     --input pattern|image initial input source (default image; the demo
//                           falls back to the pattern when testcard.png is missing)
//     --headless            force the headless path even when a display exists
//     --window              force the windowed path (fail instead of fallback)
//     --validate            enable the Khronos validation layer when present
//     -h / --help           this text
//
// With no path the bundled demo presets are offered (plus the slang-shaders
// checkout when SLANGVK_DEMO_FETCH_SHADERS=ON configured one in).
//
// Commands (terminal menu, both modes):
//   <number>  load + switch to preset number N
//   n / p     next / previous preset
//   i         toggle input source (bundled testcard.png <-> animated pattern)
//   w         write PNG (headless: render + dump now; windowed: screenshot
//             after the next presented frame)
//   l / list  list presets around the current one
//   params    list the current preset's shader parameters
//   set NAME VALUE   override a parameter (applies on the next frame)
//   q         quit

// This TU hosts the demo's own stb implementations. STB_*_STATIC keeps them
// private to this object file: libslangvk.a already exports a stb_image
// decoder copy (src/vk/lut.cpp, memory-loads only, no stdio variants), and a
// second external definition would break every link. The demo needs the
// stdio path (stbi_load on files), so it compiles a complete private copy.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION

#ifdef SLANGVK_DEMO_WINDOW
#include "window_target.hpp" // before slangvk.hpp: brings in GLFW + Vulkan
#endif

#include <slangvk/slangvk.hpp>

#include "frame_writer.hpp"
#include "input_source.hpp"
#include "preset_browser.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef SLANGVK_EXAMPLES_DATA_DIR
#define SLANGVK_EXAMPLES_DATA_DIR "presets"
#endif
#ifndef SLANGVK_DEMO_ASSETS_DIR
#define SLANGVK_DEMO_ASSETS_DIR "assets"
#endif

namespace {

constexpr uint32_t kWindowWidth = 1280;
constexpr uint32_t kWindowHeight = 720;
constexpr uint32_t kHeadlessOutWidth = 960;
constexpr uint32_t kHeadlessOutHeight = 720;

struct Options {
        std::vector<std::string> paths;
        bool listOnly = false;
        bool noMenu = false;
        bool validate = false;
        bool forceHeadless = false;
        bool forceWindow = false;
        bool inputImage = true;
        uint32_t frames = 6;
        std::string outDir = "shader_demo_output";
};

void PrintUsage()
{
        std::printf("usage: shader_demo [options] [path ...]\n"
                    "  path                  .slangp file or directory to scan (repeatable)\n"
                    "  --list                print discovered presets and exit\n"
                    "  --no-menu             render + dump every discovered preset, exit\n"
                    "  --frames N            frames per headless render (default 6)\n"
                    "  --out DIR             PNG output directory (default shader_demo_output)\n"
                    "  --input pattern|image initial input source (default image)\n"
                    "  --headless            force headless PNG mode\n"
                    "  --window              force the windowed path\n"
                    "  --validate            enable VK_LAYER_KHRONOS_validation when present\n"
                    "  -h / --help           this text\n");
}

std::string Trim(const std::string &s)
{
        const size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
                return {};
        const size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
}

std::string SanitizeForFilename(const std::string &in)
{
        std::string out;
        for (char c : in) {
                const bool safe = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == '.';
                out.push_back(safe ? c : '_');
        }
        return out.empty() ? "preset" : out;
}

/** @brief Directory containing the running executable (with trailing slash),
 * empty when the platform lookup fails. This is the only portable anchor for
 * the bundled assets: the compile-time SLANGVK_DEMO_ASSETS_DIR is a
 * build-machine path (e.g. D:/a/libslang-vk/... on GitHub Actions) that does
 * not exist on end-user machines, while the CI artifacts stage testcard.png
 * right next to the binary. */
std::string ExeDir()
{
#ifdef _WIN32
        char buf[MAX_PATH];
        const DWORD n = GetModuleFileNameA(nullptr, buf, DWORD{sizeof(buf)});
        if (n == 0 || n >= sizeof(buf))
                return {};
        const std::string exe(buf, size_t(n));
#else
        char buf[4096];
        const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0)
                return {};
        buf[n] = '\0';
        const std::string exe(buf, size_t(n));
#endif
        const size_t slash = exe.find_last_of("/\\");
        return slash == std::string::npos ? std::string() : exe.substr(0, slash + 1);
}

/** @brief Search order for bundled assets: compile-time dir (in-tree builds,
 * ctest), next to the exe (CI artifacts), exe/assets, then the CWD. */
std::vector<std::string> AssetCandidates(const char *name)
{
        std::vector<std::string> paths;
        const auto add = [&paths](std::string p) {
                if (!p.empty())
                        paths.push_back(std::move(p));
        };
        add(std::string(SLANGVK_DEMO_ASSETS_DIR) + "/" + name);
        const std::string exeDir = ExeDir();
        if (!exeDir.empty()) {
                add(exeDir + name);
                add(exeDir + "assets/" + name);
        }
        add(std::string("assets/") + name);
        add(std::string(name));
        return paths;
}

class DemoApp {
public:
        explicit DemoApp(const Options &opts) : opts_(opts) {}

        int Run()
        {
                std::printf("libslang-vk shader demo — https://github.com/VTSTech/libslang-vk\n");
                if (!DiscoverPresets())
                        return 2;
                std::printf("presets: %zu\n\n", records_.size());
                if (opts_.listOnly) {
                        PrintPresetList(true);
                        return 0;
                }

                const TargetResult target = SetupTarget();
                if (target == TargetResult::Error)
                        return 2;
                if (target == TargetResult::NoDevice) {
                        if (opts_.noMenu) {
                                std::printf("SKIP shader_demo: no Vulkan device available\n");
                                return 0;
                        }
                        return 2;
                }

                PrintBanner();
                if (opts_.inputImage)
                        ToggleInput();
                if (!inputImage_ && !BuildInputImage())
                        return 2;
                if (opts_.noMenu)
                        return RunNoMenu();
                if (!records_.empty()) {
                        if (SelectPreset(0)) {
#ifdef SLANGVK_DEMO_WINDOW
                                if (windowed_)
                                        PrintPrompt();
                                else
#endif
                                        RenderAndDump(); // headless: first frame right away
                        }
                }
#ifdef SLANGVK_DEMO_WINDOW
                if (windowed_) {
                        StartStdinReader();
                        WindowLoop();
                        QuitWindowed();
                }
#endif
                return HeadlessMenuLoop();
        }

private:
        enum class TargetResult { Ok, NoDevice, Error };

        // ---- discovery -------------------------------------------------------

        bool DiscoverPresets()
        {
                std::vector<std::string> paths = opts_.paths;
                if (paths.empty()) {
                        paths.push_back(SLANGVK_EXAMPLES_DATA_DIR);
#ifdef SLANGVK_DEMO_DEFAULT_CORPUS
                        paths.push_back(SLANGVK_DEMO_DEFAULT_CORPUS);
#endif
                }
                std::string err;
                if (!demo::CollectPresets(paths, records_, err)) {
                        std::printf("error: %s\n", err.c_str());
                        return false;
                }
                return true;
        }

        // ---- target (device + window) ----------------------------------------

        TargetResult SetupTarget()
        {
#ifdef SLANGVK_DEMO_WINDOW
                if (!opts_.forceHeadless) {
                        if (!demo::WindowTarget::InitLibrary()) {
                                if (opts_.forceWindow) {
                                        std::printf("FATAL: glfwInit failed (no display available?)\n");
                                        return TargetResult::Error;
                                }
                                std::printf("note: glfwInit failed — using headless PNG mode\n");
                        } else {
                                TargetResult r = TryWindowedTarget();
                                if (r != TargetResult::NoDevice)
                                        return r;
                                demo::WindowTarget::ShutdownLibrary();
                        }
                }
#else
                if (opts_.forceWindow) {
                        std::printf("FATAL: --window requested but this build has no GLFW support "
                                    "(configure with SLANGVK_DEMO_WINDOW=ON)\n");
                        return TargetResult::Error;
                }
#endif
                // Headless device (lavapipe/CI or the fallback path).
                slv::DeviceOptions options;
                options.appName = "libslang-vk shader demo";
                options.validationLayers = opts_.validate;
                auto created = slv::Device::create(options);
                if (!created) {
                        std::printf("no Vulkan device: %s\n", created.error().message.c_str());
                        return TargetResult::NoDevice;
                }
                device_ = std::make_unique<slv::Device>(std::move(*created));
                windowed_ = false;
                return TargetResult::Ok;
        }

#ifdef SLANGVK_DEMO_WINDOW
        /** @brief Windowed attempt: surface-capable device + window + swapchain.
         * Returns NoDevice only when the caller should fall back to headless. */
        TargetResult TryWindowedTarget()
        {
                uint32_t extCount = 0;
                const char **exts = glfwGetRequiredInstanceExtensions(&extCount);
                if (exts == nullptr) {
                        if (opts_.forceWindow) {
                                std::printf("FATAL: GLFW reports no Vulkan instance extensions\n");
                                return TargetResult::Error;
                        }
                        std::printf("note: GLFW has no Vulkan support here — using headless PNG mode\n");
                        return TargetResult::NoDevice;
                }

                slv::DeviceOptions options;
                options.appName = "libslang-vk shader demo";
                options.validationLayers = opts_.validate;
                for (uint32_t i = 0; i < extCount; ++i)
                        options.instanceExtensions.push_back(exts[i]);
                options.deviceExtensions.push_back("VK_KHR_swapchain");
                auto created = slv::Device::create(options);
                if (!created) {
                        if (opts_.forceWindow) {
                                std::printf("FATAL: Vulkan device for surface failed: %s\n",
                                    created.error().message.c_str());
                                return TargetResult::Error;
                        }
                        std::printf("note: no surface-capable Vulkan device (%s) — using headless PNG mode\n",
                            created.error().message.c_str());
                        return TargetResult::NoDevice;
                }

                auto window = std::make_unique<demo::WindowTarget>();
                std::string werr;
                if (!window->Create(*created, kWindowWidth, kWindowHeight, "libslang-vk shader demo", werr)) {
                        if (opts_.forceWindow) {
                                std::printf("FATAL: windowed path failed: %s\n", werr.c_str());
                                return TargetResult::Error;
                        }
                        std::printf("note: windowed path unavailable (%s) — using headless PNG mode\n", werr.c_str());
                        return TargetResult::NoDevice;
                }
                device_ = std::make_unique<slv::Device>(std::move(*created));
                window_ = std::move(window);
                windowed_ = true;
                return TargetResult::Ok;
        }
#endif

        // ---- rendering -------------------------------------------------------

        bool SelectPreset(int index)
        {
                if (records_.empty()) {
                        std::printf("no presets discovered — pass a directory or .slangp path\n");
                        return false;
                }
                const int count = static_cast<int>(records_.size());
                index = ((index % count) + count) % count;
                const demo::PresetRecord &rec = records_[static_cast<size_t>(index)];

                // SEC-01 sandbox: preset-internal references resolve against the
                // record's shader root. Re-point it per preset so one menu can mix
                // the bundled demos, test fixtures and a slang-shaders checkout.
                slv::preset::SetShaderRootDir(rec.root);

                auto &cached = presetCache_[rec.AbsolutePath()];
                if (!cached) {
                        auto loaded = slv::Preset::load(*device_, rec.AbsolutePath());
                        if (!loaded) {
                                // Pack layouts reference sibling trees via ../..
                                // (slang-shaders: presets/crt-plus-signal/*.slangp
                                // -> ../../crt/shaders/...). A direct-file record
                                // roots the SEC-01 sandbox at the preset's own
                                // folder, so those references resolve but then sit
                                // outside the root and load with an empty path.
                                // Retry with ancestor roots, mirroring the resolver's
                                // own ancestor walk; each attempt re-runs the whole
                                // load so the retry is the real path, not a patch.
                                namespace fs = std::filesystem;
                                const std::string &err1 = loaded.error().message;
                                // Both failure shapes mean "the SEC-01 root was
                                // set too narrow for this preset's layout":
                                //  - "cannot open shader file" — pack presets
                                //    referencing sibling trees via ../..
                                //  - "include target outside the shaders root" —
                                //    slang-shaders presets reached from a subtree
                                //    (crt/crt-effects/*.slangp include
                                //    ../../../include/compat_macros.inc, which
                                //    lives at the corpus root).
                                const bool rootTooNarrow = err1.find("cannot open shader file") != std::string::npos
                                    || err1.find("include target outside the shaders root") != std::string::npos;
                                if (rootTooNarrow) {
                                        fs::path dir(rec.root);
                                        for (int level = 0; level < 6 && !loaded; ++level) {
                                                const fs::path parent = dir.parent_path();
                                                if (parent.empty() || parent == dir)
                                                        break;
                                                dir = parent;
                                                slv::preset::SetShaderRootDir(dir.generic_string());
                                                loaded = slv::Preset::load(*device_, rec.AbsolutePath());
                                        }
                                }
                        }
                        if (!loaded) {
                                slv::preset::SetShaderRootDir(rec.root); // restore the record's own root
                                std::printf("load FAILED [%s]: %s\n", rec.AbsolutePath().c_str(),
                                    loaded.error().message.c_str());
                                return false;
                        }
                        cached = std::make_unique<slv::Preset>(std::move(*loaded));
                }

                uint32_t outWidth = kHeadlessOutWidth;
                uint32_t outHeight = kHeadlessOutHeight;
#ifdef SLANGVK_DEMO_WINDOW
                if (windowed_) {
                        const VkExtent2D ext = window_->Extent();
                        outWidth = ext.width;
                        outHeight = ext.height;
                }
#endif
                slv::ChainOptions chainOptions;
                chainOptions.outputWidth = outWidth;
                chainOptions.outputHeight = outHeight;
                chainOptions.outputFormat = VK_FORMAT_R8G8B8A8_UNORM;
                auto built = slv::Chain::create(*device_, *cached, chainOptions);
                if (!built) {
                        std::printf("chain FAILED [%s]: %s\n", rec.displayName.c_str(), built.error().message.c_str());
                        return false;
                }

                chain_ = std::move(*built);
                current_ = index;
                currentPreset_ = cached.get();
                frameCount_ = 0;
                inputDirty_ = true;
                screenshotPending_ = false;
                std::printf("[%d] %s — %u pass(es), %zu parameter(s)\n", index, rec.AbsolutePath().c_str(),
                    currentPreset_->passCount(), currentPreset_->parameterInfo().size());
#ifdef SLANGVK_DEMO_WINDOW
                if (windowed_)
                        glfwSetWindowTitle(window_->Window(),
                            ("libslang-vk demo — " + rec.displayName).c_str());
#endif
                return true;
        }

        bool BuildInputImage()
        {
                const uint8_t *pixels = nullptr;
                size_t bytes = 0;
                if (!inputIsImage_) {
                        inputWidth_ = demo::PatternSource::kWidth;
                        inputHeight_ = demo::PatternSource::kHeight;
                        pixels = pattern_.Pixels();
                        bytes = pattern_.Bytes();
                } else {
                        if (!imageLoaded_ && !LoadImageInput())
                                return false;
                        inputWidth_ = image_.Width();
                        inputHeight_ = image_.Height();
                        pixels = image_.Pixels();
                        bytes = image_.Bytes();
                }
                slv::Image::CreateInfo info;
                info.desc = {inputWidth_, inputHeight_, VK_FORMAT_R8G8B8A8_UNORM};
                info.pixels = pixels;
                info.pixelBytes = bytes;
                auto created = slv::Image::create(*device_, info);
                if (!created) {
                        std::printf("input image FAILED: %s\n", created.error().message.c_str());
                        return false;
                }
                inputImage_ = std::move(*created);
                inputDirty_ = false;
                return true;
        }

        /** @brief Uploads the active source's content (pattern animates every
         * frame via Image::update — the M4 host-upload path; the static image
         * uploads only when dirtied by a source switch). */
        bool UpdateInput()
        {
                if (!inputImage_)
                        return BuildInputImage();
                if (!inputIsImage_) {
                        pattern_.Render(frameCount_);
                        if (auto r = inputImage_->update(pattern_.Pixels(), pattern_.Bytes()); !r) {
                                std::printf("input upload FAILED: %s\n", r.error().message.c_str());
                                return false;
                        }
                } else if (inputDirty_) {
                        if (auto r = inputImage_->update(image_.Pixels(), image_.Bytes()); !r) {
                                std::printf("input upload FAILED: %s\n", r.error().message.c_str());
                                return false;
                        }
                        inputDirty_ = false;
                }
                return true;
        }

        void ToggleInput()
        {
                const bool wantImage = !inputIsImage_;
                if (wantImage && !imageLoaded_ && !LoadImageInput())
                        return;
                inputIsImage_ = wantImage;
                inputDirty_ = true;
                const uint32_t w = inputIsImage_ ? image_.Width() : demo::PatternSource::kWidth;
                const uint32_t h = inputIsImage_ ? image_.Height() : demo::PatternSource::kHeight;
                if (!inputImage_ || w != inputWidth_ || h != inputHeight_) {
                        if (!BuildInputImage())
                                return;
                        ResizeChain();
                }
                if (inputIsImage_) {
                        const std::string where = imagePath_.empty() ? std::string() : ", " + imagePath_;
                        std::printf("input: testcard.png (static%s) (%ux%u)\n", where.c_str(), w, h);
                } else {
                        std::printf("input: animated pattern (%ux%u)\n", w, h);
                }
        }

        bool LoadImageInput()
        {
                std::string tried;
                for (const std::string &path : AssetCandidates("testcard.png")) {
                        std::string err;
                        if (image_.Load(path, err)) {
                                imagePath_ = path;
                                imageLoaded_ = true;
                                return true;
                        }
                        if (!tried.empty())
                                tried += ", ";
                        tried += path;
                }
                std::printf("warning: testcard.png not found (tried %s) — staying on the animated pattern\n",
                    tried.c_str());
                return false;
        }

        slv::InputFrame MakeInputFrame() const
        {
                slv::InputFrame frame;
                frame.view = inputImage_->view();
                frame.image = inputImage_->image(); // history/mipmap/feedback readers blit from it
                frame.format = inputImage_->desc().format;
                frame.width = inputWidth_;
                frame.height = inputHeight_;
                frame.rotation = slv::Rotation::None;
                return frame;
        }

        slv::FrameInfo MakeFrameInfo(float deltaUs) const
        {
                slv::FrameInfo info;
                info.frameCount = frameCount_;
                info.frameDeltaUs = deltaUs;
                info.frameDirection = 1;
                return info;
        }

        slv::Offscreen MakeOffscreenTarget() const
        {
                const slv::ImageDesc &desc = offscreen_->desc();
                slv::Offscreen target;
                target.view = offscreen_->view();
                target.image = offscreen_->image(); // feedback-read final passes blit into it
                target.format = desc.format;
                target.width = desc.width;
                target.height = desc.height;
                return target;
        }

        void ResizeChain()
        {
                uint32_t outWidth = kHeadlessOutWidth;
                uint32_t outHeight = kHeadlessOutHeight;
#ifdef SLANGVK_DEMO_WINDOW
                if (windowed_) {
                        const VkExtent2D ext = window_->Extent();
                        outWidth = ext.width;
                        outHeight = ext.height;
                }
#endif
                if (chain_) {
                        if (auto r = chain_->resize(inputWidth_, inputHeight_, outWidth, outHeight); !r)
                                std::printf("chain resize FAILED: %s\n", r.error().message.c_str());
                }
        }

        /** @brief Headless path: render --frames frames into the offscreen
         * target, read the final frame back and write it as a PNG. */
        bool RenderAndDump()
        {
                if (!chain_ || currentPreset_ == nullptr) {
                        std::printf("no preset loaded\n");
                        return false;
                }
                if (!offscreen_) {
                        slv::Image::CreateInfo info;
                        info.desc = {kHeadlessOutWidth, kHeadlessOutHeight, VK_FORMAT_R8G8B8A8_UNORM};
                        auto created = slv::Image::create(*device_, info);
                        if (!created) {
                                std::printf("offscreen target FAILED: %s\n", created.error().message.c_str());
                                return false;
                        }
                        offscreen_ = std::move(*created);
                }
                for (uint32_t f = 0; f < opts_.frames; ++f) {
                        if (!UpdateInput())
                                return false;
                        if (auto r = chain_->render(
                                MakeInputFrame(), slv::OutputTarget{MakeOffscreenTarget()}, MakeFrameInfo(16666.7F));
                            !r) {
                                std::printf("render FAILED: %s\n", r.error().message.c_str());
                                return false;
                        }
                        ++frameCount_;
                }
                const slv::ImageDesc desc = offscreen_->desc();
                auto pixels =
                    device_->readPixels(offscreen_->image(), desc.format, desc.width, desc.height, offscreen_->layout());
                if (!pixels) {
                        std::printf("readback FAILED: %s\n", pixels.error().message.c_str());
                        return false;
                }
                std::filesystem::create_directories(opts_.outDir);
                const std::string name = std::to_string(current_) + "-" + SanitizeForFilename(
                    records_[static_cast<size_t>(current_)].relativePath) + ".png";
                const std::string path = (std::filesystem::path(opts_.outDir) / name).generic_string();
                std::string err;
                if (!demo::WritePng(path, pixels->data(), desc.width, desc.height, false, err)) {
                        std::printf("%s\n", err.c_str());
                        return false;
                }
                std::printf("wrote %s (%u frame(s) rendered, %ux%u)\n", path.c_str(), opts_.frames, desc.width,
                    desc.height);
                return true;
        }

        int RunNoMenu()
        {
                uint32_t ok = 0;
                uint32_t failed = 0;
                for (int i = 0; i < static_cast<int>(records_.size()); ++i) {
                        if (SelectPreset(i) && RenderAndDump())
                                ++ok;
                        else
                                ++failed;
                }
                std::printf("shader_demo: %u rendered, %u failed — %s\n", ok, failed,
                    failed == 0 ? "gate ok" : "gate FAILED");
                return failed == 0 ? 0 : 1;
        }

#ifdef SLANGVK_DEMO_WINDOW
        // ---- windowed loop ---------------------------------------------------

        [[noreturn]] void QuitWindowed()
        {
                // The detached stdin reader may still sit inside getline(); skip
                // static teardown (std::_Exit) and let the OS reclaim the Vulkan
                // handles — the same trade a real game host makes on force-quit.
                std::fflush(stdout);
                std::fflush(stderr);
                std::_Exit(0);
        }

        void StartStdinReader()
        {
                std::thread reader([this] {
                        std::string line;
                        while (std::getline(std::cin, line)) {
                                const std::lock_guard<std::mutex> lock(stdinMutex_);
                                stdinLines_.push_back(line);
                        }
                        const std::lock_guard<std::mutex> lock(stdinMutex_);
                        stdinEof_ = true;
                });
                reader.detach(); // joined implicitly by QuitWindowed()'s _Exit
        }

        bool PopStdin(std::string &out)
        {
                const std::lock_guard<std::mutex> lock(stdinMutex_);
                if (stdinLines_.empty())
                        return false;
                out = stdinLines_.front();
                stdinLines_.erase(stdinLines_.begin());
                return true;
        }

        void WindowLoop()
        {
                auto last = std::chrono::steady_clock::now();
                std::string line;
                while (!window_->ShouldClose()) {
                        window_->PollEvents();
                        while (PopStdin(line))
                                if (!ExecuteCommand(line))
                                        return;

                        int fbWidth = 0;
                        int fbHeight = 0;
                        glfwGetFramebufferSize(window_->Window(), &fbWidth, &fbHeight);
                        if (fbWidth == 0 || fbHeight == 0)
                                continue; // minimized
                        if (window_->PollResize() && !RecreateWindowTargets())
                                return;

                        if (!chain_)
                                continue; // no preset selected yet

                        uint32_t index = 0;
                        VkResult acquired = window_->Acquire(&index);
                        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
                                if (!RecreateWindowTargets())
                                        return;
                                continue;
                        }
                        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
                                std::printf("acquire FAILED (VkResult %d)\n", static_cast<int>(acquired));
                                return;
                        }

                        if (!UpdateInput())
                                return;
                        const auto now = std::chrono::steady_clock::now();
                        const auto delta =
                            std::chrono::duration_cast<std::chrono::microseconds>(now - last).count();
                        last = now;

                        if (auto r = chain_->render(MakeInputFrame(), slv::OutputTarget{window_->Frame(index)},
                                MakeFrameInfo(static_cast<float>(delta)));
                            !r) {
                                std::printf("render FAILED: %s\n", r.error().message.c_str());
                                return;
                        }
                        ++frameCount_;
                        if (screenshotPending_) {
                                screenshotPending_ = false;
                                DoWindowScreenshot(index);
                        }

                        const VkResult presented = window_->Present(device_->graphicsQueue(), index);
                        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
                                window_->FlagRecreate();
                }
        }

        bool RecreateWindowTargets()
        {
                std::string err;
                if (!window_->RecreateSwapchain(err)) {
                        std::printf("swapchain recreate FAILED: %s\n", err.c_str());
                        return false;
                }
                ResizeChain();
                return true;
        }

        void DoWindowScreenshot(uint32_t index)
        {
                if (!window_->TransferSrcSwapchain()) {
                        std::printf("screenshot unavailable: swapchain images lack TRANSFER_SRC usage\n");
                        return;
                }
                const VkExtent2D ext = window_->Extent();
                auto pixels = device_->readPixels(
                    window_->Images()[index], window_->Format(), ext.width, ext.height,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
                if (!pixels) {
                        std::printf("screenshot readback FAILED: %s\n", pixels.error().message.c_str());
                        return;
                }
                std::filesystem::create_directories(opts_.outDir);
                char name[64];
                std::snprintf(name, sizeof(name), "screenshot-%03u.png", screenshotIndex_++);
                const std::string path = (std::filesystem::path(opts_.outDir) / name).generic_string();
                const bool bgra = window_->Format() == VK_FORMAT_B8G8R8A8_UNORM;
                std::string err;
                if (!demo::WritePng(path, pixels->data(), ext.width, ext.height, bgra, err)) {
                        std::printf("%s\n", err.c_str());
                        return;
                }
                std::printf("wrote %s (%ux%u)\n", path.c_str(), ext.width, ext.height);
        }
#endif

        // ---- headless menu ---------------------------------------------------

        int HeadlessMenuLoop()
        {
                PrintPresetList(false);
                std::string line;
                while (true) {
                        PrintPrompt();
                        if (!std::getline(std::cin, line))
                                break; // EOF — quit
                        if (!ExecuteCommand(line))
                                break;
                }
                std::printf("bye\n");
                return 0;
        }

        // ---- UI --------------------------------------------------------------

        void PrintBanner() const
        {
                const VkPhysicalDeviceProperties &props = device_->properties();
#ifdef SLANGVK_DEMO_WINDOW
                if (windowed_)
                        std::printf("device: %s — windowed %ux%u (terminal menu drives switching)\n\n",
                            props.deviceName, kWindowWidth, kWindowHeight);
                else
#endif
                        std::printf("device: %s — headless (PNG out: %s)\n\n", props.deviceName, opts_.outDir.c_str());
        }

        void PrintPrompt() const
        {
                if (current_ >= 0)
                        std::printf("[%d] shader_demo (n/p/i/w/l/params/set/q, ? for help) > ", current_);
                else
                        std::printf("shader_demo (number/n/p/i/w/l/params/set/q, ? for help) > ");
                std::fflush(stdout);
        }

        void PrintPresetList(bool full) const
        {
                const int count = static_cast<int>(records_.size());
                std::printf("presets: %d\n", count);
                if (count == 0)
                        return;
                int begin = 0;
                int end = count;
                if (!full) {
                        constexpr int kWindow = 20;
                        const int center = current_ < 0 ? 0 : current_;
                        begin = std::max(0, center - kWindow / 2);
                        end = std::min(count, begin + kWindow);
                }
                if (begin > 0)
                        std::printf("  ... %d earlier\n", begin);
                for (int i = begin; i < end; ++i)
                        std::printf("%s %4d  %s\n", i == current_ ? ">" : " ", i + 1,
                            records_[static_cast<size_t>(i)].AbsolutePath().c_str());
                if (end < count)
                        std::printf("  ... %d more\n", count - end);
                std::printf("\n");
        }

        void PrintParams() const
        {
                if (currentPreset_ == nullptr) {
                        std::printf("no preset loaded\n");
                        return;
                }
                const auto params = currentPreset_->parameterInfo();
                if (params.empty()) {
                        std::printf("preset has no parameters\n");
                        return;
                }
                for (const slv::ParameterInfo &p : params)
                        std::printf("  %s = %.3f (default %.3f, range %.3f..%.3f step %.3f)\n", p.name.c_str(),
                            static_cast<double>(p.defaultValue), static_cast<double>(p.defaultValue),
                            static_cast<double>(p.minimum), static_cast<double>(p.maximum),
                            static_cast<double>(p.step));
        }

        void PrintHelp() const
        {
                std::printf("commands:\n"
                            "  <number>  load + switch to preset number N\n"
                            "  n / p     next / previous preset\n"
                            "  i         toggle input (testcard.png <-> animated pattern)\n"
                            "  w         write PNG (headless: render + dump; windowed: screenshot)\n"
                            "  l / list  list presets around the current one\n"
                            "  params    show shader parameters\n"
                            "  set NAME VALUE   override a parameter (next frame)\n"
                            "  q         quit\n");
        }

        /** @brief Returns false only when the app should quit. */
        bool ExecuteCommand(const std::string &raw)
        {
                const std::string line = Trim(raw);
                if (line.empty())
                        return true;
                if (line == "q" || line == "quit" || line == "exit")
                        return false;
                if (line == "?" || line == "help") {
                        PrintHelp();
                        return true;
                }
                if (line == "l" || line == "list") {
                        PrintPresetList(false);
                        return true;
                }
                if (line == "n") {
                        if (SelectPreset(current_ + 1) && !windowed_)
                                RenderAndDump();
                        return true;
                }
                if (line == "p") {
                        if (SelectPreset(current_ - 1) && !windowed_)
                                RenderAndDump();
                        return true;
                }
                if (line == "i") {
                        ToggleInput();
                        if (!windowed_)
                                RenderAndDump();
                        return true;
                }
                if (line == "w") {
                        if (windowed_)
                                screenshotPending_ = true;
                        else
                                RenderAndDump();
                        return true;
                }
                if (line == "params") {
                        PrintParams();
                        return true;
                }
                if (line.rfind("set ", 0) == 0) {
                        const std::string rest = Trim(line.substr(4));
                        const size_t space = rest.find(' ');
                        if (space == std::string::npos || !chain_) {
                                std::printf("usage: set <Name> <value>\n");
                                return true;
                        }
                        const std::string name = rest.substr(0, space);
                        const float value = std::strtof(rest.c_str() + space + 1, nullptr);
                        if (auto r = chain_->setParameter(name, value); !r)
                                std::printf("set FAILED: %s\n", r.error().message.c_str());
                        else
                                std::printf("%s = %.3f (applies next frame)\n", name.c_str(),
                                    static_cast<double>(value));
                        return true;
                }
                const bool numeric =
                    !line.empty() && std::all_of(line.begin(), line.end(), [](unsigned char c) {
                            return std::isdigit(c) != 0;
                        });
                if (numeric) {
                        if (SelectPreset(std::atoi(line.c_str()) - 1) && !windowed_)
                                RenderAndDump();
                        return true;
                }
                std::printf("unknown command '%s' — '?' lists commands\n", line.c_str());
                return true;
        }

        // ---- state -----------------------------------------------------------
        //
        // Declaration order matters: reverse destruction must tear down Vulkan
        // children before the Device. offscreen_/inputImage_/chain_/window_ all
        // reference device_; presetCache_ (LUT uploads live in Preset) must
        // outlive chain_.

        const Options &opts_;

        std::unique_ptr<slv::Device> device_;
        std::vector<demo::PresetRecord> records_;
        std::unordered_map<std::string, std::unique_ptr<slv::Preset>> presetCache_;
#ifdef SLANGVK_DEMO_WINDOW
        std::unique_ptr<demo::WindowTarget> window_;
#endif
        bool windowed_ = false;

        int current_ = -1;
        slv::Preset *currentPreset_ = nullptr;
        std::optional<slv::Chain> chain_;

        uint32_t inputWidth_ = demo::PatternSource::kWidth;
        uint32_t inputHeight_ = demo::PatternSource::kHeight;
        std::optional<slv::Image> inputImage_;
        bool inputIsImage_ = false;
        bool inputDirty_ = true;
        demo::PatternSource pattern_;
        demo::ImageSource image_;
        bool imageLoaded_ = false;
        std::string imagePath_;

        std::optional<slv::Image> offscreen_;
        uint64_t frameCount_ = 0;
        uint32_t screenshotIndex_ = 0;
        bool screenshotPending_ = false;

        std::thread stdinThread_;
        std::mutex stdinMutex_;
        std::vector<std::string> stdinLines_;
        bool stdinEof_ = false;
};

} // namespace

// Shader demo entry point. Core-only builds have no runner to drive.
#ifndef SLANGVK_ENABLE_VK
int main()
{
        std::printf("shader_demo: built without the Vulkan runner (SLANGVK_ENABLE_VK=OFF); nothing to demo.\n");
        return 0;
}
#else

int main(int argc, char *argv[])
{
        Options opts;
        bool bad = false;
        for (int i = 1; i < argc; ++i) {
                const std::string arg = argv[i];
                const bool hasValue = i + 1 < argc;
                if (arg == "-h" || arg == "--help") {
                        PrintUsage();
                        return 0;
                }
                if (arg == "--list")
                        opts.listOnly = true;
                else if (arg == "--no-menu")
                        opts.noMenu = true;
                else if (arg == "--validate")
                        opts.validate = true;
                else if (arg == "--headless")
                        opts.forceHeadless = true;
                else if (arg == "--window")
                        opts.forceWindow = true;
                else if (arg == "--input" && hasValue) {
                        const std::string value = argv[++i];
                        if (value != "pattern" && value != "image") {
                                std::printf("error: --input expects 'pattern' or 'image', got '%s'\n", value.c_str());
                                bad = true;
                        }
                        opts.inputImage = value == "image";
                } else if (arg == "--frames" && hasValue) {
                        opts.frames = static_cast<uint32_t>(std::atoi(argv[++i]));
                        if (opts.frames == 0)
                                opts.frames = 1;
                } else if (arg == "--out" && hasValue) {
                        opts.outDir = argv[++i];
                } else if (!arg.empty() && arg[0] == '-') {
                        std::printf("error: unknown option '%s'\n", arg.c_str());
                        bad = true;
                } else {
                        opts.paths.push_back(arg);
                }
        }
        if (bad) {
                PrintUsage();
                return 2;
        }

        DemoApp app(opts);
        return app.Run();
}

#endif // SLANGVK_ENABLE_VK
