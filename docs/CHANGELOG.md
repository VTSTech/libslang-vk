# Changelog

All notable changes to **libslang-vk** — a C++23 Vulkan runner for libretro
slang shader presets — are documented in this file.

Milestones follow the M-series plan in [PLAN.md §11](PLAN.md) (`M0` → `M5`).
Each milestone corresponds to the matching `M*` commit(s) on `main`, tagged
`M0` … `M4.08`. Format adapted from
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [M4.08] — 2026-10-24

The first iteration measured by its own benchmark suite: this release
ships a real benchmarking framework (google/benchmark, six executables,
built in-tree behind one flag), and the framework immediately paid for
itself — its first real-GPU run surfaced a double file-read in
`TranslateSlang`, fixed here and measured at **−18% per call on
Windows**. Two more audit findings close on the cheap: the preset
parser's edge-case contract is now pinned by tests, and the
thread-safety guarantees are stated in the public headers.

### Added

- **The benchmark framework** (closes the framework half of TEST-12):
  six google/benchmark v1.9.1 executables built with the library behind
  `-DSLANGVK_BUILD_BENCHMARKS=ON` — preset parse, slang translate, slang
  compile, chain create+destroy, blit render, FXAA render. The original
  suite (M0) had machine-specific build wiring and never produced a
  valid measurement outside its author's workstation; the rework links
  the in-tree `slangvk::slangvk` target (transitive glslang), fetches
  google/benchmark pinned via FetchContent, uses repo-relative data
  roots, wires in the orphaned `bench_vk_render_fxaa.cpp`, and makes
  every bench count its failures and print first-failure diagnostics
  instead of silently timing error paths. Runner scripts for Windows
  (VS2022/MSVC) and Linux ship in `benchmarks/scripts/`.
  [benchmarks/README.md](../benchmarks/README.md) documents the
  measurement contract and carries the first numbers: the lavapipe
  reference (2 vCPU sandbox, GCC 14.2) and the first real-GPU run —
  **RX 560 / Windows 11 / MSVC 19.44, where an FXAA frame completes
  end-to-end in 411 µs (~2,400 FPS ceiling, 13.1× the software
  rasterizer) and blit presentation in 225 µs (5.1×)**.

- **Preset-parser edge-case tests** (audit ROB-36, test half): 23 checks
  pinning the parse contract on adversarial inputs — empty files
  (rejected at top level, legal as `#reference` targets), lines without
  `=`, unknown-key tolerance, duplicate keys (last wins), CRLF inputs,
  comment forms including quoted `#` literals, missing `#reference`
  targets, self/two-file reference cycles, the 9-vs-10-file
  `#reference` depth boundary, and the 8 MiB size cap (ROB-20).

- **Thread-safety contract in the public headers** (audit ARCH-04,
  documentation half): the full contract block at the top of
  `runner.hpp` and `@par Thread safety` notes on `Device`, `Image` and
  `Preset` — the library is single-threaded per Device by design;
  distinct Devices (separate Vulkan devices/queues) are independent and
  may be used concurrently; the core layer keeps no shared mutable state
  apart from the shader-root global (set it once at startup); glslang's
  process initialization is internally mutex-guarded, so concurrent
  `CompileSlang` calls are supported. The existing `Chain` and
  `SetShaderRootDir` notes predate this and now have company.

### Fixed

- **`TranslateSlang` reads the shader file once per call** — the first
  optimization surfaced by the benchmark suite above (the translate
  bench on Windows was paying for two file opens per call). The
  include-resolution walk now captures the raw top-level bytes it
  already read, and the source fingerprint (`sourceSize`/`sourceHash`)
  is computed from that capture — previously the file was opened a
  second time purely for the fingerprint (`ReadWholeFileBytes`).
  Measured: **1,364 → 1,119 µs per call on the RX 560 box (−18%)**,
  84.4 → 81.6 µs on the lavapipe sandbox (where a read costs ~1 µs).
  Byte-for-byte parity of all `SlangTranslation` fields verified across
  LF, CRLF, include-resolved and failure-path cases (the fingerprint
  remains a hash of the *raw* file bytes, CRLF included). The saving
  applies per translated shader — and per pass, since chain creation
  translates every pass shader.

### Audited

- **TEST-12 and ARCH-04 are removed from [audit.md](../audit.md) and
  recorded in [deltas.md](../deltas.md)** (the M4.07 closure
  convention): TEST-12 for the benchmark framework above (CI wiring is
  the one outstanding slice), ARCH-04 for the header contract above.
  **ROB-36 is half-closed the same way**: its test half (the 23-check
  slice above) is recorded in deltas.md as a partial entry, while the
  parser refactor keeps the finding in audit.md. Six findings remain
  open, all Low: ROB-36 (refactor half), PERF-08 (SPIR-V host-side
  cache), MAINT-22, MAINT-23, FEAT-06, TEST-11.

### Validation

- ctest **22/22 green** (sandbox: GCC 14.2, lavapipe), including the new
  parser checks; translate bench re-measured 82.6 µs (within noise of
  the 81.6 µs post-fix reference).
- The translate fix was verified with a dedicated parity probe over
  LF / CRLF / include-resolved / failure-path fixtures: identical
  `SlangTranslation` fields before and after the change.
- Windows: MSVC 19.44 Release build clean; all six benchmarks ran on the
  RX 560; the translate bench re-run after the fix produced the 1,119 µs
  figure above (640 iterations, 890 items/s).

## [M4.07] — 2026-10-21

The first M4.xx iteration after the M4.06 pre-release: the two Low findings
that the game hosts feel most directly — the per-frame allocation churn in
the render path and the C API's null-handle contract — plus the SPIR-V cache
enabler, which lets a host's own compile cache be correct by construction.

