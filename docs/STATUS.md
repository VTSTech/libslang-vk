# STATUS — libslang-vk

The project's status, history and prose. The reference is
[docs/API.md](API.md); [USAGE.md](../USAGE.md) covers building, consuming
and the tools; [docs/EXAMPLES.md](EXAMPLES.md) is the API by example.

---

## Where the project is

**M4.07** — async rendering, frame pipelining, device-lost recovery, an
allocation-free render path and a SPIR-V cache enabler. All library-side
M0–M4 gates are green: **31/31 ctest**, byte-exact goldens, zero
validation-layer errors on lavapipe.

The M0 core suite is joined by the M1 runner skeleton (Device
adopt/create, offscreen `Chain::render`, golden blit round-trips), the M2
execution contract (history rings `OriginalHistory#`, feedback
double-buffering `PassFeedback#` / `<name>Feedback`, per-pass FBO formats,
`mipmap_input` refill, FBO recreation on resize, live parameters), M3 LUT
uploads, and — since M4 — swapchain presentation: the final pass draws into
an acquired swapchain image with the letterbox rect and orientation handled
by the library (plan §8 output mode (a)). M4.05 adds the pure-C API and
the single-submit per-frame upload path. M4.06 splits the frame path into
record / submit / wait, lets several frames be pipelined at once, and adds
device-lost recovery. M4.07 removes the per-frame allocations from the
render path and adds the SPIR-V cache enabler.

Per-item implementation status, audited against the plan:
[docs/CHECKLIST.md](CHECKLIST.md). Notable changes per milestone:
[docs/CHANGELOG.md](CHANGELOG.md). The open findings and their severity:
[audit.md](../audit.md); what has been closed and how it was verified:
[deltas.md](../deltas.md).

**The freeze is not imminent.** M4.xx keeps iterating until the surface is
ready — the 2026-10-19 freeze review
([docs/API-freeze-review.md](API-freeze-review.md)) is a baseline, not a
commitment, and the review will be redone against the then-current headers.

---

## The reference implementations

Two hosts consume the library; both were exercised against the M4.07
build.

### DevilutionX-libslang-vk

The DevilutionX fork's [`libslang-vk`](https://github.com/VTSTech/DevilutionX/tree/libslang-vk)
branch (plan §11 M4): `gpu_scaler` keeps its public interface and runs the
library's Vulkan runner **dual-path** with the SDL_GPU runner — the preset
browser, ini persistence, OSD overlay and the `(Unavailable)` fallback
continue unchanged on top (plan §9). Hardware field sweeps on Windows
(Radeon RX 560) and Linux (llvmpipe) are the spike's exit gate; the
sessions are logged in [PROVENANCE.md](../PROVENANCE.md).

The M4.07 build was smoke-tested there: presets load, the shaders work, and
the run holds **140+ FPS with FXAA.slangp** — the first frame-rate figure
from real hardware rather than the software rasterizer, so the M4.07
allocation work is confirmed on a real GPU and not only counted.

### Chocolate-Doom-VK

