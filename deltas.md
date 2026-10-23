# Fix Deltas — Closed Findings

**libslang-vk v0.4.06**

**Repository:** https://github.com/VTSTech/libslang-vk  
**Baseline audit:** [audit.md](audit.md) @ commit 249734b  
**Date:** 2026-10-21 (updated 2026-10-24 — M4.08 closures)

This ledger tracks findings that have been **fixed and verified**. Each entry preserves the
original audit finding (title, severity, location) and records the resolution actually shipped:
what changed, in which files, and how it was verified. Once an entry appears here it is removed
from `audit.md` entirely — its master table, priority matrix, and counts list only the
open findings; this ledger is the single record of closures.

Seven findings are closed: the three Medium ones from the robustness wave (ROB-33, ROB-34,
ROB-35) and four Low ones (SEC-07, PERF-09, plus the M4.08 wave: TEST-12, ARCH-04). One
further finding is **partially** closed and keeps its `audit.md` entry: ROB-36 (the
test half shipped; the parser refactor remains open).

| ID | Severity | Category | Title | Closed |
|----|----------|----------|-------|--------|
| ROB-33 | Medium | Robustness | Synchronous render() contract limits performance and prevents batching | 2026-10-21 |
| ROB-34 | Medium | Robustness | No async execution or pipelining support | 2026-10-21 |
| ROB-35 | Medium | Robustness | Limited error recovery for Vulkan device lost scenarios | 2026-10-21 |
| SEC-07 | Low | Security | C API lacks input validation for null handles | 2026-10-21 |
| PERF-09 | Low | Performance | Per-frame allocations in render path | 2026-10-21 |
| TEST-12 | Low | Testing | No performance benchmarking framework | 2026-10-24 |
| ARCH-04 | Low | Architecture | Single-threaded design limits concurrent use cases | 2026-10-24 |

---

## Detailed Findings (Closed)

### Robustness

#### ROB-33: Synchronous render() contract limits performance and prevents batching

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **File(s)** | `include/slangvk/runner.hpp`, `src/vk/chain.cpp`, `src/vk/impls.hpp`, `tests/test_runner_async.cpp` |

**Original finding.** The `Chain::render()` method is strictly synchronous — it records, submits, and fences a single command buffer per frame. While this simplifies memory management and ensures deterministic behavior, it prevents performance optimizations like command buffer batching, timeline semaphores, or parallel execution of multiple chains.

**Current behavior:** Each render call creates a new command buffer, submits it, and waits for completion before returning. This creates a submit-wait-submit-wait pattern that underutilizes the GPU and limits throughput.

**Resolution.**

The frame path was split into *record*, *submit* and *wait*, so a host can hold a frame in
flight while it does its own CPU work.

- **`Chain::render_async(input, target, frame) -> expected<VkFence, Error>`** — records and
  submits the frame, then returns a fence. It does not block on the GPU. The host may use the
  time between this call and `wait_async()` for its own work.
- **`Chain::wait_async(fence) -> expected<void, Error>`** — blocks until the fence is signalled,
  then releases the frame's fence and command buffer. The host must call it before the frame's
  output is consumed (presented, read back, or overwritten).
- **`Chain::render()` is now `render_async()` + `wait_async()`** — one recording implementation
  serves both entry points, so existing callers are unaffected.
- **`Device::Impl::RecordCommandBuffer()` / `SubmitRecorded()`** — the new device-level
  primitives; `SubmitAndWait()` had collapsed record → submit → wait into one call.
- **`SubmitAndWait()` frees its own command buffer** instead of calling `vkResetCommandPool`.
  A pool-wide reset also frees command buffers that are still in flight on an async submission.
- **The feedback double-buffer flip moved from "after the frame completed" to "after the frame
  was recorded"**, so the recorded commands and the next frame's descriptor writes agree on
  which buffer holds frame T-N.

**Contract and limits (stated rather than glossed over).**

- At most **one async frame may be in flight per chain** — the frame's texture bindings
  are written in place into the chain's shared descriptor sets, so a second frame cannot
  be recorded while the first is still executing. `render_async()` drains the previous
  frame first; the host still gets real CPU/GPU overlap between the two calls.
  **This limit was lifted later the same milestone by ROB-34** (per-frame resource slots
  and `ChainOptions::framesInFlight`), which is the follow-on finding this entry
  pointed at.
- **Not implemented** (and therefore not claimed): command-buffer pooling, timeline
  semaphores, and cross-chain pipelining.

