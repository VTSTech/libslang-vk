# benchmarks/ — the libslang-vk benchmark suite

A google/benchmark-based performance suite for the library's key operations:
preset parsing, slang→GLSL translation, glslang→SPIR-V compilation, Vulkan
chain creation and per-frame rendering (plain and FXAA). Written by VTSTech
(Nigel Todman); tested, fixed and documented against the M4.07 tree in a
clean-room container (GCC 14.2, Debian 13, no Vulkan driver installed — the
original Vulkan numbers below are lavapipe/CPU; a first real-GPU run is
recorded further down).

---

## What each target measures

| Target | Measures | Device needed |
|---|---|---|
| `bench_preset_parser` | `Preset::parse` — one `.slangp` from `tests/data/runner/presets` (default: `blit.slangp`), full key/value + pass-structure parse | no |
| `bench_slang_translate` | `TranslateSlang` — one `.slang` file (default: `blit.slang`) translated standalone to per-stage GLSL (include resolve + descriptor rewrite) | no |
| `bench_slang_compile` | `TranslateSlang` + `CompilePass` — the full source→SPIR-V path for one shader | no |
| `bench_vk_chain` | `Chain::create` + destroy — pipelines, FBOs, descriptor sets, samplers for the blit preset at 640×480 | yes |
| `bench_vk_render` | `Chain::render` — one full frame (upload path excluded) through the blit chain, into device-owned images | yes |
| `bench_vk_render_fxaa` | `Chain::render` — one full frame through the bundled FXAA preset at 640×480 | yes |

## Building

The suite builds as part of the main project (added in this iteration; see
"Changes" below), so all library dependencies come in transitively:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSLANGVK_BUILD_TESTS=OFF -DSLANGVK_BUILD_EXAMPLES=OFF \
    -DSLANGVK_BUILD_BENCHMARKS=ON
cmake --build build
```

Binaries land in `build/benchmarks/`. google/benchmark v1.9.1 is fetched by
FetchContent (pinned, tests/GTest disabled) — no system libbenchmark is
needed, and the old machine-specific `/home/<user>/local/benchmark` paths
are gone.

## Running

```sh
# Core (no GPU needed)
./build/benchmarks/bench_preset_parser
./build/benchmarks/bench_slang_translate
./build/benchmarks/bench_slang_compile

# Vulkan (needs any Vulkan device; llvmpipe/lavapipe works)
./build/benchmarks/bench_vk_chain
./build/benchmarks/bench_vk_render
./build/benchmarks/bench_vk_render_fxaa

# JSON output for tracking
./build/benchmarks/bench_preset_parser --benchmark_format=json \
    --benchmark_out=results.json