[VTSTech/chocolate-doom-vk](https://github.com/VTSTech/chocolate-doom-vk) —
a Chocolate Doom port with a Vulkan path, the second reference
implementation. The M4.06 and M4.07 builds were exercised against it
(presets load and render with the shaders working again; the descriptor
pool budget fix was found through these runs).

Its frame rate is **locked to 35 FPS by the engine**, so it exercises the
shader path but its frame rate says nothing about the shader cost — the
DevilutionX run above is the one that measures it.

---

## What the runner implements

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

M4.05 trims the per-frame sync cost for hosts that upload every frame:
`Device::createMappedBuffer()` hands back a persistently mapped staging
buffer — memcpy into it and pass it as `InputFrame::pixelBuffer`, and the
chain records the upload copy at the top of its own frame submit
(shader-read → transfer-dst → copy → shader-read barriers, before history
blits and passes run). One command buffer, one submit, one fence wait per
frame — the standalone `Image::update` round-trip's second submit + fence
drain disappears, along with the per-frame staging alloc/free.
`Image::update` stays for headless and test use.

M4.06 splits the frame path into record / submit / wait, so a host can keep
a frame in flight (`Chain::render_async` + `Chain::wait_async`) and several
frames on the GPU at once (`ChainOptions::framesInFlight`); a lost Vulkan
device can be rebuilt (`Device::recover()`), with the preset's textures and
the chain rebuilding themselves.

M4.07 removes the per-frame allocations from the render path — the command
buffer belongs to the frame slot and is reset, not reallocated; the uniform
blocks are filled in place in their mapped UBOs; the per-frame vectors are
chain scratch; the LUT sizes and the merged parameter overrides are cached —
and adds the SPIR-V cache enabler.

---

## Corpus verdict (M4.03)

The full libretro preset corpus — all 2,658 presets, pinned to the revision
recorded in the ledger header — is swept through the real load path, and the
resulting per-preset baseline
([corpus/baseline-ledger.txt](../corpus/baseline-ledger.txt)) is committed
and gated: **2,637 loaded / 21 failed, zero regressions** since the
2026-10-17 baseline (826 / 1832). The remaining 21 are upstream-broken or
malformed presets (parameter-only overlays, stray-quote keys, missing LUT
declarations) plus one intentionally-invalid fixture — not library failures;
each is enumerated in the changelog.

---

## The M-series milestones

| Milestone | What landed |
|---|---|
| **M0** | Repo scaffolding, the core parser and the provenance gate. |
| **M1** | The Vulkan runner skeleton: `Device` adopt/create, offscreen `Chain::render`, golden blit round-trips. |
| **M2** | The full chain semantics: history rings, feedback double-buffering, per-pass FBO formats, `mipmap_input` refill, FBO recreation on resize, live parameters. |
| **M3** | LUT uploads and the conformance harness (`corpus_sweep`, golden images, perf budgets). |
| **M4** | Swapchain presentation, letterboxing, input rotation, `Image::update`. |
| **M4.05** | The pure-C API, the per-frame upload path, the freeze review. |
| **M4.06** | Async rendering, frame pipelining, device-lost recovery. |
| **M4.07** | An allocation-free render path, the C API's null-handle contract, the SPIR-V cache enabler. |

The plan itself — milestones, the API sketch, the risk register — is
[docs/PLAN.md](PLAN.md).

---

## Design notes and the trade-offs behind them

**The core layer never touches Vulkan.** It consumes shader source and
preset files and emits ordered passes, resource bindings, parameter blocks
and SPIR-V blobs; the runner consumes exactly that IR. This is what keeps
the "Vulkan only" decision cheap to revisit (PLAN §5.1).

**Synchronous by default, asynchronous on request.** `Preset::load`
compiles with glslang inline and `Chain::render` fences; the async path
(`render_async` / `wait_async`, `ChainOptions::framesInFlight`) is what a
host opts into when it wants to keep frames on the GPU. A frame borrows
its input image and output target for its duration, so overlapping frames
need one of each.

**No disk cache in the library.** The reference host already keeps one
(`shaders/cache/`), and two caches would disagree. What the library
supplies instead is a **correct key** — a digest of the exact GLSL handed
to glslang plus the compiler's version and target — so a host caching by
it can never serve a stale shader, not even when the preset `#include`s
other files.

**Device loss is recoverable, in place.** `Device::recover()` rebuilds the
`VkDevice`, its command pool and the allocator; the preset re-uploads its
textures; the chain rebuilds its resources. The objects of the lost device
are released *before* the device is replaced, so they are destroyed while
they still belong to a live device.

**The provenance gate is the release gate.** `tools/scan_codefindings.py`
scans for RetroArch code fingerprints on every push; a violation fails CI
before it can reach a release. The core layer is a black-box port of the
reference host's behaviour, and every divergence is recorded in
[PROVENANCE.md](../PROVENANCE.md).

---

## Known limitations

The full format-level list is [docs/SPEC.md](SPEC.md) §6/§7. The ones
that affect an API consumer directly:

- **Cold preset loads are expensive.** The heaviest field preset took
  4.05 s to load (parse + translate + glslang ×25 + LUT uploads) on
  hardware; the warm-cache collapse is 196 → 73 ms. Warm `Preset` objects
  ahead of need, or serve the bytes from a host cache
  (`PresetLoadOptions::spirvCache`).
- **No hot reload.** Preset switching is replace-and-recreate; there is no
  `Preset::reload()`.
- **Parameters are not preprocessor macros.** `#if` on a parameter name is
  always dead code (SPEC §4.2) — pack authors porting RetroArch-conditional
  shaders must define the gate themselves.
- **Some shader features are hard errors:** vertex-stage textures, sampler
  arrays, more than one UBO per stage, unsized arrays (SPEC §7.9).
- **`OriginalHistory` depth is capped at 8** (the spec is unbounded; the
  corpus max is 7), and `FrameDirection` is constant `+1`.
- **Same-toolchain ABI only** for the shared build; the static library is
  the portable artifact.
- **No logging surface.** Load warnings surface through `Preset::load`
  under `strict` (as errors) or through the core layer's
  `SlangTranslation::warnings`; validation errors through
  `Device::validationErrors()`.

---

## Validation records

- **Corpus**: 2,637 / 21 at the pinned revision, gated in CI nightly.
- **Golden images**: 21 fixtures, byte-exact within a backend; first
  real-GPU validation on the RX 560 (2026-10-19) was 19/20 against the
  llvmpipe goldens and 20/20 byte-exact on-device after regeneration.
- **Hardware field log**: the DevilutionX sessions (Windows RX 560, Linux
  llvmpipe) are in [PROVENANCE.md](../PROVENANCE.md).
- **Downstream hosts**: the M4.07 build on DevilutionX-libslang-vk (140+
  FPS with FXAA.slangp) and on Chocolate-Doom-VK.