---

#### ROB-34: No async execution or pipelining support

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **File(s)** | `include/slangvk/runner.hpp`, `include/slangvk/c.h`, `src/vk/chain.cpp`, `src/c_api.cpp`, `tests/test_runner_pipeline.cpp` |

**Original finding.** The library lacks any form of asynchronous execution or pipelining. All operations are blocking, which prevents overlapping CPU and GPU work and limits performance in scenarios where multiple frames could be in flight simultaneously.

**Current behavior:** The render pipeline is strictly linear: input → uniform fill → pass execution → output. There's no way to pipeline multiple frames or overlap CPU preparation with GPU execution.

**Resolution.**

ROB-33's async path could only keep **one** frame in flight per chain: the frame's
parameter values and texture bindings live in uniform buffers and descriptor sets that
the next frame overwrites in place. Multi-frame pipelining therefore needed those
resources to be per-frame.

- **`ChainOptions::framesInFlight`** (1..8, default 1) — how many frames the chain may
  have in flight at once. Each additional frame costs one more set of framebuffers,
  uniform buffers and descriptor sets per pass.
- **Per-frame resources** — the chain's mutable per-frame state (the uniform buffers, the
  three descriptor sets per pass, and the present draw's descriptor set) moved into a
  `Chain::Impl::FrameSlot`; one slot per frame in flight. A frame's parameters and
  texture bindings are therefore private to it, and concurrent frames never overwrite
  each other.
- **A logical frame index** — the output buffer a frame writes and the history ring slot
  it fills are derived from a monotonically increasing frame index instead of a toggled
  write index. That keeps the "N frames ago" semantics of `OriginalHistoryN` and the
  feedback reader correct when frames overlap: a feedback reader samples the previous
  frame's output, and the buffer it reads is not the one the current frame writes.
- **The history ring grows with the overlap** — it holds `depth + framesInFlight` slots,
  so an in-flight frame cannot overwrite the slot another in-flight frame is reading.
- **Slot ownership** — the in-flight frame's fence and command buffer live in the slot
  that owns it. A slot is reused only after its previous occupant retired, which is
  where the backpressure lives: the chain waits there when the host runs ahead.
- **C API** — `slangvk_chain_options` + `slangvk_chain_create_ex()` expose
  `frames_in_flight`; `slangvk_chain_create()` is unchanged (one frame in flight).
- **The descriptor pool was resized for the slots** — three sets per pass per
  slot, plus the shared empty set and the present set. The budget was one set
  short, and since the present set is allocated last the failure surfaced as
  "present descriptor set allocation failed" on drivers that enforce
  `maxSets` strictly (lavapipe does not, which is why the local and CI
  suites did not catch it). The chain now also counts the sets it allocates
  and refuses to exceed the pool's budget, so a mis-sizing fails the same
  way everywhere.

**Contract and limits (stated rather than glossed over).**

- **A frame borrows its input image and its output target for the frame's duration.**
  Frames that overlap in time therefore need one input image and one output target
  each — the next frame's upload would otherwise overwrite the previous frame's input
  while the GPU is still reading it. With `framesInFlight == 1` one of each is enough,
  as before. This is the one requirement the host has to meet; the chain cannot check it.
- The frames are submitted to the device's single graphics queue, whose submission
  order is what makes a frame's read of the previous frame's output safe.
- **Not implemented** (and therefore not claimed): timeline semaphores, per-frame
  command buffer pools, and frames in flight across *different* chains.

**Verification.**

- `tests/test_runner_pipeline.cpp` (new) drives the real path on lavapipe:
  - a chain with N frames in flight keeps N frames outstanding on the GPU at once;
  - an overlapped frame produces **byte-identical** output to a serial render;
  - the history and feedback presets — the two cases where the ring indexing matters —
    produce the **same pixels** pipelined as they do serially, frame for frame;
  - the host-side input/output requirement is exercised with one image and one target
    per in-flight frame.
- The pre-existing runner suite (26 tests) still passes unchanged, including the
  history-ring and feedback cases.
- ASAN + UBSAN builds are clean; the leak profile is unchanged from the pre-change
  baseline (240 bytes in 3 allocations, the same as the untouched runner tests).
- **Downstream hosts**: the M4.06 build was exercised against both reference
  implementations (**DevilutionX-libslang-vk** and **Chocolate-Doom-VK**) —
  both load presets and render with the shaders working again.

---