### Added

- **The SPIR-V cache enabler** (`PresetLoadOptions::spirvCache`) — the
  library keeps no disk cache (that is the host's business, and the
  reference host already has one), but it hands the host a **correct** key
  so the host's own cache cannot serve a stale shader:
  - the host is asked for a pass's compiled SPIR-V before the library
    invokes glslang, and is handed the result afterwards; a hit skips the
    compile;
  - the key is a SHA-256 over the exact GLSL handed to glslang plus the
    compiler's version and target environment. Hashing the compiler's
    *input* makes the key correct by construction — it changes exactly when
    the compiled output would, so a host caching by it can never serve a
    stale shader, not even when the preset includes other files (which a
    host-side hash of the `.slangp` alone would miss);
  - the compile environment moved into constants shared by the compile and
    the key, so the two cannot drift;
  - the C surface mirrors it (`slangvk_spirv_cache`, load/store function
    pointers);
  - `src/core/sha256.{hpp,cpp}` — a self-contained SHA-256, so the library
    gains no dependency for a cache key.

  `test_runner_cache` pins the contract: the host is asked before the
  compile, a hit skips it, the key is stable across loads, and **the key
  changes when the shader source changes**.

### Fixed

- **PERF-09 — per-frame allocations in the render path.** The render path no
  longer allocates per frame:
  - the command buffer belongs to the frame slot — allocated once and reset
    per frame, instead of a `vkAllocateCommandBuffers` +
    `vkFreeCommandBuffers` round-trip per frame;
  - the uniform block fill writes straight into the mapped UBO (no
    temporary vector, no copy);
  - the pass sizes, alias sizes, descriptor write arrays and the pipeline
    array are chain scratch: cleared and refilled, never reallocated;
  - the LUT sizes and the merged parameter overrides are cached and only
    rebuilt when the chain is rebuilt or a parameter is overridden.

  Measured with a malloc counter over the bundled fixtures (184 rendered
  frames): **130,806 → 129,177** allocations, i.e. **~8.9 fewer heap
  allocations per frame** in the library's own code.

