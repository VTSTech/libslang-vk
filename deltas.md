# Fix Deltas — Closed Findings

**libslang-vk v0.2.0**

**Repository:** https://github.com/VTSTech/libslang-vk  
**Baseline audit:** [audit.md](audit.md) @ commit 249734b  
**Date:** 2026-10-21

This ledger tracks findings that have been **fixed and verified**. Each entry preserves the
original audit finding (title, severity, location) and records the resolution actually shipped:
what changed, in which files, and how it was verified. Once an entry appears here it is removed
from `audit.md` entirely — its master table, priority matrix, and counts list only the
open findings; this ledger is the single record of closures.

| ID | Severity | Category | Title | Closed |
|----|----------|----------|-------|--------|
| ROB-33 | Medium | Robustness | Synchronous render() contract limits performance and prevents batching | 2026-10-21 |
| ROB-34 | Medium | Robustness | No async execution or pipelining support | 2026-10-21 |
| ROB-35 | Medium | Robustness | Limited error recovery for Vulkan device lost scenarios | 2026-10-21 |

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