#### ROB-35: Limited error recovery for Vulkan device lost scenarios

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **File(s)** | `include/slangvk/runner.hpp`, `include/slangvk/c.h`, `src/vk/device.cpp`, `src/vk/chain.cpp`, `src/vk/preset.cpp`, `src/vk/images.cpp`, `src/vk/impls.hpp`, `src/vk/vk_internal.hpp`, `src/c_api.cpp`, `tests/test_runner_recovery.cpp` |

**Original finding.** The library handles Vulkan initialization failures well but has limited recovery mechanisms for runtime device lost scenarios. Once a device is lost, all associated objects become invalid, but the library doesn't provide a clear path to recovery or recreation.

**Current behavior:** Device lost errors (VK_ERROR_DEVICE_LOST) are reported through the Error mechanism, but there's no built-in way to recreate the device or associated objects while preserving application state.

**Resolution.**

**Precise detection.** `vk::VkErrorCode()` maps a `VK_ERROR_DEVICE_LOST` result to
`Errc::vk_device_lost` at every call site, so the chain can recognise a device loss from the
error code alone. The previous code sniffed the error *message* for the string
"VK_ERROR_DEVICE_LOST", which missed device losses surfacing as a generic `VkResult`.
`Errc::device_lost_recovery_failed` was added for a failed recovery attempt.

**Device-level recovery.** `Device::recover()` recreates the `VkDevice`, its command pool and
the library's VMA allocator from the same instance and physical device. `Device::generation()`
returns a counter that is bumped by every successful recovery, so objects can tell that the
device they were built on is gone. A device adopted from a host (`Device::adopt`) stays the
host's to recreate and is reported as `Errc::unsupported_feature`.

**Object-level recovery.** The chain's construction was split out of `Chain::create` into
`Chain::Impl::BuildAll`, and the destructor into `DestroyGpuResources`, so both can be re-run:

- `Preset::rebuildLuts(Device&)` re-uploads the preset's LUT textures on the new device. It is
  a **const** operation: the preset's observable state (pass data, parameters) survives a device
  loss and is not reloaded — only its GPU textures are refreshed.
- `Chain::rebuild()` recreates the chain's pipelines, render passes, framebuffers, descriptor
  sets, UBOs and pass targets from the preset it was built from.

**Automatic recovery.** `Chain::render()` detects `Errc::vk_device_lost` and rebuilds the
device, the preset's textures and the chain's resources. It then returns `Errc::vk_device_lost`
to the host **without retrying the frame**: the input and output images the host handed in died
with the old device, and the library cannot recreate them (it does not know their contents or
layout). The chain is left rebuilt and ready; the host recreates its own device-side resources
and renders again. Retrying with stale views would have been a crash.

**Release before replacement.** The device keeps a registry of the objects created on it
(chains, presets, images). `recover()` runs their release callbacks *before* swapping the
device, so the objects are destroyed while they still belong to a live device. This matters:
a destroyed device does not reliably reclaim the memory its objects used, so objects that were
merely dropped leaked — and destroying the VMA allocator with live allocations aborts in VMA's
assertion. With the registry the allocator is empty and can be destroyed properly. Owners
re-register after a release, so a rebuilt object is covered by the next replacement.

**Safe release of stale objects.** `Image`, `Chain` and `Preset` remember the device generation
they were created on. After a replacement their handles are forgotten rather than destroyed
through the new device, and `Image::update()` reports `Errc::vk_device_lost` instead of using
stale handles.

**C API.** `slangvk_device_recover()` mirrors `Device::recover()`;
`SLANGVK_ERR_DEVICE_LOST_RECOVERY_FAILED` keeps `slangvk_result` aligned with `slv::Errc`
(the C enum mirrors it code-for-code).

**Limits (stated rather than glossed over).**

- Recovery is **best-effort**: a driver that lost the device may also refuse to hand out a new
  one, in which case `recover()` reports the failure and the host is expected to re-create the
  whole `Device`.
- A real `VK_ERROR_DEVICE_LOST` cannot be provoked from a test (it needs a driver that faults
  the device), so the recovery path is driven through the same code path a device loss takes.

**Verification.**

- `tests/test_runner_recovery.cpp` (new) drives the recovery machinery end to end on lavapipe:
  - `Device::recover()` rebuilds the device and bumps the generation;
  - the chain notices that its resources belong to a replaced device and **rebuilds itself**
    before the next frame, producing **byte-identical** output;
  - two further recoveries work the same way;
  - releasing stale objects (image, chain, preset) after a replacement is clean;
  - the chain is usable again once the host has recreated its images.