```

### Environment knobs

| Variable | Default | Purpose |
|---|---|---|
| `SLANGVK_BENCH_PRESET_DIR` | `runner/presets` | subdir of `tests/data` the parser bench collects `.slangp` from |
| `SLANGVK_BENCH_SHADER_DIR` | `runner/shaders` | subdir of `tests/data` the translate/compile benches collect `.slang` from |
| `SLANGVK_BENCH_SHADER` | `blit.slang` | which shader file the translate/compile benches measure |

`blit.slang` is the default shader because it translates **standalone** —
it references only `Source` and no preset-context aliases. Shaders that
reference pass aliases (e.g. `blend2.slang` uses `pre` from
`twopass.slangp`) legitimately fail translation with an empty texture-name
set; a benchmark that silently timed failure paths would report garbage
(this actually happened — see "The failure-path trap" below).

## The measurement contract

Every benchmark in this suite follows the same rules, enforced in source:

1. **No silent failure paths.** Any in-loop operation failure increments a
   named counter (`parse_fail`, `translate_fail`, `compile_fail`,
   `create_fail`, `render_fail`); a non-zero counter aborts the run with
   `state.SkipWithError` and the first error message. A timing with an
   ERROR line is not a number; it is a bug report.
2. **First-failure diagnostics.** The first failure of each kind prints its
   full API error string to stderr, so a broken run explains itself.
3. **Deterministic subject selection.** File lists are sorted before
   picking (directory order is unspecified), and the shader subject is
   pinned by name via `SLANGVK_BENCH_SHADER` rather than "whatever sorted
   first".
4. **Real objects, not empty handles.** The render benches create
   device-owned input/output `Image`s with a generated RGBA8 pattern;
   `render()` with a null view is a documented hard error, not a workload.
5. **Setup outside the timing loop.** Device creation, preset load, image
   creation, chain creation and file scans are all outside `for (auto _ :
   state)`. The loop measures only the operation under test.

Known cost-shaping effects to keep in mind when reading numbers:

- **First-call glslang initialization is real money.** The first
  `CompilePass` on a process costs ~109 ms (glslang `InitializeProcess` +
  first compile) against ~1.0 ms steady-state. Benchmark means amortize
  that over all iterations; a host's *cold* preset load pays it once.
- **CPU time vs wall time on the render benches.** `Chain::render` is
  synchronous (submits + fence-waits), so wall time = CPU submit cost +
  GPU execution. On a software rasterizer the GPU side dominates (see the
  results table); on hardware the submit path is what you will mostly see
  for cheap chains. Read both columns.

## Results (reference sandbox)

Debian 13 container, 2 vCPU, GCC 14.2.0, CMake 4.4.4, Release (-O3),
libslang-vk M4.07 static build, Vulkan on **lavapipe** (mesa
26.1.6, software rasterizer, extracted locally — no GPU in the sandbox).
Numbers are for orientation and regression-sniffing, not for absolute
claims; hardware (RX 560 class) runs are the meaningful ones.

| Benchmark | Time | Notes |
|---|---|---|
| `BM_PresetParser` | **5.15 µs**/preset (~196k/s) | `blit.slangp`, 10-line preset |
| `BM_SlangTranslate` | **84.4 µs**/shader (~11.9k/s) | `blit.slang` standalone |
| `BM_SlangCompile` | **1,046 µs**/shader (~956/s) | translate + glslang→SPIR-V; ~84 µs of that is translate |
| `BM_VkChainCreate` | **1,341 µs**/create+destroy | blit chain, 640×480, llvmpipe |
| `BM_VkRenderIteration` | CPU **21.7 µs**, wall 1,150 µs/frame | blit; submit path vs software-GPU cost |
| `BM_VkRenderFXAA` | CPU **27.3 µs**, wall 5,394 µs/frame | FXAA ≈ 4.7× blit on llvmpipe |

Reading the render rows: the submit-side cost (21.7 µs blit, 27.3 µs FXAA)
is the library-side per-frame work on the M4.07 allocation-free path; the
wall time is dominated by llvmpipe's software execution of the shaders.
The FXAA-vs-blit submit delta (~5.6 µs) is the extra descriptor/push work
of the heavier chain; the wall delta is the shader cost.

Raw JSON for this run is archived alongside the project docs
(`download/bench/*.json` in the working copy that produced this README).

## Results — real GPU (first hardware run)

Radeon RX 560, Windows 11, 8 threads @ 3.6 GHz, VS2022 BuildTools
(MSVC 19.44, Release /O2), google/benchmark v1.9.1 — run by VTSTech,
2026-10-23. First numbers from a real driver + GPU; every prior run was
lavapipe. The FXAA log confirms the device
(`Device created: Radeon RX 560 Series`) and the render benches run on
Release binaries from `build/benchmarks/Release/`.

| Benchmark | Reference sandbox (lavapipe) | RX 560 / Windows | Reading |
|---|---|---|---|
| `BM_PresetParser` | 5.15 µs | 104 µs | dominated by per-iteration file-open tax (see below) |
| `BM_SlangTranslate` | 84.4 µs | 1,364 µs | file-open tax (×2 opens) + MSVC codegen; profile follow-up flagged |
| `BM_SlangCompile` | 1,046 µs | 3,831 µs | ~3.6× ≈ MSVC vs GCC on glslang-heavy code (I/O amortized) |
| `BM_VkChainCreate` | 1,341 µs | 2,514 µs | AMD Windows PSO compile heavier than lavapipe's; one-time per chain |
| `BM_VkRenderIteration` | CPU 21.7 µs, wall 1,150 µs | CPU 75.0 µs, wall **225 µs** | wall 5.1× faster — real GPU executes the pass |
| `BM_VkRenderFXAA` | CPU 27.3 µs, wall 5,394 µs | CPU 59.4 µs, wall **411 µs** | wall **13.1× faster** (~2,400 FPS ceiling incl. fence sync) |

Headline: an FXAA frame completes end-to-end (submit + execute + fence
wait) in **411 µs** on the RX 560 — a ~2,400 FPS ceiling for the shader
pass itself, versus 5.4 ms on a software rasterizer. Chain creation
being *slower* than lavapipe is expected: the Windows AMD driver does
real pipeline compilation, lavapipe does not.

### Why the CPU-side rows differ so much (read before comparing)

- **The parse/translate/compile benches hand the library a file path**
  inside the timed loop, and the library re-reads the file every
  iteration: `ParsePreset` opens it once per call
  (`src/core/preset_parser.cpp:317`); `TranslateSlang` opens it twice —
  once to translate, once to re-read and hash the bytes
  (`src/core/slang_translate.cpp:152`, `:1498`, `:1607`). On Windows
  every open pays NTFS path resolution plus the Defender filter port —
  roughly 0.1 ms (the parser row's 104 µs vs 5.15 µs, where a warm
  Linux open is 1–2 µs, is mostly this tax). These rows measure
  I/O + parse together, and the tax is a Windows-environment cost, not
  a library regression.
- **Toolchain gap:** the cleanest codegen-only estimate is the compile
  row (glslang-dominated, I/O amortized): ~3.6× MSVC vs GCC.
  Cross-OS CPU numbers are orientation only — compare like with like.
- **Render CPU per frame is higher on Windows** (75.0/59.4 µs vs
  21.7/27.3 µs): WDDM's user-mode submit path is heavier than
  lavapipe's in-process queue. The wall columns are the meaningful
  cross-machine metric.

**Optimization note found by this run:** `TranslateSlang`'s per-call
double read costs ~0.2 ms/iteration on Windows for an operation that
takes 84 µs of CPU on Linux. A buffer-based overload (read the file
once, hash the bytes already in memory) would remove one open and the
redundant read on every host, not just Windows.

## Changes in this iteration (vs the original M0 drop)

The original suite (commit `6ea23c4`) had machine-specific build wiring and
never produced a valid measurement outside its author's workstation. What
changed, by file:

**`CMakeLists.txt` (rewritten, same target names)**
- Built as `add_subdirectory(benchmarks)` behind
  `SLANGVK_BUILD_BENCHMARKS` (top-level option, default OFF) so benchmarks
  link the in-tree `slangvk::slangvk` target — the old standalone
  `-L/-lslangvk` link line bypassed CMake's transitive link interface and
  failed to link glslang (`undefined reference to
  glslang::InitializeProcess`).
- Removed hardcoded `/home/vtstech/...` cache paths; removed the invalid
  `find_package(benchmark REQUIRED EXCLUDE_FROM_ALL)` (google/benchmark's
  config target is `benchmark::benchmark`, and the deb-extracted config
  resolved its include prefix to `/include` when relocated).
- google/benchmark v1.9.1 via pinned FetchContent; GTest/tests/install off.
- Wired in the orphaned `bench_vk_render_fxaa.cpp` (existed on disk, was
  not in any build) and added it to `install()`.

**All benches**
- Removed `state.ResumeTiming()` calls that were never paired with
  `PauseTiming()` — a latent misuse that fires on the first in-loop
  failure.
- Guarded every directory scan with the `std::error_code` iterator form;
  the old range-for over `directory_iterator(dir)` threw an uncaught
  `std::filesystem::filesystem_error` and aborted the process when a
  directory was missing (the `SkipWithError` guard never got a chance).
- Deterministic subject picks (sort + named shader).
- Failure counters + first-failure stderr diagnostics + `SkipWithError`
  (the measurement contract above).
- `SetItemsProcessed` now counts what the loop actually did (the old
  `iterations × file_count` inflated `items_per_second` by the corpus
  size while the loop measured one file).

**`bench_preset_parser`** — scans `tests/data/runner/presets` (env
override) instead of the nonexistent `tests/data/presets`.

**`bench_slang_translate` / `bench_slang_compile`** — scan
`tests/data/runner/shaders` (env override) instead of the `tests/data`
root (which contains no `.slang` files); default subject `blit.slang`;
`bench_slang_compile` also aborts when translate fails (compile never
ran — see below).

**`bench_vk_chain`** — sets `outputWidth/outputHeight` (create failed with
"zero output size" on every machine, and the fixed `->Iterations(10)`
made the failure look like a 0.76 µs workload); data root now CWD-
independent.

**`bench_vk_render` / `bench_vk_render_fxaa`** — create device-owned
input/output images and render real frames; the old code passed
null-view `InputFrame{}`/`Offscreen` handles, which the M4.07 contract
rejects, so every render failed (silently, with the `ResumeTiming` bug on
top). Data root CWD-independent.

### The failure-path trap (case study)

The first "working" run of `bench_slang_compile` reported **506 µs** —
less than translate (~84 µs) plus any plausible glslang compile. The loop
was failing every iteration (`blend2.slang` references the pass alias
`pre`, unknown outside its preset) and timing the failure path. An
independent probe of `blit.slang` showed the true costs: ~84 µs translate,
~1.0 ms compile, ~109 ms first-call glslang init. The counters and the
named-shader default exist precisely so this class of bug cannot produce
a number again.

## Running the Vulkan benches without a GPU (lavapipe recipe)

Works in any container, no root needed — extract the Debian debs for
`libvulkan1`, `mesa-vulkan-drivers` and `libdisplay-info2` into a local
prefix, then:

```sh
export LD_LIBRARY_PATH=<root>/usr/lib/x86_64-linux-gnu
export VK_ICD_FILENAMES=<root>/usr/share/vulkan/icd.d/lvp_icd.json
./build/benchmarks/bench_vk_chain
```

`mesa-vulkan-drivers` also ships hardware ICD JSONs; pointing
`VK_ICD_FILENAMES` at `lvp_icd.json` selects the software device only.

## Known limitations / next steps

- The benches measure one fixed subject each (by design, for comparable
  numbers). A corpus-scale parse bench (all 2,658 libretro presets through
  `ParsePreset`) and a per-shader-size scaling sweep are natural follow-ups
  now that the harness is trustworthy.
- `bench_vk_chain` measures create+destroy together; splitting setup from
  teardown would isolate pipeline compile from resource allocation.
- Render benches are synchronous-path only; the M4.06+ async
  (`render_async`/`wait_async`, `framesInFlight`) path is unbenchmarked.
- Hardware numbers: the first RX 560 run is recorded above (2026-10-23).
  Remaining environment gaps: a Ubuntu-VM lavapipe run as a third
  reference column.
- From-memory variants of the parse/translate/compile benches would
  split the Windows file-open tax from library CPU (see the RX 560
  section) and make cross-toolchain numbers comparable.
- A buffer-based `TranslateSlang` overload (read once, hash the bytes
  already in memory) would remove the per-call double read on every
  host — flagged by the RX 560 numbers.
