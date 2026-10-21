# USAGE — libslang-vk

How to use libslang-vk: build it, consume it, run the tools, and wire it
into a host.

- The **API surface, by example** → [docs/EXAMPLES.md](docs/EXAMPLES.md)
- The **API reference** → [docs/API.md](docs/API.md)
- The **project status and history** → [docs/STATUS.md](docs/STATUS.md)

---

## Contents

- [Build and test](#build-and-test)
- [Embed the library](#embed-the-library)
- [The shader demo](#the-shader-demo)
- [Consuming the library](#consuming-the-library)
- [The example tools](#the-example-tools)
  - [preset_info](#preset_info)
  - [corpus_sweep](#corpus_sweep)
- [Conformance: golden images + perf budgets](#conformance-golden-images--perf-budgets)
- [CI artifacts](#ci-artifacts)
- [Troubleshooting](#troubleshooting)

---

## Build and test

glslang, Vulkan-Headers, volk and VMA are fetched and SHA-256-pinned
automatically, so no Vulkan SDK is needed at link time. Vulkan is loaded at
runtime through vendored [volk](https://github.com/zeux/volk) (no link-time
libvulkan); memory comes from vendored
[VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).
Hosts adopting a context keep owning their instance/device/queue:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `SLANGVK_BUILD_SHARED` (default `ON`, builds `libslangvk.so/dll`
alongside the static lib), `SLANGVK_BUILD_TESTS` (default `ON`),
`SLANGVK_BUILD_EXAMPLES` (default `ON`, builds `preset_info`,
`shader_demo`, `corpus_sweep` and `conformance`), `SLANGVK_ENABLE_VK`
(default `ON`, builds the Vulkan runner; `OFF` gives a core-only build) and
the demo options `SLANGVK_DEMO_WINDOW` / `SLANGVK_DEMO_FETCH_SHADERS` (see
[The shader demo](#the-shader-demo)).

## Embed the library

Four calls:

```cpp
auto device = slv::Device::create({.appName = "myhost"});
auto preset = slv::Preset::load(*device, "shaders/crt-royale.slangp");
auto chain  = slv::Chain::create(*device, *preset, {.outputWidth = 1280, .outputHeight = 720});
chain->render(inputFrame, outputTarget, {});   // once per frame
```

The same runner speaks C too — [`include/slangvk/c.h`](include/slangvk/c.h)
is an `extern "C"` mirror of the C++ surface (see
[docs/EXAMPLES.md](docs/EXAMPLES.md#the-c-api)).

## The shader demo

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
| `i`            | toggle input: `testcard.png` (default) ↔ animated pattern      |
| `w`            | write PNG (headless: render + dump now; windowed: screenshot)  |
| `l` / `list`   | list presets around the current one                            |
| `params`       | show the current preset's shader parameters (live values)      |
| `set NAME VAL` | override a parameter (applies on the next frame)               |
| `r` / `reload` | re-read the current preset from disk and rebuild its chain     |
| `q`            | quit                                                           |

Notes:

- The default input is the bundled `testcard.png` test card — your sprites
  show up at startup. The card is found next to the executable first (so CI
  artifact zips work anywhere), then via the build-time assets path and the
  working directory. `--input pattern` / `i` switches to the animated test
  pattern, whose frames go through `Image::update` every frame — the M4
  per-frame host-upload path a game host uses for its converted frame.
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

## Consuming the library

```cmake
include(FetchContent)
FetchContent_Declare(libslang-vk
    GIT_REPOSITORY https://github.com/VTSTech/libslang-vk.git
    GIT_TAG        main)
FetchContent_MakeAvailable(libslang-vk)

target_link_libraries(myapp PRIVATE slangvk::slangvk)        # static
# or slangvk::shared for the shared build
```

The shared build is **same-toolchain ABI only** (the API uses C++23
vocabulary types) — for a portable artifact, link the static library, or
consume per toolchain.

The host owns the Vulkan context in the adopt mode
(`slv::Device::adopt(...)`), which is what a game with its own renderer
does; the create mode (`slv::Device::create(...)`) is for headless tools
and CI. See [docs/EXAMPLES.md](docs/EXAMPLES.md) for both.

## The example tools

### preset_info

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

### corpus_sweep

[`examples/corpus_sweep.cpp`](examples/corpus_sweep.cpp) is the M3
conformance harness — and since M4.02 the baseline-ledger generator: it
walks preset directories through the real load path and reports a per-preset
ledger (parse/translate/compile/LUT upload), optionally executing chains on
the Vulkan device:

```sh
cmake --build build --target corpus_sweep
./build/examples/corpus_sweep path/to/shader-pack          # compile-only sweep
./build/examples/corpus_sweep pack --render --lut          # + render 2 frames per chain
./build/examples/corpus_sweep pack --fail-on-broken        # nonzero exit on failures
./build/examples/corpus_sweep pack --ledger new.txt --baseline corpus/baseline-ledger.txt
./build/examples/corpus_sweep pack --only-from list.txt    # rescan only the listed presets
```

`--baseline` diffs the fresh ledger against a pinned one and fails the run on
LOAD→FAIL regressions (improvements are reported as review material);
`--corpus-rev` stamps the shader-pack revision into the ledger header;
`--only-from` restricts the sweep to the newline-separated preset paths in
FILE while keeping corpus-root semantics — the chunked-rescan escape hatch
for slow machines. This is the tool behind
[`corpus/baseline-ledger.txt`](corpus/baseline-ledger.txt) (2,658 presets:
2,637 loaded / 21 failed at the pinned revision) and the nightly gate.

`--render` executes every chain through the real render path — presets
whose final pass does not resolve to the output size go through the M4
letterbox present draw instead of failing — and fails the run on
any validation-layer error. The bundled-fixture smoke runs as a ctest
(`example_corpus_sweep_smoke`); the full sweep runs in CI nightly — see
[CI artifacts](#ci-artifacts).

## Conformance: golden images + perf budgets

The two M3 leftovers ship as one tool, `conformance` — golden-image
rendering for visual regressions and per-frame wall-clock budgets for
performance regressions. Both modes skip cleanly (exit 0) when no Vulkan
device is available, like `corpus_sweep`.

**Golden images** render every preset listed in
[`tests/golden/manifest.txt`](tests/golden/manifest.txt) — 21 bundled
fixtures covering every chain semantic (blit, multi-pass, feedback
ping-pong, history ring, mipmap refill, FBO formats including the
upstream R32_UINT integer pack/unpack round-trip, push-fragment
uniforms, parameters, size uniforms, smoothness qualifiers, named / `User#`
LUTs, LUT mipmaps, LUT size, rotation, include composition, the two demo
presets) — from a deterministic procedural testcard (320×240) at a fixed
target (640×480, `--size WxH`), 3 frames per chain, and compare the final
frame against the committed PNG in `tests/golden/images/`. A pixel passes
when every channel differs by ≤ `max-delta` (default 2); the image passes
when the mismatched-pixel fraction is ≤ `max-mismatch-pct` (default 0.5%)
— same backend + driver is byte-exact in practice, the slack absorbs
cross-version LSB drift. The manifest's last header line,
`# generated-by:`, records the backend that produced the goldens; a
different local device prints a WARN once (cross-backend drift on real
GPUs is expected — regenerate with `--update` when adopting a new
reference backend). The suite's one non-default tolerance belongs to
`presets/demo-glow.slangp` (`200 / 12.0`): its exactly-0.5× downsample
puts every box tap on a source texel boundary, where linear filtering
rounds differently per backend (measured llvmpipe vs Radeon RX 560:
worst 156 on 8.7% of pixels, byte-exact within each backend). First
real-GPU validation, RX 560 2026-10-19: 19/20 against the llvmpipe
goldens (feedback worst 2, mip worst 3, the rest ≤ 2) and 20/20
byte-exact on-device after `--update` — see PROVENANCE Session 7.

```sh
cmake --build build --target conformance
./build/examples/conformance golden tests/data examples        # compare (ctest: example_conformance_golden)
./build/examples/conformance golden tests/data examples --update   # (re)generate goldens + stamp
```

**Perf budgets** load every preset under the roots (`--only-from FILE`
whitelist to restrict), render `--warmup` (default 3) + `--timed`
(default 20) frames and time each frame — `Chain::render` is synchronous
(one fenced submit per frame), so wall-clock around it is the honest
per-frame cost including submit + execute + fence wait. `--report FILE`
writes a TAB ledger (mean/min/max ms, fps, pass count) suitable for
committing and diffing across driver/hardware changes; `--budgets FILE`
gates the mean — `path<TAB>max-ms` lines, breach exits 1, presets absent
from the budgets file are informational only, budget entries whose preset
was not measured are tolerated (the corpus-ledger gate philosophy).
[`tests/golden/budgets-lavapipe.txt`](tests/golden/budgets-lavapipe.txt)
is the format reference (5× measured + 5 ms floor, sandbox-informational);
for your own GPU run `--report` there and set budgets from the measured
means — 16.6 ms is the 60 fps ceiling, mean × 1.5 a pure regression guard.
A committed real-GPU reference ships too:
[`tests/golden/perf-rx560.txt`](tests/golden/perf-rx560.txt) (Radeon RX 560
Series, Windows — every fixture 0.29–1.20 ms mean, 835–3,457 fps at
640×480) with the derived regression-guard budgets
[`tests/golden/budgets-rx560.txt`](tests/golden/budgets-rx560.txt)
(`max(1.5 × mean, 1.0 ms)` — the floor absorbs cross-run jitter on the
sub-0.5 ms fixtures; regenerate from a fresh `--report` after driver
changes).

```sh
./build/examples/conformance perf tests/data examples --report my-gpu.txt
./build/examples/conformance perf tests/data examples --budgets tests/golden/budgets-lavapipe.txt
./build/examples/conformance perf tests/data examples --budgets tests/golden/budgets-rx560.txt
```

The golden compare and a light perf smoke (no budgets — sandbox timing
must never flake the suite) run as ctests
(`example_conformance_golden`, `example_conformance_perf`); the CI
shader-demo artifacts bundle `conformance(.exe)` with `tests-golden/` +
`tests-data/` so the golden suite runs from the artifact too.

**Fallback drill** (`conformance drill`) is the library half of the
`(Unavailable)` fallback contract: every preset in
[`tests/golden/drill/manifest.txt`](tests/golden/drill/manifest.txt) must
FAIL to load, each exercising a distinct failure layer (comments-only
preset → parser; missing shader file; GLSL compile error; live-sampled
never-declared sampler; missing LUT image; absent `#reference` target;
`PassOutput0` sampled in pass 0 — the exact class a fork session hit on
hardware, see PROVENANCE). Per case the drill requires a clean rejection
with a non-empty error, zero validation-layer messages, and a
pixel-identical re-render of a known-good preset (`runner/blit.slangp`)
on the same device — a loaded "broken" preset, a poisoned device or a
drifted recovery frame all exit 1.

```sh
./build/examples/conformance drill tests/golden tests/data   # ctest: example_conformance_drill
```

```sh
# from a CI shader-demo artifact (fixtures ride along in tests-golden/):
./conformance drill --manifest tests-golden/drill/manifest.txt tests-golden tests-data
```

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
- **`shader-demo-linux-x64` / `shader-demo-windows-x64`** — the runnable
  demo (windowed GLFW build on Linux; headless-capable on Windows) bundled
  with `corpus_sweep` / `corpus_sweep.exe`, `conformance` / `conformance.exe`,
  the demo presets, `testcard.png`, the golden-image kit (`tests-golden/`:
  manifest + images + budgets) and the fixture presets (`tests-data/`) —
  the golden suite runs straight from the artifact with
  `conformance golden --manifest tests-golden/manifest.txt --golden-dir
  tests-golden tests-data .` — plus a USAGE.txt covering the tools. Assets
  resolve relative to the executable, so the zip runs from anywhere.

A sanitized (address + undefined) Linux test build runs alongside the
Release jobs — with synchronization validation enabled whenever the
Khronos layer is present. A `provenance` job runs
[`tools/scan_codefindings.py`](tools/scan_codefindings.py) (the plan §4
clean-room gate: RetroArch fingerprint scan) on every push. The Vulkan
runner tests execute against lavapipe on Linux; on Windows they skip
cleanly when no ICD is present and everything else still runs.

The full-corpus compile-sweep ledger runs separately:
[`.github/workflows/corpus.yml`](.github/workflows/corpus.yml) shallow-
clones `libretro/slang-shaders` at the revision pinned in the committed
baseline header (nightly, or via workflow_dispatch), sweeps every preset
headlessly on lavapipe, and diffs the fresh ledger against
[`corpus/baseline-ledger.txt`](corpus/baseline-ledger.txt) — with the
**no-regression gate ON since 2026-10-18**: a preset that loads in the
baseline but fails the fresh sweep fails the run, while FAIL→LOAD fixes are
tolerated and reported as review material. Refreshing the pinned baseline
itself is a deliberate, reviewed procedure documented in the workflow
header.

The artifacts are per-toolchain: the shared build is **same-toolchain ABI
only** (the API uses C++23 vocabulary types). The static library is the
portable artifact; the shared library is for a host that builds with the
same toolchain.

## Troubleshooting

**"no Vulkan device" / the tests skip.** The runner needs a Vulkan ICD.
Linux CI uses `mesa-vulkan-drivers` (lavapipe); Windows CI has none, so
the Vulkan runner tests skip cleanly there. Check with
`vulkaninfo` or run `ctest --test-dir build -R runner --output-on-failure`.

**"unknown texture reference" on a foreign preset.** A shader that samples
another pass's output as `<name>` (this frame) or `<name>Feedback` (the
previous frame) needs the translation's texture universe seeded with the
`#pragma name` pass names. `Preset::load` does this automatically; a host
rolling its own translate loop must call
`slv::preset::CollectSlangPragmaNames(shaderPath, names)` first.

**A preset fails to load with "path escapes the shader root".** The SEC-01
sandbox refuses a shader outside the shader root. The root is the first
preset's directory unless the host sets it: call
`slv::preset::SetShaderRootDir(dir)` once at startup when the host has
several preset trees.

**A cached shader looks stale.** If the host caches the SPIR-V itself, key
it on the library's content key (`core::ComputePassCacheKey`), not on the
`.slangp` — a preset that `#include`s other files would otherwise serve a
stale shader. See [docs/EXAMPLES.md](docs/EXAMPLES.md#the-spir-v-cache-enabler).

**The device was lost.** The chain rebuilds the device, the preset's
textures and itself, and reports `slv::Errc::vk_device_lost` — the host's
own images died with the device too, so recreate them and render again.