- The pre-existing runner suite (26 tests) still passes unchanged.
- ASAN + UBSAN builds are clean. The recovery test's leak profile is **240 bytes in 3
  allocations — identical to the pre-existing baseline** of the untouched runner tests; before
  the release registry it was 2.3 MB in 327 allocations.
- `slangvk_result_name()` / `MapErrc()` cover the new error code, and the C API test exercises
  the new entry point's error path.

---

### Performance (partial — PERF-08 remains open)

#### M4.07 delivered for PERF-08: the SPIR-V cache enabler

PERF-08 ("no caching of compiled SPIR-V shaders") is **not closed** — the disk
store and the in-process memo are the host's side of the line — but the part
that only the library can do is now done.

**What shipped.**
- `PresetLoadOptions::spirvCache` — the host is asked for a pass's compiled
  SPIR-V before the library invokes glslang (a hit skips the compile), and
  is handed the freshly compiled bytes afterwards.
- The key is a **SHA-256 over the exact GLSL handed to glslang** plus the
  compiler's version and target environment. Hashing the compiler's *input*
  is what makes it correct by construction: the key changes exactly when the
  compiled output would. A host caching by it can never serve a stale
  shader — not even when the preset includes other files, which a host-side
  hash of the `.slangp` alone would miss.
- The compile environment (GLSL version, target client, target SPIR-V
  version) moved into constants shared by the compile and the key, so the two
  cannot drift.
- `src/core/sha256.{hpp,cpp}` — a self-contained SHA-256 (no new
  dependency), verified against the FIPS 180-4 test vectors.
- The C surface mirrors it: `slangvk_spirv_cache` with load/store function
  pointers.

**What stays open.** The disk store stays the host's (the reference host
already keeps `shaders/cache/`, and a second cache in the library would
disagree with it), so switching that host over to the library's key is
host-side work. The in-memory half — the same preset loaded twice in one
process still recompiles — also remains.

**Verification.** `tests/test_runner_cache.cpp` pins the contract: the host
is asked before the compile, a hit skips it (the host is never asked to
store), the key is stable across loads, and **the key changes when the
shader source changes**. The C test covers the same path, and a chain built
from a cached preset renders byte-identically to one built from a fresh
compile.

---

### Security

#### SEC-07: C API lacks input validation for null handles

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Security |
| **File(s)** | `include/slangvk/c.h`, `src/c_api.cpp`, `tests/test_c_api.cpp` |

**Original finding.** The C API accepts Vulkan handles (VkImage, VkBuffer, VkImageView) as raw pointers but performs minimal validation for null values before passing them to Vulkan calls. While the C++ API uses std::expected for comprehensive error handling, the C API relies on Vulkan's internal validation, which may produce less diagnostic-friendly error messages.

**Current behavior:** Device lost errors (VK_ERROR_DEVICE_LOST) are reported through the Error mechanism, but there's no built-in way to recreate the device or associated objects while preserving application state.

**Resolution.**
- The C API now checks the **required Vulkan handles itself** — the input frame's
  `view`, the swapchain frame's `swapchain_image_view` and the offscreen
  target's `view` — before the call reaches the C++ layer, and names the
  offending handle in the message ("input->view is VK_NULL_HANDLE") instead
  of letting the C++ layer report a generic one.
- The **optional** handles stay optional: `input->image` is only needed by
  history/mipmap-input presets and `target->image` only by a feedback-read
  final pass, so the C API does not reject them (the C++ layer still
  decides).
- The contract is now **pinned by the test**: every entry point is called
  with NULL pointers and with NULL required views, and each must return
  `SLANGVK_ERR_INVALID_ARGUMENT` with a message. The handle checks assert
  the *message* names the handle, so a regression to the generic C++-layer
  text is caught too.

**Verification.**
- `tests/test_c_api.cpp` covers every entry point's NULL path (device,
  image, preset, chain — including both render entry points and their
  required view handles).
- The C API test passes unchanged otherwise, so the existing contract is
  intact.

---

### Performance

#### PERF-09: Per-frame allocations in render path

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Performance |
| **File(s)** | `src/vk/chain.cpp`, `src/vk/impls.hpp` |

**Original finding.** The render path performs several allocations per frame, including command buffers, descriptor sets, and temporary buffers. While these are necessary for the current architecture, they could be optimized for high-frequency scenarios.

