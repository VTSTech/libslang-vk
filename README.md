# libslang-vk

<img width="1280" height="971" alt="image" src="https://github.com/user-attachments/assets/1442876b-4c29-40d3-9771-946f0619300e" />


[![ci](https://github.com/VTSTech/libslang-vk/actions/workflows/ci.yml/badge.svg)](https://github.com/VTSTech/libslang-vk/actions/workflows/ci.yml)

Parse, translate and compile **libretro slang shader presets** (`.slangp` /
`.slang`) as an embeddable C++23 library, with a **Vulkan** execution path.

```
.slangp ──parser──▶ pass chain ──translator──▶ per-stage GLSL ──glslang──▶ SPIR-V ──runner──▶ Vulkan
```

The core layer is extracted from the proven preset stack of
[VTSTech/DevilutionX](https://github.com/VTSTech/DevilutionX)'s GPU shader mod
(corpus-validated against all 2,658 libretro presets; provenance and
divergences documented in [PROVENANCE.md](PROVENANCE.md)). The format contract
is [docs/SPEC.md](docs/SPEC.md); the project plan with milestones, API sketch
and risk register is [docs/PLAN.md](docs/PLAN.md).

**Status: M4 (presentation) — library milestones complete.** All library-side
M0–M4 gates are green: 19/19 ctest, byte-exact goldens, zero validation-layer
errors on lavapipe. The M0 core suite is joined by the M1 runner skeleton
(Device adopt/create, offscreen `Chain::render`, golden blit round-trips), the
M2 execution contract (history rings `OriginalHistory#`, feedback
double-buffering `PassFeedback#` / `<name>Feedback`, per-pass FBO formats,
`mipmap_input` refill, FBO recreation on resize, live parameters), M3 LUT
uploads, and — since M4 — swapchain presentation: the final pass draws into an
acquired swapchain image with the letterbox rect and orientation handled by
the library (plan §8 output mode (a)). Per-item implementation status,
audited against the plan: [CHECKLIST.md](CHECKLIST.md).

**DevilutionX integration (plan §11 M4) is underway** on the fork's
[`libslang-vk`](https://github.com/VTSTech/DevilutionX/tree/libslang-vk)
branch: `gpu_scaler` keeps its public interface and runs the library's
Vulkan runner dual-path with the SDL_GPU runner — preset browser, ini
persistence, OSD overlay and the `(Unavailable)` fallback continue unchanged
on top (plan §9). Hardware field sweeps on Windows + Linux are the spike's
exit gate.

## Contents

- [Quickstart](#quickstart)
- [Shader demo](#shader-demo)
- [Consuming](#consuming)
- [Vulkan runner](#vulkan-runner-m1--m2--m3--m4)
- [Example](#example)
- [Building and testing](#building-and-testing)
- [CI artifacts](#ci-artifacts)
- [Documentation](#documentation)
- [License](#license)

## Quickstart

Build and test — glslang, Vulkan-Headers, volk and VMA are fetched and
SHA-256-pinned automatically, so no Vulkan SDK is needed at link time:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Then load, render and switch real shader presets with the bundled demo:

```sh
./build/examples/shader_demo                                 # menu over the bundled presets
./build/examples/shader_demo path/to/slang-shaders           # browse any preset pack
./build/examples/shader_demo --no-menu --out shots           # headless: PNG per preset, exit
```

And embed the library in four calls:

```cpp
auto device = slv::Device::create({.appName = "myhost"});
auto preset = slv::Preset::load(*device, "shaders/crt-royale.slangp");
auto chain  = slv::Chain::create(*device, *preset, {.outputWidth = 1280, .outputHeight = 720});
chain->render(inputFrame, outputTarget, {});   // once per frame
```

## Shader demo

[`examples/shader_demo`](examples/shader_demo) is a working host built
directly on the public API — the end-to-end embedding story. It discovers
presets, renders chains and switches presets, input sources and shader
parameters live, in two forms:

- **Windowed** (default when GLFW is buildable): opens a Vulkan window and
  plays the M4 presentation contract from the host side — the demo owns the
  window, surface and swapchain and the acquire/present pair, while the
  library draws the chain into the acquired image with fit-and-center
  letterboxing.
- **Headless** (automatic fallback when no display exists, or `--headless`):
  renders warmup frames offscreen and writes the final frame as a PNG — how
  the ctest smoke runs on CI/lavapipe.

```sh
shader_demo                                          # menu over the bundled presets
shader_demo ~/slang-shaders                          # browse a full shader pack
shader_demo crt/blend.slangp                         # load one preset directly
shader_demo --no-menu --frames 2 --out shots pack/   # dump one PNG per preset, exit
shader_demo --list                                   # print what would be offered
```

Menu commands (terminal-driven, both modes):

| Command        | Action                                                         |
|----------------|----------------------------------------------------------------|
| `<number>`     | load + switch to preset number N                               |
| `n` / `p`      | next / previous preset                                         |
| `i`            | toggle input: animated test pattern ↔ bundled `testcard.png`   |
| `w`            | write PNG (headless: render + dump now; windowed: screenshot)  |
| `l` / `list`   | list presets around the current one                            |
| `params`       | show the current preset's shader parameters                    |
| `set NAME VAL` | override a parameter (applies on the next frame)               |
| `q`            | quit                                                           |

Notes:

- Both input sources are uploaded through `Image::update` — the M4 per-frame
  host-upload path a game host uses for its converted frame. `--input image`
  starts on the bundled static test card instead of the animated pattern.
- Preset switching re-points the SEC-01 shader-root sandbox per source, so a
  single menu can mix the bundled demos, the test fixture trees and a full
  slang-shaders checkout.
- `--validate` turns on `VK_LAYER_KHRONOS_validation` when installed.

CMake options:

- `SLANGVK_DEMO_WINDOW` (default `ON`) — fetches GLFW 3.4 and compiles the
  windowed path; `OFF` gives a pure headless demo build with no GLFW fetch.
  GLFW 3.4 hard-fails its configure when a Linux windowing stack is only
  half present, so every required dev package is probed up front and GLFW is
  only fetched when one full backend exists: X11 needs the `libx11-dev`,
  `libxrandr-dev`, `libxinerama-dev`, `libxext-dev`, `libxcursor-dev`,
  `libxi-dev` headers, Wayland needs `libwayland-dev` (with
  `wayland-scanner`) plus `libxkbcommon-dev`. With neither stack installed
  (typical CI images), the demo silently builds headless-only instead of
  failing configure. First-configure overrides: `-DGLFW_BUILD_X11=ON/OFF`,
  `-DGLFW_BUILD_WAYLAND=ON/OFF`.
- `SLANGVK_DEMO_FETCH_SHADERS` (default `OFF`) — shallow-clones
  [libretro/slang-shaders](https://github.com/libretro/slang-shaders) at
  configure time and seeds the demo's default preset search with it (large
  download; runtime paths work without it too).

## Consuming

```cmake
include(FetchContent)
FetchContent_Declare(libslang-vk
    GIT_REPOSITORY https://github.com/VTSTech/libslang-vk.git
    GIT_TAG        main)
FetchContent_MakeAvailable(libslang-vk)

target_link_libraries(myapp PRIVATE slangvk::slangvk)        # static
# or slangvk::shared for the shared build
```

```cpp
#include <slangvk/slangvk.hpp>

slv::preset::SetShaderRootDir("shaders");       // once, at startup
slv::preset::Preset preset;
std::string error;
if (slv::preset::ParsePreset(slv::preset::ResolvePresetPath("crt-royale.slangp"),
                             preset, error)) {
    for (const auto &pass : preset.passes) {
        slv::preset::SlangTranslation t;
        slv::preset::TranslateSlang(slv::preset::ResolvePresetFilePath(preset.dir, pass.shaderPath),
                                    {}, /*passIndex*/ 0, /*passCount*/ 1, t);
        slv::preset::PassSpirv spirv;
        slv::preset::CompilePass(t, spirv, error);
        // spirv.vertex / spirv.fragment: SPIR-V for the Vulkan runner
    }
}
```

The descriptor conventions the translated GLSL targets (vertex UBOs set 1,
fragment textures set 2, fragment UBOs set 3) are exactly what the Vulkan
runner consumes; a host rolling its own pipeline can use them too.

## Vulkan runner (M1 + M2 + M3 + M4)

The runner executes compiled presets on a Vulkan device. The host either
adopts its own context or goes headless; errors come back as
`std::expected<T, slv::Error>`:

```cpp
#include <slangvk/slangvk.hpp>

// Headless device (lavapipe/CI) or slv::Device::adopt(instance, pd, dev, queue, family)
auto device = slv::Device::create({.appName = "myhost"});

auto preset = slv::Preset::load(*device, "shaders/crt-royale.slangp");

slv::ChainOptions opts{.outputWidth = 1280, .outputHeight = 720};
auto chain = slv::Chain::create(*device, *preset, opts);

// One frame: input texture in SHADER_READ_ONLY_OPTIMAL, output into your view.
// .image fields are needed for history/mipmap presets and feedback-read
// final passes (the runner blits from/into them; content is never modified).
chain->render(
    {/* .view = */ inputView, /* .image = */ inputImage, VK_FORMAT_R8G8B8A8_UNORM, 640, 480,
     slv::Rotation::None},
    slv::Offscreen{outputView, outputImage, VK_FORMAT_R8G8B8A8_UNORM, 1280, 720},
    {/* .frameCount = */ 0, 16666.7F, 1});
```

M1+M2 semantics already live: multi-pass chains with per-pass scale rules
and aliases, the pinned UBO fill (sizes, FrameCount mod, parameters —
alias/`#pragma name` sizes resolve to the owning pass), history rings
(depth+1 RGBA8 slots, seeded warmup, T-N indexing per SPEC 5.2),
demand-driven feedback double-buffering with a final-pass resolve blit
(SPEC 5.3), per-pass target formats, mip-chain refill for `mipmap_input`
readers (F1), FBO recreation on resize, and case-insensitive live
parameters.

M3 adds lookup textures (SPEC 3.5): image files decode to RGBA8 and
upload as `R8G8B8A8_UNORM` with the LUT's own filter/wrap options and a
full mip chain generated at upload when `name_mipmap = true`. Bindings
resolve by declared name (case-insensitive, F9) or positionally via
`User#` (first-declared order), `<LutName>Size` uniforms carry the
uploaded size, and git-lfs pointer files fail the load with an explicit
diagnostic. `PresetLoadOptions::noLutImages` keeps compile-only sweeps
(no device work); chains then fail at bind time with a named error. A
`Preset` must outlive the `Chain`s created from it (LUT views are
borrowed). M4 adds presentation: render into an acquired swapchain image
with `SwapchainFrame` (the host acquires and presents, the library waits
the acquire semaphore and leaves the image in `PRESENT_SRC_KHR`), with
fit-and-center or explicit-rect letterboxing (reference-host present
math, linear sampling, black margins), input-frame rotation (the chain
renders in source orientation, every `Rotation` uniform carries the
value, the present draw rotates the output), and `Image::update` for
per-frame host uploads. Final passes that do not resolve to the output
target size letterbox through the same draw instead of failing. Gates:
golden blit + full chain semantics + LUT goldens + letterbox/rotation
goldens + a full acquire/render/present/readback cycle on
`VK_EXT_headless_surface` — headless on lavapipe with a **live**
validation-error gate — `tests/test_runner_headless.cpp` (M1),
`tests/test_runner_m2.cpp` (M2 field canaries), `tests/test_runner_m3.cpp`
(M3) and `tests/test_runner_m4.cpp` (M4).

Vulkan is loaded at runtime through vendored [volk](https://github.com/zeux/volk)
(no link-time libvulkan); memory comes from vendored
[VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).
Hosts adopting a context keep owning their instance/device/queue.

## Example

[`examples/preset_info.cpp`](examples/preset_info.cpp) is a complete
end-to-end demo in ~180 lines: it loads a `.slangp`, prints the pass chain,
scale rules, aliases and parameters, then translates and compiles every pass
to SPIR-V with glslang — printing the per-stage bytecode sizes and the dense
texture bindings the Vulkan runner creates. Two demo presets are
bundled under [`examples/presets/`](examples/presets): a single-pass
brightness shader (`demo-bright`) and a two-pass glow chain exercising
per-pass scale rules, linear filtering and pass aliases (`demo-glow`).

```sh
cmake --build build --target preset_info
./build/examples/preset_info                       # bundled demo-bright
./build/examples/preset_info examples/presets/demo-glow.slangp
./build/examples/preset_info path/to/any.slangp    # any preset on disk
```

Each demo preset also runs as a ctest smoke test (`example_preset_info_*`),
so the example path is covered by CI. Exit code is 0 only when every pass
parses, translates and compiles — convenient for checking foreign presets.

[`examples/corpus_sweep.cpp`](examples/corpus_sweep.cpp) is the M3
conformance harness: it walks preset directories through the real load
path and reports a per-preset ledger (parse/translate/compile/LUT upload),
optionally executing chains on the Vulkan device:

```sh
cmake --build build --target corpus_sweep
./build/examples/corpus_sweep path/to/shader-pack          # compile-only sweep
./build/examples/corpus_sweep pack --render --lut          # + render 2 frames per chain
./build/examples/corpus_sweep pack --fail-on-broken        # nonzero exit on failures
```

`--render` executes every chain through the real render path — presets
whose final pass does not resolve to the output size go through the M4
letterbox present draw instead of failing — and fails the run on
any validation-layer error. The bundled-fixture smoke runs as a ctest
(`example_corpus_sweep_smoke`); the full shader-pack corpus sweep runs
out of CI against a slang-shaders checkout.

Sample output (`demo-glow`, trimmed):

```
pass 0: shaders/demo_downsample.slang
  filter linear, wrap clamp-to-edge, mipmap off
  scale x: x0.5 source, y: x0.5 source
  alias 'blur', frame_count_mod 0, format rgba8
...
pass 1 shaders/demo_glow_combine.slang: OK
  vertex SPIR-V 1172 bytes (293 words), fragment SPIR-V 1428 bytes (357 words)
  set 2 textures: Original=0 blur=1
  parameters:
    GLOW = 0.600 (default 0.500, range 0.000..2.000 step 0.050) [preset]

result: 2/2 pass(es) compiled to SPIR-V — ready for the Vulkan runner.
```

## Building and testing

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `SLANGVK_BUILD_SHARED` (default `ON`, builds `libslangvk.so/dll`
alongside the static lib), `SLANGVK_BUILD_TESTS` (default `ON`),
`SLANGVK_BUILD_EXAMPLES` (default `ON`, builds `preset_info`,
`shader_demo` and `corpus_sweep`), `SLANGVK_ENABLE_VK` (default `ON`,
builds the Vulkan runner; `OFF` gives a core-only build) and the demo
options `SLANGVK_DEMO_WINDOW` / `SLANGVK_DEMO_FETCH_SHADERS` (see
[Shader demo](#shader-demo)). `SLANGVK_SANITIZE=address,undefined` is
available for test builds. Size levers (opt-in, off by default):
`SLANGVK_LTO` enables IPO + `-Oz` release codegen for the glslang-bearing
library, and `SLANGVK_SYSTEM_GLSLANG=ON` links a system-installed glslang
via `find_package(glslang CONFIG)` instead of the pinned FetchContent copy.
glslang, Vulkan-Headers, volk and VMA are fetched and built automatically
(SHA-256 pinned; see [PROVENANCE.md](PROVENANCE.md)).

## CI artifacts

CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) tests every
push and uploads the built libraries as workflow artifacts — the
per-toolchain distribution the plan prescribes for the C++23
same-toolchain-ABI API (`include/slangvk/export.h`):

- **`libslangvk-windows-x64-mingw`** — `libslangvk.dll` (glslang + volk
  linked in), `libslangvk.dll.a` MinGW import library, `libslangvk.a`
  static library, `include/slangvk/` headers. MSYS2 MinGW-w64 GCC build;
  link the DLL from MinGW-w64, not MSVC.
- **`libslangvk-linux-x64`** — `libslangvk.a`, `libslangvk.so`, headers;
  GCC Release + Debug builds gating the full ctest suite on lavapipe with
  the Khronos validation layer installed.

A sanitized (address + undefined) Linux test build runs alongside the
Release jobs — with synchronization validation enabled whenever the
Khronos layer is present. A `provenance` job runs
[`tools/scan_codefindings.py`](tools/scan_codefindings.py) (the plan §4
clean-room gate: RetroArch fingerprint scan) on every push. The Vulkan
runner tests execute against lavapipe on Linux; on Windows they skip
cleanly when no ICD is present and everything else still runs.

The full-corpus compile-sweep ledger runs separately:
[`.github/workflows/corpus.yml`](.github/workflows/corpus.yml) shallow-
clones `libretro/slang-shaders` nightly (or via workflow_dispatch), loads
and compiles every preset headlessly on lavapipe, and uploads the ledger
artifact — bootstrap mode until the first baseline ledger is pinned,
then the documented no-regression gate (plan §10) flips on.

## Documentation

- [docs/SPEC.md](docs/SPEC.md) — the preset-format contract (parsing,
  translation and execution semantics, section by section).
- [docs/PLAN.md](docs/PLAN.md) — milestones, API sketch, risk register.
- [CHECKLIST.md](CHECKLIST.md) — per-item M0–M4 implementation status,
  audited against the plan.
- [PROVENANCE.md](PROVENANCE.md) — upstream provenance, documented
  divergences, pinned dependency hashes and the corpus ledger.

## License

MIT — see [LICENSE](LICENSE). The plan (§4) defines the provenance policy:
clean-room against RetroArch's GPL slang implementation; no upstream
DevilutionX code; vendored dependencies limited to permissively licensed ones.