- **SEC-07 — C API null-handle validation.** The C API checks the required
  Vulkan handles itself and names the offending one ("input->view is
  VK_NULL_HANDLE") rather than letting the C++ layer report a generic
  message. The optional handles (`input->image`, `target->image`) stay
  optional. The test now pins the contract for every entry point: a NULL
  pointer or a NULL required view is `SLANGVK_ERR_INVALID_ARGUMENT` with a
  message.

### Validation

- **Downstream host** — the M4.07 build was smoke-tested on the
  **DevilutionX-libslang-vk** fork: presets load, the shaders work, and the
  run holds **140+ FPS with FXAA.slangp**. This is the first frame-rate
  figure from real hardware rather than the software rasterizer, so the
  allocation work above is confirmed on a real GPU rather than only counted.
  (The other reference implementation, **Chocolate-Doom-VK**, is engine-locked
  to 35 FPS, so it exercises the shader path but its frame rate says nothing
  about the shader cost.)

### Changed

- **The README is now a landing page** (677 → 88 lines). The prose moved to
  [docs/STATUS.md](STATUS.md), the API examples to
  [docs/EXAMPLES.md](EXAMPLES.md), and the usage to [USAGE.md](../USAGE.md).
  The README now names the two reference implementations:
  **DevilutionX-libslang-vk** (the fork branch) and
  **Chocolate-Doom-VK** (`VTSTech/chocolate-doom-vk`).

### Audited

- SEC-07 and PERF-09 are removed from [audit.md](../audit.md) and recorded in
  [deltas.md](../deltas.md). 8 findings remain, all Low severity.

## [M4.06] — 2026-10-21

The robustness wave: the three Medium findings from the codebase audit land as
code. The frame path is split into record / submit / wait so a host can hold
a frame in flight, several frames can be pipelined at once, and a lost Vulkan
device can be rebuilt — together with the presets and chains that lived on it
— instead of ending the process.

### Added

- **Async render path** (`Chain::render_async` + `Chain::wait_async`) — the
  frame path is split into *record*, *submit* and *wait*, so a host can keep a
  frame in flight while it does its own CPU work.

  - `render_async(input, target, frame)` records and submits the frame, then
    returns a fence. It does not block on the GPU.
  - `wait_async(fence)` blocks until the frame retires, then releases the
    frame's fence and command buffer. The host must call it before the
    frame's output is consumed.
  - `Chain::render()` is now `render_async()` + `wait_async()` — one
    recording implementation serves both entry points, so existing callers
    are unaffected.

  ```cpp
  auto fence = chain.render_async(input, target, frame);
  // ... the host's own work ...
  chain.wait_async(*fence);   // before the output is presented or read back
  ```

  At most **one async frame may be in flight per chain**: the frame's texture
  bindings are written in place into the chain's shared descriptor sets, so a
  second frame cannot be recorded while the first is still executing.
  `render_async()` drains the previous frame first. (ROB-34, below, lifts this
  limit.)

- **Multi-frame pipelining** (`ChainOptions::framesInFlight`, up to 8) — a chain
  configured for N frames in flight keeps N frames outstanding on the GPU at
  once, so the GPU is not idle while the host prepares the next frame.

  - The chain's mutable per-frame state (the uniform buffers, the three
    descriptor sets per pass, and the present draw's descriptor set) moved into
    a per-frame slot, so concurrent frames never overwrite each other's
    parameters or texture bindings.
  - The output buffer a frame writes and the history ring slot it fills are
    derived from a logical frame index rather than a toggled write index, which
    keeps the `OriginalHistoryN` and feedback semantics correct when frames
    overlap. The history ring grows by one slot per in-flight frame so an
    in-flight frame cannot overwrite the slot another in-flight frame reads.
  - The in-flight frame's fence and command buffer live in the slot that owns
    it; a slot is reused only after its previous occupant retired, which is
    where the backpressure lives.
  - **Host contract:** a frame borrows its input image and its output target
    for the frame's duration, so frames that overlap in time need one input
    image and one output target each. With `framesInFlight == 1` one of each is
    enough, as before.
  - C API: `slangvk_chain_options` + `slangvk_chain_create_ex()` expose
    `frames_in_flight`; `slangvk_chain_create()` is unchanged.

- **Device-lost recovery** (`Device::recover`, `Device::generation`) — a lost
  Vulkan device can be rebuilt. `recover()` recreates the `VkDevice`, its
  command pool and the library's memory allocator from the same instance and
  physical device; `generation()` returns a counter that is bumped by every
  successful recovery, so objects can tell that the device they were built on
  is gone. An adopted device (`Device::adopt`) stays the host's to recreate
  and is reported as `unsupported_feature`.

  - `Preset::rebuildLuts(Device&)` re-uploads the preset's LUT textures on
    the new device. It is a const operation — the preset's pass data and
    parameters survive a device loss and are not reloaded.
  - `Chain::rebuild()` recreates the chain's pipelines, render passes,
    framebuffers, descriptor sets, UBOs and pass targets from its preset.
  - `Chain::render()` detects a device loss, rebuilds the device, the
    preset's textures and the chain, and then reports `vk_device_lost` to the
    host **without retrying the frame**: the host's own images died with the
    old device and the library cannot recreate them. The chain is left
    rebuilt; the host recreates its images and renders again.
  - The device keeps a registry of the objects created on it (chains,
    presets, images) and releases them **before** swapping the device, so
    they are destroyed while they still belong to a live device. Objects
    remember the generation they were created on and forget their handles
    rather than destroying them through the new device.

  Recovery is best-effort: a driver that lost the device may also refuse to
  hand out a new one.

- **Precise device-loss detection** (`vk::VkErrorCode`) — a
  `VK_ERROR_DEVICE_LOST` result now surfaces as `Errc::vk_device_lost` at
  every call site, instead of the previous approach of sniffing the error
  *message* for the string "VK_ERROR_DEVICE_LOST" (which missed device losses
  surfacing as a generic `VkResult`). `Errc::device_lost_recovery_failed` was
  added for a failed recovery attempt.

- **C API**: `slangvk_device_recover()` mirrors `Device::recover()`, and
  `SLANGVK_ERR_DEVICE_LOST_RECOVERY_FAILED` keeps `slangvk_result` aligned
  with `slv::Errc` (the C enum mirrors it code-for-code).

- **CI: skip the build jobs for documentation-only commits** — the workflow
  diffs the commit range (three-dot, so a stale `before` still yields the
  real change set) and skips the heavy jobs when every changed file is
  `*.md`/`*.txt`/`*.rst`. No third-party action is involved; the range comes
  from the event payload and the checkout is a full clone so the diff has
  the history it needs.
- **CI: self-containment check** (`tools/check_self_contained.sh`) — every
  public header and every test translation unit is compiled on its own, with
  only the includes it declares. A translation unit that leans on another
  header pulling in `<optional>` (or `<string>`, ...) can build fine locally
  and fail on CI; the check turns that into a failure in seconds instead of
  at the end of a long build.

### Changed

- `SubmitAndWait()` frees its own command buffer instead of calling
  `vkResetCommandPool`: a pool-wide reset would also free command buffers
  that are still in flight on an async submission.
- The feedback double-buffer flip moved from "after the frame completed" to
  "after the frame was recorded", so the recorded commands and the next
  frame's descriptor writes agree on which buffer holds frame T-N.
- `Chain::create` documents that the preset it borrows must outlive the
  chain and must not be moved while the chain exists.
- Version bumped to 0.4.06 (major 0, minor 4, patch 6).

### Fixed

- **ROB-33** — synchronous render contract limitation: closed by the async
  render path.
- **ROB-34** — no async execution or pipelining support: closed by
  `ChainOptions::framesInFlight` and the per-frame resource slots.
- **ROB-35** — limited error recovery for device-lost scenarios: closed by
  `Device::recover()` and the automatic rebuild.
- **The descriptor pool's set budget** — the per-frame resources changed what
  a chain allocates (three sets per pass per frame slot, plus the empty and
  present sets), and the pool's `maxSets` was one short of that. The present
  set is allocated last, so the failure surfaced as "present descriptor set
  allocation failed" on drivers that enforce `maxSets` strictly. lavapipe
  hands out more sets than `maxSets`, so the local and CI suites did not
  notice. The chain now also counts the sets it allocates and refuses to
  exceed the pool's budget, so a mis-sizing fails the same way on every
  driver.

### Validation

- **Downstream hosts** — the M4.06 build was exercised against both reference
  implementations (**DevilutionX-libslang-vk** and **Chocolate-Doom-VK**):
  both load presets and render with the shaders working again. The descriptor
  pool budget fix below was found through those runs.

### Tests

- `tests/test_runner_async.cpp` — the async path: byte-identical output
  against the synchronous render, four back-to-back async frames, and
  `wait_async()`'s error contract.
- `tests/test_runner_pipeline.cpp` — the pipelining path: several frames
  outstanding on the GPU at once, overlapped frames producing byte-identical
  output to a serial render, and the history and feedback presets producing
  the same pixels pipelined as they do serially. It also builds a two-pass
  chain with 1, 3 and 8 frame slots, which is the case the descriptor pool
  budget got wrong.
- `tests/test_runner_recovery.cpp` — the recovery path: device replacement,
  the chain rebuilding itself, byte-identical output afterwards, further
  recoveries, and the release of stale objects.

### Audited

- The two findings are removed from [audit.md](../audit.md) and recorded in
  [deltas.md](../deltas.md) with what actually shipped.

## [M4.05] — 2026-10-18/20

The freeze-review + C-ABI + frame-path wave: the freeze-review findings
land as code and contract changes (process-global glslang frontend mutex,
`Chain::resize` dead parameters dropped, `docs/API.md` folded to match the
delivered surface), the library ships its first foreign-ABI surface — a
pure-C header, adapter and headless test suite — and the per-frame Vulkan
path drops its second submit + fence-drain cycle by folding the host
upload into the chain's own frame submit. Closes with the fix that keeps
the public header's only external dependency on the Vulkan headers.

### Added

- **C API (`include/slangvk/c.h` + `src/c_api.cpp`)** — the runner's first
  foreign-ABI surface: a pure-C header (`stddef.h`/`stdint.h` +
  `vulkan/vulkan.h` only, `extern "C"`, consumable from any FFI language)
  mirroring the C++ API 1:1. `slangvk_result` mirrors `slv::Errc`
  code-for-code, `slangvk_result_name()` names codes for logs and
  thread-local `slangvk_last_error()` carries the last failed call's
  message; opaque `slangvk_device` / `slangvk_image` / `slangvk_preset` /
  `slangvk_chain` handles; 25 entry points covering device `create`
  (library-owned) or `adopt` (host-owned instance/device/queue — the
  DevilutionX mode) plus `get_handles`/`get_info`/`wait_idle`/
  `read_pixels`, image `create`/`update`/`get_handles`, preset `load` +
  pass/parameter queries, and chain `create`/`set_parameter`/`resize`/
  `render_swapchain`/`render_offscreen`/`pass_output_size` — with the C++
  contracts carried over verbatim (input layout, preset outlives chains,
  single-threaded chains, resize before render after an extent change).
  `slangvk.hpp` pulls the header in; both `slangvk` and `slangvk-shared`
  build the adapter. `tests/test_c_api.cpp` (424 lines) drives the
  create → load → render → destroy loop headless at 64×64 and exercises
  the error model, skipping cleanly (exit 0) when no Vulkan ICD is
  present — CI runs it on lavapipe, Windows CI has none.
- **Single-submit per-frame upload path** (`Device::createMappedBuffer` /
  `destroyMappedBuffer`, `InputFrame::pixelBuffer`) — hosts doing their own
  per-frame upload no longer pay the standalone `Image::update` round-trip:
  its own staging alloc/free + command buffer + submit + fence drain on top
  of the chain's render submit. `createMappedBuffer()` allocates a
  persistently mapped host-visible staging buffer once at frame size; every
  frame the host memcpys into `mapped` and passes `buffer` as
  `InputFrame::pixelBuffer`, and the chain records the buffer→image copy at
  the top of its own frame submit (shader-read → transfer-dst → copy →
  shader-read barriers, before history blits and passes run). Per frame:
  one command buffer, one submit, one fence wait, zero staging churn —
  motivated by the DevilutionX integration, where the extra submit+drain
  cycle dominated the frame budget with cheap presets (a passthrough preset
  cost ~4.4 ms/frame over the no-shader path). `Image::update` stays for
  headless/test use.
- **shader_demo `r` / `reload`** — re-reads the current preset from disk and
  rebuilds its chain: the edit-slangp → reload → see-result loop without
  restarting the demo. The fresh parse goes into a side slot and the cache
  entry swaps only after the rebuilt chain replaced the old one, so the
  cache-outlives-chain invariant holds at every step and a failed re-parse
  leaves the previous preset + chain fully intact. Runtime parameter
  overrides are cleared by a reload (the chain is rebuilt from file
  values). First entry of the M4.04-post follow-up wave.
- **Process-global glslang frontend mutex** (freeze-review F-1, decision
  D-3a): `docs/API.md` §5.3/§6 claimed "glslang is process-globally
  guarded", but `GlslangRuntime` guarded lifecycle only —
  `InitializeProcess()` once, `FinalizeProcess()` at exit, no lock ever
  taken (the `<mutex>` include was dead weight). Two frontend sites ran
  unserialized — `CompileStageToSpv`'s `TShader::parse`/`link`/
  `GlslangToSpv` and the translate-time dead-code oracle's
  `TShader::preprocess` — so two concurrent `Preset::load`s could race
  inside glslang, with nothing in CI exercising concurrency. One
  process-global `std::mutex` (`GlslangFrontendMutex`, defined in
  `slang_compile.cpp`, forward-declared in `slang_translate.cpp` per the
  `EnsureGlslangRuntime` pattern) now serializes both entry points;
  seconds-scale loads make contention irrelevant and the written threading
  claim is true as published. API.md §4.4/§5.3 state the contract.

### Changed

- **`Chain::resize` drops its two dead input-size parameters**
  (freeze-review F-2, decision D-1a; pre-freeze signature break):
  `resize(inputW, inputH, outputW, outputH)` is now
  `resize(outputWidth, outputHeight)`. The review proved all three
  documented input-size clauses wrong — pass FBOs derive from the
  per-frame `InputFrame` size plus the stored target at render, and pass
  targets, the history ring and the mip copy are (re)created lazily on
  size drift; the impl only validated the output size and stored it (the
  input params were even commented out of the definition). Call sites were
  ours (`test_runner_m2.cpp`, `shader_demo.cpp`). API.md §5.4 rewritten to
  the lazy-rederive semantics; §8 gains the host duty that a swapchain
  extent change (window resize / `VK_SUBOPTIMAL_KHR`) must be followed by
  `resize()` with the new extent before the next render (F-3 — render
  rejects a drifted extent with `invalid_argument`).
- **`docs/API.md` folds the eight freeze-review findings** (Task 29):
  §3 names the library's only two stderr writers as documented exceptions
  and documents `SLANGVK_FILL_DEBUG` (F-8, decision D-4a); §5.4 states
  `setParameter` is write-only by design — no getter in v1, UI-facing
  hosts track the merged view from `parameterInfo()` + their own calls
  (F-2's getter gap, decision D-2b); §4.2 documents `ResolvePresetFilePath`
  refusing out-of-root hits as the deliberate SEC-01 sandbox behavior
  (F-5); §4.3 enumerates the 14 `UniformType` values (F-7); §5.5 carries
  the header default of `SwapchainFrame::format` (F-6) and the three
  undocumented render-time validations — offscreen target needs a concrete
  `ChainOptions::outputFormat`, `SwapchainFrame::format` `UNDEFINED`/
  zero-texel rejection, null `InputFrame::image` when history/mip is
  required (F-4); §9 cross-references the stderr exceptions.

### Fixed

- **`runner.hpp` builds without VMA on the include path.** The mapped-buffer
  API put `VmaAllocation` into the public header, whose only external
  include had been `vulkan/vulkan.h` (VMA is linked PRIVATE —
  `vk_mem_alloc.h` reached only the `src/vk/` TUs), so the first public
  consumer TU (`src/c_api.cpp`) failed the CI build with "'VmaAllocation'
  does not name a type". Fixed with a `VK_DEFINE_HANDLE(VmaAllocation)`
  forward declaration: it expands to the identical
  `typedef struct VmaAllocation_T* VmaAllocation` that `vk_mem_alloc.h`
  emits, so both include orders are legal identical-typedef redeclarations
  and the public header keeps its single Vulkan-headers dependency.
- **shader_demo `params` now shows live values.** It printed the preset
  default as the *current* value, so after `set BRIGHTNESS 3.0` the display
  still read `1.250` — indistinguishable from "the set didn't take" (the
  exact confusion behind the maintainer's live-parameter field report).
  Runtime overrides are now tracked per session and reported with a
  `[runtime override]` tag; the lib path itself was never broken —
  verified by byte-different PNG dumps before/after `set` on llvmpipe.
  Help text and both CI artifact USAGE blocks document `reload` and the
  file-edits-need-reload rule.

## [Unreleased]

### Planned

- M5 (1.0): API freeze, packaging, `docs/API.md`.
- The 21 ledger-FAIL presets are upstream-broken or malformed (see M4.03) —
  no library action planned; they stay pinned as FAIL in the baseline.
- Command-buffer pooling and timeline semaphores for the async path (the
  per-frame resource slots and `framesInFlight` are in; the pool and the
  timeline semaphores are not).

## [M4.04] — 2026-10-18/19

The conformance + hardware wave that closes M4: the two M3 leftovers ship
as one conformance/perf tool with the first real-GPU (RX 560) numbers and
pixel-level cross-backend verification, the fallback contract gains a
standing regression drill, `docs/API.md` lands as the M5 freeze baseline,
and the sweep's one real fork failure became a library fix — integer
samplers now join the canonical descriptor layout.

### Added

- **`docs/API.md` (draft)** — the full public-surface reference pulled from
  `include/slangvk/`: core + runner layers, `std::expected` error model,
  descriptor-convention table, lifetimes/threading rules, headless
  walkthrough, swapchain-mode contract, v1 limitation list and the M5
  freeze-review checklist. Marked pre-freeze; the M5 deliverable becomes
  "review + freeze against it".
- **Docs sync (2026-10-19):** CHECKLIST updated for the M4.03/M4.04 waves
  (25/25 evidence base, golden-image + perf-budget rows closed with the
  refined-scope notes, miss list #6/#7 closed, M5 boundary reflects the
  API.md draft); PLAN bumped to v0.3 (M3 fully ✅, §5.1 module map refreshed,
  §6 gains the delivered-API deltas paragraph, §10 documents the shipped
  conformance/budget tools); SPEC gained the M4.03 preprocessed-text
  sampling oracle (§4.5 + matrix + divergence #22) and the parameters-are-
  not-preprocessor-macros divergence (§4.2).
- **Golden-image conformance suite + perf budgets** — the two M3 leftovers
  ship as one tool, `examples/conformance.cpp` (M4.04 scope). `conformance
  golden` renders every preset in `tests/golden/manifest.txt` (20 bundled
  fixtures covering every chain semantic: blit, multi-pass, feedback
  ping-pong, history ring, mipmap refill, FBO formats, push-fragment
  uniforms, parameters, size uniforms, smoothness qualifiers, named /
  `User#` LUTs, LUT mipmaps, LUT size, rotation, include composition, the
  two demo presets) from a deterministic procedural testcard at a fixed
  target and compares the final frame against committed PNGs in
  `tests/golden/images/` — per-channel delta + mismatched-fraction
  tolerances per entry, `--update` regenerates, and the manifest's
  `# generated-by:` stamp names the backend that produced the goldens
  (a different local device warns once). `conformance perf` times
  `--timed` frames per preset after `--warmup` (wall-clock around the
  synchronous fenced `Chain::render`), writes a TAB `--report` ledger and
  gates `--budgets` files (`path<TAB>max-ms`, breach exits 1, unmeasured
  entries tolerated); `tests/golden/budgets-lavapipe.txt` documents the
  format with generous sandbox reference budgets. Both modes skip cleanly
  without a Vulkan device; ctests `example_conformance_golden` /
  `example_conformance_perf` (suite now 25/25); the CI shader-demo
  artifacts bundle `conformance(.exe)` with the golden kit and fixture
  presets so the suite runs from the artifact.
- **Hardware perf baseline (RX 560) + regression-guard budgets** — first
  real-GPU numbers committed alongside the sandbox references:
  `tests/golden/perf-rx560.txt` (Radeon RX 560 Series, Windows, 640×480
  from 320×240, warmup 3 / timed 20; all 20 fixtures measured twice via two
  roots — means 0.29–1.20 ms, 835–3,457 fps, every fixture ≥ 14× the 60 fps
  bar) and `tests/golden/budgets-rx560.txt` derived from it
  (`max(1.5 × mean, 1.0 ms)`; the duplicate-root runs put the tightest
  cross-run headroom at 38%, and observed single-frame spikes up to 4.3×
  mean stay invisible to the mean-of-20 gate). Opt-in on hardware —
  ctest/CI keep `budgets-lavapipe.txt`.
- **Fallback drill closed (CHECKLIST miss #11), both halves.** (a) *The
  event* — on the fork (Radeon RX 560, exe `996e5afcc`, 2026-10-19) the
  broken preset `test/feedback-noncausal.slangp` was rejected in 25 ms
  with the precise diagnosis (`slang: PassOutput0 is not causal in pass
  0`), the scaler session shut down cleanly into "GPU scaler unavailable,
  using the regular renderer instead", the error surfaced on the OSD, and
  the next session recovered end-to-end. (b) *The standing regression* —
  new `conformance drill` mode + `tests/golden/drill/` fixtures (7
  expect-fail cases across distinct failure layers: comments-only preset,
  missing shader, GLSL compile error, live-sampled unknown sampler,
  missing LUT, absent `#reference`, `PassOutput0` causality): each case
  must reject cleanly, leave zero validation messages, and let a
  pixel-identical recovery render of `runner/blit.slangp` succeed on the
  same device; surprises, poisoned devices or drifted frames exit 1
  (pass and fail directions both verified). ctest
  `example_conformance_drill` (suite 25→26); drill fixtures + a USAGE
  line ship in the CI shader-demo artifacts.
- **Hardware golden conformance (RX 560, PROVENANCE Session 7) + the
  demo-glow cross-backend tolerance** — first pixel-level comparison of
  the committed (llvmpipe-generated) goldens against a real GPU: 19/20
  within manifest tolerances, every passing entry at worst delta ≤ 3 —
  feedback ping-pong (worst 2 / 0.000%), mipmap refill (worst 3 /
  0.104%), history, FBO formats, LUTs, rotation, params and size
  uniforms all pixel-verified cross-backend (CHECKLIST M2 hardware
  halves closed). The one DIFF, `presets/demo-glow.slangp`, was
  root-caused to boundary-exact box taps at the fixture's exactly-0.5×
  downsample — deterministic per backend (byte-exact across repeated
  runs on both llvmpipe and the RX 560), so rounding, not a race — and
  now carries the manifest's only non-default tolerance (`200 / 12.0`,
  ~28% headroom over the measured envelope). `--update` + re-compare on
  the RX 560: 20/20 byte-exact. Committed goldens stay llvmpipe-
  generated; CI keeps its byte-exact baseline.
- **Hardware sweep, first shortlist batch (RX 560, PROVENANCE Session 8)
  + sharper pipeline errors.** Fork sessions verified the named
  `MainPassFeedback` reader form (`test/pragma-name`, ~1,166 frames) and
  the `TextGeneratePassFeedback` generate-and-cache feedback pattern
  (`misc/print-resolution`, visually confirmed) — with Session 7's
  numeric/mipmap results, every F2 feedback form is now
  hardware-covered. One real finding: `test/format.slangp` fails to
  load on the fork (`pass pipeline: VK_ERROR_UNKNOWN`) —
  **[attribution corrected in Session 9: the failing preset was the
  upstream corpus file (2-pass R32_UINT round-trip), not the 3-pass
  field fixture the golden suite covers; the root cause is the
  integer-sampler rewrite gap under Fixed below — not the present
  path, sRGB, or final-FBO formats]**. The fallback contract held
  (second failure class). Isolation kit shipped to the maintainer;
  diagnosis closed in Session 9. Library-side: pass/present pipeline
  and render-pass errors now name the pass index, shader path and
  target format (`pass 2 'shaders/fmt_srgb.slang': pass pipeline
  (format R8G8B8A8_SRGB): VkResult …`) via a new `FormatShortName`
  helper, so hardware logs identify the failing pipeline directly.
- **Integer FBO round-trip golden (`format-uint`, suite entry 21).**
  Verbatim port of the upstream corpus `test/format.slangp` pair at
  the pinned revision (`e1d75632`): pass 0 packs the testcard into an
  R32_UINT FBO (`uint` fragment output), pass 1 decodes through
  `usampler2D` + `texelFetch` + `unpackUnorm4x8` — the exact shape the
  fork field log exposed and the fix below targets. Byte-exact on
  llvmpipe; the RX 560's first golden run predates this entry and
  covers the original 20. 12 new `test_slang_translate` assertions pin
  the rewrite behavior (re-home + type preservation + orphan-drop
  parity for `usampler2D`/`isampler2D`).

### Fixed

- **Integer samplers never joined the canonical descriptor layout**
  (PROVENANCE Session 9). `RewriteSamplers` re-homed only float
  `sampler2D` declarations — the regex could not match
  `usampler2D`/`isampler2D`, and the scan index deliberately skipped
  them — so an explicit source-level `layout(set = 0, binding = 1)` on
  an integer sampler reached SPIR-V unmodified while the chain's
  canonical pipeline layout provides an empty set 0 and hosts textures
  at set 2. Observed symptoms: llvmpipe segfaults the host at draw;
  the AMD Windows driver (RX 560, fork field log Session 8) returns
  `VK_ERROR_UNKNOWN` from `vkCreateGraphicsPipelines` when it validates
  the shader-layout interface. The entire 2,658-preset corpus contains
  exactly one integer-sampler file — upstream `test/decode-format.slang`
  — which is also the only file the corpus-wide no-regression gate
  needed to prove safe (gate PASSED at the pinned revision; MBZ
  MEGATRON + crt-royale spot-checked). Fix: the rewrite regex matches
  `(?:u|i)?sampler2D` and re-emits the matched type at the canonical
  set/binding; the declaration scans and directive classifier treat
  integer samplers on par with float ones; `stage.textures` records
  them identically (combined-image-sampler descriptors are
  type-agnostic).

## [M4.03] — 2026-10-18

Dead-code texture-reference fix: the corpus verdict moved from **2,567 LOAD /
91 FAIL** to **2,637 LOAD / 21 FAIL — zero regressions** at the pinned corpus
revision.

### Fixed

- **Sampled-but-unbound textures behind preprocessor dead code** (70 presets:
  Mega Bezel NO-REFLECT variants' `ReflectionPass`, uborder
  `ambi_temporal_pass` wrappers, ...): the sampler rewrite ran its
  sample-site check on the raw include-resolved text, so a name sampled only
  inside conditional blocks the GLSL preprocessor discards
  (`#ifdef IS_NO_REFLECT_PRESET` wraps both the ReflectionPass declaration
  and its sampling in NO-REFLECT variants) hard-failed the load although
  glslang discards every use. `RewriteSamplers` now answers the "is it
  sampled?" question on a glslang-preprocessed copy of the stage text — the
  exact text and environment `CompilePass` feeds glslang, with no host
  defines injected (injecting anything would diverge from the real compile
  and could flip dead code live). A name still sampled after preprocessing
  remains a hard failure; a dead-sampled orphan declaration drops with a
  warning, matching RetroArch. The oracle is computed lazily (at most one
  preprocess per stage, only on the old failure path — healthy presets pay
  nothing), and a preprocess failure falls back to the raw verdict, which is
  the pre-fix behavior.

### Tests

- `test_slang_translate` dead-code cases: dead `#ifdef` and dead `#if`
  shapes (dead declaration + dead sampling; live declaration + dead
  sampling) translate with no bindings emitted and drop warnings reported;
  the same conditional shape with a live gate still hard-fails with
  `unknown texture reference`.

### Changed

- `corpus/baseline-ledger.txt` refreshed: 2,658 presets → **2,637 loaded /
  21 failed**. The remaining 21 are not library failures: 6 koko-aio
  `refs/` parameter-only overlays (no passes), 4 Adamant tilde variants
  (malformed shader indices), 6 nedi bilateral-variants (vhs.slang samples a
  `play` LUT the preset never declares), 1 crt-super-xbr (`InfoCachePass`
  absent from the mini-chain), 1 append-uborder-koko-ambi (template missing
  its appended chain), 1 technicolor + 1 gbc-frontlit (stray-quote malformed
  keys), and the corpus's own intentionally-invalid
  `test/feedback-noncausal`.

## [M4.02] — 2026-10-18

Corpus rescan release: full-corpus verdict went from **826 LOAD / 1832 FAIL**
(2026-10-17 baseline) to **2567 LOAD / 91 FAIL — zero regressions** at the
pinned revision.

### Fixed

- **`#reference` merge rebase stored cwd-relative paths** (`RebaseReferenceFilePath()`
  in `src/core/preset_parser.cpp`): when the scan root was relative (the
  documented `corpus_sweep slang-shaders` invocation), rebased pass/texture
  paths were joined onto the *importing* preset's directory at load time,
  producing garbled paths and failing ~700 otherwise-healthy presets. Rebased
  paths are now absolutized inside the SEC-01 shader root. Regression test in
  `tests/test_preset_parser.cpp` (relative root + `#reference` chdir fixture
  asserts an absolute, existing merged path).

### Added

- `corpus_sweep --only-from FILE` whitelist for chunked rescans (keeps
  corpus-root semantics; enables full-corpus sweeps in sandboxed/short-lived
  environments).
- CI: the Windows `shader-demo` artifact now bundles `corpus_sweep.exe` (Linux
  ships `corpus_sweep`), with an updated USAGE.txt section showing the
  absolute-root invocation.
- `.github/workflows/corpus.yml` `workflow_dispatch` can regenerate the ledger
  without any local binary.

### Changed

- `corpus/baseline-ledger.txt` refreshed at the same corpus rev: 2658 presets
  → **2567 LOAD / 91 FAIL, 0 regressions, +1741 improved** (bezel +1367,
  presets +160, hdr +146, crt +32, ...). Added/gone: 0/0.

## [M4.01] — 2026-10-18

### Fixed

- **`slang: unknown texture reference 'PhosphorPassFeedback'`** on presets
  whose passes are named only via `#pragma name` (no `aliasN` — e.g.
  `crt-yah.slangp`): the translator's texture-name universe only knew LUT
  names, `aliasN` and `User#`, while the chain runtime already resolved pragma
  names, `Feedback`-suffix taps and `<name>Size`. Pass names from
  `#pragma name` are now collected at load time and seeded into the universe,
  so bare-name taps, previous-frame `<name>Feedback` taps and `<name>Size`
  UBO members all bind.

### Added

- Public API `slv::preset::CollectSlangPragmaNames(shaderPath, out)`
  (`include/slangvk/core/slang_translate.hpp`): soft-fail include-once scan
  with the same `ResolveIncludes` bucketing as `TranslateSlang`; scan failure
  is silent and real errors surface from per-pass translation.

### Tests

- Four feedback-tap regressions in `tests/test_slang_translate.cpp`: collector
  finds the pragma name; seeded universe binds `PhosphorPassFeedback` in set 2;
  lowercase spelling resolves; empty universe still hard-fails with the
  original diagnostic.

## [M4] — 2026-10-17/18

DevilutionX presentation spike (plan §8 mode (a)) plus the `shader_demo`
example app and its first two real-world fix waves.

### Added

- **`examples/shader_demo`**: hybrid windowed/headless demo — GLFW backend
  probing with headless-only fallback, TUI preset browser, animated-pattern +
  image inputs, PNG frame writer, and a `--headless --no-menu --frames N --out
  DIR` sweep mode used for corpus verification (~2k lines across
  `shader_demo.cpp`, `window_target.hpp`, `input_source.hpp`,
  `preset_browser.hpp`, `frame_writer.hpp`).
- **Default input**: bundled `assets/testcard.png` (480×320 pixel-art
  Goku/Mario/Luigi, generated by `tools/make_testcard.py` + `tools/sprites/`),
  with a 5-level portable asset lookup starting at the executable's directory.
- **Corpus tooling**: `corpus/baseline-ledger.txt` (per-preset LOAD/FAIL
  baseline) + `.github/workflows/corpus.yml` — nightly pinned-rev sweep with a
  no-regression gate.
- Runner presentation: `SwapchainFrame` with letterbox and rotation;
  push-block / size-param runner coverage (`test_runner_m4.cpp`,
  `test_runner_pushblock.cpp`, `sizeparam/pushfrag` golden shaders).
- `docs/CHECKLIST.md`, `tools/scan_codefindings.py`, vendored
  `stb_image_write.h`; README CI badge + screenshot.

### Fixed

- **crt-consumer.slangp black screen**: UBO member parsing stripped `smooth`
  from `float smoothness;` via a left-token match, leaving member `ness`
  zero-initialized and the corner mask at 0. Added a right-token-boundary
  check plus a `SLANGVK_FILL_DEBUG` block-fill dump in `chain.cpp`, with a
  byte-exact golden regression (`tests/data/runner/smoothqual.*`).
- **analog-service-menu.slangp** `include target outside the shaders root`:
  the SEC-01 ancestor-retry now also fires on that error string (previously
  only on cannot-open).
- shader_demo defaulted to the animated pattern (characters never visible);
  it now defaults to the testcard image and falls back only if the asset is
  missing.

## [M3] — 2026-10-17

Conformance + perf wave (partial per PLAN.md §11).

### Added

- `src/vk/lut.cpp`: LUT texture upload paths (plain, mipmap, `<texture>Size`
  user bindings), golden runner3 presets (`lut`, `lut_mip`, `lut_size`,
  `lut_user` + PNG fixtures).
- `examples/corpus_sweep.cpp`: batch load/render sweep over a preset tree —
  the basis of the corpus baseline ledger.
- Chain/images/device hardening (+~350 lines) and parser fixes; CI updates.

## [M2] — 2026-10-17

### Added

- Chain semantics on the Vulkan runner, proven by the `test_runner_m2`
  canary suite: feedback ping-pong (previous-frame output), history ring taps
  (`OriginalHistory1/3`), pass formats (default/float/srgb), include/cycle
  resolution, mipmap refill (`textureLod`), FBO + history-ring recreation on
  input-size change, live `setParameter()`, validation-error gating, and a
  negative control (`PassOutput0` self-read must fail LOAD).

## [M1] — 2026-10-17

### Added

- Vulkan runner skeleton (`src/vk/` — `device.cpp`, `images.cpp`, `preset.cpp`,
  `chain.cpp` + VMA): headless golden blit on lavapipe with zero validation
  errors; runner test presets (`blit`, `param`).

### Fixed

- MSVC build fix; MinGW64 CI matrix additions (`libslangvk.dll` / `.a`
  artifacts).

## [M0] — 2026-10-17

### Added

- Scaffold: core trio ported clean — preset parser, slang→GLSL translator,
  slang compiler glue (`src/core/`, `include/slangvk/`); 12/12 tests green;
  field-preset round-trip fixtures (feedback / history / format / include
  cycles).
- CI: Linux tests on lavapipe + asan/ubsan build; Windows MinGW
  `libslangvk.dll` / `.a` artifacts; vendored CMake deps.
- Docs: `PLAN.md` (architecture + milestones), `SPEC.md` (libretro slang
  preset conformance scope), `PROVENANCE.md` (MIT + upstream ledger).

---

[Unreleased]: https://github.com/VTSTech/libslang-vk/compare/540c9d6...HEAD
[M4.05]: https://github.com/VTSTech/libslang-vk/compare/31519c8...540c9d6
[M4.04]: https://github.com/VTSTech/libslang-vk/compare/89963da...31519c8
[M4.03]: https://github.com/VTSTech/libslang-vk/commit/89963da
[M4.02]: https://github.com/VTSTech/libslang-vk/commit/cf91f52
[M4.01]: https://github.com/VTSTech/libslang-vk/commit/3a6ec45
[M4]: https://github.com/VTSTech/libslang-vk/compare/b7c6583...3a6ec45
[M3]: https://github.com/VTSTech/libslang-vk/commit/b7c6583
[M2]: https://github.com/VTSTech/libslang-vk/commit/8a02171
[M1]: https://github.com/VTSTech/libslang-vk/compare/f1bd2b8...aed9cef
[M0]: https://github.com/VTSTech/libslang-vk/commit/25ce03d