**Current behavior:** Each render call creates new command buffers and descriptor sets. While Vulkan drivers optimize this pattern, there's still allocation overhead per frame.

**Resolution.**
- **The command buffer belongs to the frame slot.** It is allocated once and
  *reset* for each frame, instead of a `vkAllocateCommandBuffers` +
  `vkFreeCommandBuffers` round-trip per frame. The pool was already created
  with `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT`, so the reset is
  free of driver bookkeeping. (The descriptor sets were already per-slot
  after ROB-34; the audit's "new descriptor sets per frame" no longer
  applies.)
- **The uniform block fill writes straight into the mapped UBO.**
  `FillBlockBytes` took a `std::vector<uint8_t>` and the caller copied it
  into the UBO: one heap allocation and one copy per uniform block per
  frame. It now fills the caller's buffer in place, with
  `FillBlockSize()` used for the overflow check.
- **The per-frame vectors are chain scratch**: the pass sizes, the alias
  sizes, the descriptor write arrays and the pipeline array are cleared and
  refilled, never reallocated.
- **The static data is cached**: the LUT sizes (which cost a `"User<N>"`
  string each) and the merged parameter overrides only change when the
  chain is rebuilt or a parameter is overridden, so they are cached and
  invalidated at those points.

**Verification.**
- Measured with a malloc counter over the bundled fixtures (184 rendered
  frames): **130,806 → 129,177** allocations, i.e. **~8.9 fewer heap
  allocations per frame** in the library's own code.
- The whole runner suite (30 tests) passes unchanged, including the
  live-parameter cases that exercise the override cache's invalidation.
- ASAN + UBSAN builds are clean; the leak profile is unchanged from the
  pre-change baseline (240 bytes in 3 allocations).
- **Downstream host**: the M4.07 build was smoke-tested on the
  **DevilutionX-libslang-vk** fork — presets load, the shaders work, and the
  run holds **140+ FPS with FXAA.slangp**. The allocation work is therefore
  confirmed on a real GPU, not only counted on the software rasterizer.
  (The other reference implementation, **Chocolate-Doom-VK**, is engine-locked
  to 35 FPS, so it exercises the shader path but its frame rate says nothing
  about the shader cost.)

---

### Architecture

#### ARCH-04: Single-threaded design limits concurrent use cases

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Architecture |
| **File(s)** | `include/slangvk/runner.hpp`, `src/vk/device.cpp` |

**Original finding.** The library is designed as single-threaded per device, which limits its use in multi-threaded applications. While this simplifies the implementation, it prevents concurrent operations on the same device.

**Current behavior:** All operations on a Device are assumed to be single-threaded. There's no synchronization for concurrent access to the same device from multiple threads.

**Resolution.**
- The contract is now **stated in the public headers** — a full-contract
  block at the top of `include/slangvk/runner.hpp`: the library is
  single-threaded per Device *by design* (plan §5); a Device and
  everything created from it (Image, Preset, Chain, command submission)
  must be created, used and destroyed by one thread at a time, with any
  cross-thread synchronization supplied by the host; distinct Device
  instances (separate Vulkan devices/queues) are independent and may be
  used concurrently.
- `@par Thread safety` notes on **`Device`**, **`Image`** and
  **`Preset`** carry the per-class slice of the contract. `Preset` adds
  the nuance that it is immutable after load, so const access — chains
  rendering from a shared preset — is safe from multiple threads. The
  existing `Chain` and `SetShaderRootDir` notes predate this entry.
- The **core layer** is documented as stateless apart from the
  shader-root global (`g_shaderRootDir`): concurrent `ParsePreset`,
  `TranslateSlang` and `CompileSlang` calls are fine once the root is
  set; `SetShaderRootDir` must not race with parses (its existing note
  already says so).

**Contract and limits (stated rather than glossed over).**
- Nothing gained locks: the guarantee is exactly "one thread at a time
  per Device subtree". Hosts sharing an object across threads still
  need their own synchronization — the headers now say so instead of
  leaving it to be discovered.
- The claims were written after a statics audit of the core layer: the
  only cross-call statics in `slang_translate.cpp` / `slang_compile.cpp`
  are `const` (regex tables, type maps — thread-safe after init) or the
  mutex-guarded one-time `glslang::InitializeProcess()`. Concurrent
  `CompileSlang` calls are therefore supported, and the header says so.
- Thread-safety **annotations** (e.g. `guarded_by` attributes) and
  optional multi-threaded support were considered and are **non-goals**
  for a single-threaded-by-design runner; this closure is "documented",
  not "made thread-safe".

**Verification.**
- Docs-only change on top of the verified M4.07 code: the full test
  suite (22/22) stays green.
- The statics audit above was performed by reading every `static`
  declaration in `src/core/slang_translate.cpp` and
  `src/core/slang_compile.cpp` before writing the contract text.

---

### Testing

#### TEST-12: No performance benchmarking framework

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Testing |
| **File(s)** | `benchmarks/`, `CMakeLists.txt` |

**Original finding.** The library lacks a dedicated performance benchmarking framework to track performance regressions and measure optimization effectiveness. While the conformance tests include timing, they're focused on correctness rather than performance.

**Current behavior:** Performance is measured informally through conformance test timing and manual testing, but there's no systematic way to track performance over time or measure the impact of changes.

**Resolution.**
- Six google/benchmark (v1.9.1, pinned FetchContent) executables build
  with the library behind `-DSLANGVK_BUILD_BENCHMARKS=ON`: preset parse,
  slang translate, slang compile, chain create+destroy, blit render,
  FXAA render. The original M0 suite had machine-specific wiring and
  never produced a valid measurement off its author's workstation; the
  rework links the in-tree `slangvk::slangvk` target (transitive
  glslang), uses repo-relative data roots, wires in the orphaned
  `bench_vk_render_fxaa.cpp`, and gives every bench failure counters +
  first-failure diagnostics — so an environment problem reads as an
  error, not as a suspiciously fast number.
- Runner scripts for Windows (VS2022/MSVC, no Vulkan SDK needed) and
  Linux ship in `benchmarks/scripts/`.
- [benchmarks/README.md](benchmarks/README.md) documents the measurement
  contract and carries the reference numbers: lavapipe (2 vCPU sandbox,
  GCC 14.2) and the first real-GPU run (RX 560 / Windows 11 / MSVC
  19.44) — an FXAA frame completes end-to-end in **411 µs (~2,400 FPS
  ceiling, 13.1× the software rasterizer)**, blit presentation in
  **225 µs (5.1×)**.
- The framework's first catch is already part of this release: its
  cross-platform numbers surfaced the `TranslateSlang` double
  file-read, fixed in M4.08 (see the changelog).

**Verification.**
- All six benchmarks run green on both reference environments and
  produce stable numbers across rebuilds (translate 82.6 µs vs the
  81.6 µs post-fix reference on the sandbox).
- The suite builds clean under MSVC 19.44 / VS2022 and ran end-to-end
  on the RX 560 host.

**Limits (stated rather than glossed over).** CI wiring — running the
CPU benches on a CI runner and failing on regression thresholds — is
the one outstanding slice of the original recommendation; the
Vulkan-device benches stay local runs by nature.

---

### Robustness (partial — ROB-36 remains open)

#### M4.08 delivered for ROB-36: the parser edge-case test slice

ROB-36 ("preset parser has complex state machine with edge cases") is
**not closed** — the recommended refactor into smaller components is
untouched — but the finding's other half, "add more comprehensive unit
tests for edge cases and error conditions", is now done, and the
parser's contract on hostile input is pinned instead of folklore.

**What shipped.** 23 checks in `tests/test_preset_parser.cpp`
(`rob36:` labels), written after reading the parser so they pin
*actual* behavior, not wishes: empty files (rejected at top level with
the named "preset has no shader passes" error, yet legal as
`#reference` targets — the asymmetry the deep-chain merge relies on);
lines without `=` skipped; unknown keys inert; duplicate keys
last-wins; CRLF inputs identical to LF; comment forms (full-line,
trailing, non-`#reference` line-opening `#`) with a quoted `#` literal
surviving; missing `#reference` targets failing with the full
"referenced preset failed → cannot open preset file" cause chain;
self- and two-file reference cycles terminating in the named
"preset #reference depth limit exceeded" error; the depth boundary
itself (a 9-file chain parses, a 10-file chain rejects); and the
ROB-20 8 MiB size cap rejecting before any large allocation.

**What stays open.** The refactor recommendation: `preset_parser.cpp`
is still one ~960-line state machine. The new tests are the safety net
that will make that refactor reviewable when it happens.

**Verification.** The parser test binary reports ALL PASS (23 new
checks; 22/22 ctest overall) and the translate bench is unchanged
(82.6 µs, within noise of the 81.6 µs reference) — the tests pin
behavior without perturbing it.

---
