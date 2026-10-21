# libslang-vk — Project Plan

| | |
|---|---|
| **Status** | v0.3 — living plan. Lib milestones M0–M4 complete — M3 closed 2026-10-18 (corpus ledger gate + golden-image conformance + perf budgets); DevilutionX adapter spike in progress (see §11 status) |
| **Date** | 2026-10-19 |
| **Author** | VTSTech · drafted with Super Z |
| **License** | MIT |
| **Repo (proposed)** | `github.com/VTSTech/libslang-vk` |
| **Language / Standard** | C++23 (matches DevilutionX fork's `CMAKE_CXX_STANDARD 23`) |
| **Source lineage** | `VTSTech/DevilutionX` — preset layer ported from `master` @ `96af9b5fc` (see PROVENANCE.md); adapter spike on branch [`libslang-vk`](https://github.com/VTSTech/DevilutionX/tree/libslang-vk) |
| **Audit baseline** | `docs/audit.md` round 16 (2026-10-17): 31 open findings — **0 High / 0 Medium / 31 Low** |

---

## 1. Executive Summary

**libslang-vk** is a standalone C++23 library that parses, translates, and compiles **libretro slang shader presets** (`.slangp` + `.slang`) and executes them as **Vulkan** render chains. It gives any project — DevilutionX first among them — access to the entire RetroArch shader ecosystem (CRT simulation, xBR, ScaleFX, HQx, Mega Bezel, interlacing/deinterlacing, and 2,600+ more) through a small, clean C++ API, distributed as both a static library and a shared object/DLL.

The library is an extraction, not a from-scratch rewrite. The DevilutionX GPU shader mod already contains a proven, GPU-agnostic preset layer — `preset_parser` (767 LOC), `slang_translate` (854 LOC), `slang_compile` (183 LOC) plus the `preset_types` data model — validated byte-for-byte against the full 2,658-preset libretro corpus during the fork's coverage campaign. That layer moves into libslang-vk largely intact. The one genuinely new component is a direct-Vulkan pass runner that replaces the fork's SDL_GPU runner, which stays behind in the mod.

Timing is deliberate. The codebase-audit ledger currently stands at **zero open High or Medium findings** — every silent-bug class in the preset stack (the inverted history ring, the order-dependent scale parse) has been fixed and regression-covered, the format contract is documented normatively in `docs/SLANG_SPEC.md`, and the audit's own strategic frame already names a standalone reusable shader-preset library as the long-term goal. Extracting now means extracting code whose semantics are pinned by tests, with the fork as a permanent hardware testbed and reference consumer.

## 2. Goals and Non-Goals

### Goals (v1.0)

1. **Full libretro slang spec support** — presets, `#pragma name/stage/parameter/texture`, defines, `#include` (with cycle detection), pass aliases (`source0..`), feedback passes, `PREV` / `PASSPREV` / `OriginalHistory#` frame history, wrap/mipmap/filter modes, all scale types (source/viewport/absolute, fractional), FBO format selection (RGBA8, RGBA16F, R16F, sRGB, …), and `max_fbo_*` clamping.
2. **Direct Vulkan execution** — no SDL, no SDL_GPU, no intermediate abstraction layer. Vulkan 1.1 floor everywhere (classic synchronization — the dynamic-rendering fast path is a recorded post-1.0 lever, §8/§13).
3. **Drop-in host integration** — the host keeps ownership of its window, surface, swapchain, and fallback rendering. The library adopts host-provided `VkInstance`/`VkDevice`/queue or creates its own; input frame in, presented frame out.
4. **C++23 API** — namespace `slv`, `std::expected`-based error handling, RAII handles. No C API in v1.
5. **Conformance as a feature** — an automated CI sweep that loads and compiles the entire libretro corpus, plus golden-image tests, with a no-regression ledger policy inherited from the fork's audit discipline.
6. **Dual distribution** — CMake targets for both static and shared builds.

### Non-Goals (v1)

- Other rendering backends (OpenGL, D3D, Metal). Vulkan only — by explicit decision. The module boundary between spec-processing and execution keeps other backends *conceivable* without designing for them.
- C ABI and non-C++ language bindings.
- On-disk SPIR-V cache. v1 compiles with bundled glslang at preset load and caches only in memory for the lifetime of a `Preset` object. Disk cache is a post-1.0 candidate (see §13).
- Shader authoring tooling and live preset hot-reload.
- macOS native support. Vulkan-only means macOS is reachable only through MoltenVK, which is an optional, unsupported tier in v1 (the fork's SDL_GPU path continues to serve Metal in the meantime).

## 3. Background: What Exists Today

The fork's shader stack is organized in five layers under `Source/engine/render/gpu/`, and their disposition under this plan is the core of the extraction strategy:

| Fork module | LOC | Role | Disposition |
|---|---|---|---|
| `preset/preset_types.hpp` | — | Data model: `Preset`, `PassDef`, `TextureDef`, scale/wrap/filter enums | **Port** to `src/core` (relicense; author-owned) |
| `preset/preset_parser.cpp` | 767 | `.slangp` grammar, `#reference` merge, path resolution, folder scan | **Port** + order-independence tests |
| `preset/slang_translate.cpp` | 854 | `.slang` → per-stage GLSL: stage split, includes, pragmas, UBO/push re-homing, sampler rewrite, semantic validation | **Port** (mostly Vulkan-flavored already) |
| `preset/slang_compile.cpp` | 183 | glslang invocation → SPIR-V | **Port**; glslang stays vendored |
| `preset/preset_runner.cpp` | 1,228 | SDL_GPU pass runner (pipelines, FBOs, history, feedback) | **Not ported** — rewritten as the new Vulkan runner |
| `gpu_scaler.cpp/.hpp` | 1,434 | DevilutionX-side scaler: window claim, presentation, OSD, fallback | **Stays in fork**; becomes a thin adapter over the lib |
| `3rdParty/glslang/` | — | Vendored glslang | **Moves** to lib repo |
| `tools/audit/` (harnesses, field presets, CSVs) | — | Corpus test infrastructure: `test_history_ring`, `test_mipmap_plan`, `test_uniform_names`, feedback/format/include presets… | **Moves** to lib repo as conformance suite |
| `docs/SLANG_SPEC.md` | — | Normative per-key/per-pragma format contract with status matrix | **Moves** to lib repo as `docs/SPEC.md` |

Two properties of this codebase make the extraction unusually low-risk. First, the core trio is already GPU-API-free: it produces a pass graph plus SPIR-V blobs and knows nothing about SDL_GPU, so it transfers cleanly. Second, the semantics were hardened against the *actual* corpus, not a sample: the 2026-10-14 static audit cross-referenced all five preset-layer TUs against 2,658 real presets and fixed the two silent "renders but wrong" bugs it found (F5 history ring, P1 bare-`scale` parse), with the regression tests that now seed this library's suite. The remaining 31 open findings are all Low severity and concentrate on the runner surface being replaced anyway.

## 4. Provenance and Licensing Policy

The MIT choice is only as good as the provenance discipline behind it, so this section is normative.

- **Fork-authored code is portable.** The preset layer was authored inside the VTSTech fork; VTSTech holds the copyright and may relicense it to MIT. Each ported file gets a provenance header (origin commit, relicense statement) in a `PROVENANCE.md` ledger.
- **Upstream DevilutionX code is banned.** Nothing under `Source/` that originates from upstream `diasurgical/DevilutionX` (game logic, UI, options plumbing around the scaler) may enter the libslang-vk repo. Where an idea is needed (e.g., the fallback-to-stock-renderer contract), it is re-expressed, not copied.
- **No RetroArch code, ever.** RetroArch's slang implementation (`gfx/video_shader_parse.c` et al.) is GPL-3. libslang-vk is clean-room against it: the references are the libretro slang documentation, `docs/SLANG_SPEC.md` (itself written from docs + corpus conventions + the fork's independent implementation), and black-box behavior comparison. The fork's `tools/audit/scan_codefindings.py` methodology is realized as this repo's `tools/scan_codefindings.py` (2026-10-18, miss-list #9) — a RetroArch-fingerprint scanner with inline-waiver support, wired as the `provenance` CI job so the gate runs on every push and before any release tag.
- **Vendored dependencies** (all MIT-compatible): glslang (Apache-2.0 / BSD, Khronos), Vulkan Memory Allocator (MIT), Vulkan-Headers (Apache-2.0 OR MIT). No other runtime dependencies; logging is a caller-provided callback.

## 5. Architecture

### 5.1 Module map

```
libslang-vk/
├── include/slangvk/      Public headers — C++23, namespace slv, raw VkHandle types
├── src/core/             GPU-agnostic: preset parser, translator, compiler
├── src/vk/               Vulkan runner: device, images/LUTs, chain executor, present
├── tests/                Unit + field-round + headless runner suites; fixtures in tests/data/, golden-image kit in tests/golden/
├── examples/             preset_info, corpus_sweep, shader_demo, conformance + bundled presets
├── tools/                scan_codefindings.py (provenance gate, CI job `provenance`) · make_testcard.py + sprites/ (demo testcard)
├── cmake/deps.cmake      Pinned FetchContent deps (glslang, Vulkan-Headers, volk, VMA)
└── docs/                 SPEC.md (format contract), PLAN.md, CHECKLIST.md, CHANGELOG.md, API.md (draft)
```

(The plan's original `src/interop/` row landed as the public value types in
`include/slangvk/runner.hpp`; `corpus/` fixtures live under `tests/data/`;
the audit harnesses became the `tests/` suite. Map corrected 2026-10-18,
miss-list #13; examples/tools/docs rows refreshed 2026-10-19 after the
shader_demo + conformance tools and the doc set landed.)

The load-bearing rule is the **core/vk boundary**: `src/core` never includes `vulkan.h`. It consumes shader source and preset files, and emits an intermediate representation — ordered passes, resource bindings, parameter blocks, compiled SPIR-V. `src/vk` consumes that IR. This is what made the fork's preset layer portable in the first place, and preserving it keeps the "Vulkan only" decision cheap to revisit later without having designed for anything.

### 5.2 Data flow

Per frame, the host calls `Chain::render(input, output, frameInfo)`. The executor walks the preset's pass list: the input frame (for DevilutionX, the RGBA8 conversion of the 640×480/960×540 paletted backbuffer) is bound as `source` for pass 0; each pass renders into an FBO sized by its scale rules (with feedback and history images rotated in per their ring semantics); pass N−1's output goes to the host's swapchain image view or to an offscreen export target. Parameters (`#pragma parameter` uniforms) are read from a host-updatable block; frame metadata (`FrameCount`, `FrameDirection`, `FrameTiming`, orientation flip) travels in the same UBO plus push constants for pass matrices.

Failure contract, inherited verbatim from the mod's invariant: **any** load or run error is returned as `std::expected`'s error — the library never terminates, never crashes, never writes to stdout uninvited. The host owns degradation (in DevilutionX: fall back to the stock renderer, never `app_fatal`).

### 5.3 Threading model

v1 is single-threaded per `Chain`. Compilation is synchronous at `Preset::load` — the accepted trade-off of the "glslang only, no cache" decision is a one-time load cost per preset selection, which matches DevilutionX's UX (shaders load on menu selection, not per frame). glslang invocations are thread-safe across distinct loads, so a host may warm several `Preset` objects on a worker thread. Sampler and pipeline caches are per-`Chain` and not thread-safe; this is documented API behavior, not an oversight to fix later.

## 6. Public API Sketch

Anchors the discussion; not final. Raw Vulkan handle types at the boundary (no vulkan-hpp dependency); errors as `std::expected<T, slv::Error>`.

```cpp
namespace slv {

enum class Errc { preset_parse, include_cycle, shader_compile, unsupported_feature,
                  vk_device_lost, vk_surface_unsupported, vk_out_of_memory, … };

struct Error { Errc code; std::string message; std::string file; int line = -1; };

struct DeviceOptions { std::string_view appName; uint32_t minApiVersion; /* … */ };

class Device {  // adopts host context or creates its own
public:
    static auto adopt(VkInstance, VkPhysicalDevice, VkDevice,
                      VkQueue graphicsQueue, uint32_t graphicsFamily)
        -> std::expected<Device, Error>;
    static auto create(const DeviceOptions&) -> std::expected<Device, Error>;
};

struct PresetLoadOptions { bool strict;  // RetroArch-divergence lints become errors
                           bool noLutImages; };  // for headless/compile-only sweeps

class Preset {  // parsed + translated + compiled; immutable; shareable across chains
public:
    static auto load(Device&, const std::filesystem::path& slangp,
                     PresetLoadOptions) -> std::expected<Preset, Error>;
    auto parameterInfo() const -> std::span<const ParameterInfo>;
};

struct ChainOptions { uint32_t outputWidth, outputHeight; VkFormat outputFormat; };

class Chain {  // per-device runtime: pipelines, FBOs, samplers, history rings
public:
    static auto create(Device&, const Preset&, ChainOptions) -> std::expected<Chain, Error>;
    auto resize(uint32_t w, uint32_t h) -> std::expected<void, Error>;
    auto setParameter(std::string_view id, float value) -> std::expected<void, Error>;
    auto render(InputFrame, OutputTarget, const FrameInfo&) -> std::expected<void, Error>;
};

struct InputFrame  { VkImageView view; VkFormat format; uint32_t width, height;
                     Rotation rotation; };
struct Offscreen   { VkImageView view; };              // host composites
struct SwapchainFrame { VkImageView swapchainImageView; Rect outputRect; }; // lib presents into it
using OutputTarget = std::variant<SwapchainFrame, Offscreen>;

} // namespace slv
```

The `Offscreen` output mode is what makes headless conformance possible: the entire corpus can be exercised on lavapipe in CI with no window system at all.

**Delivered API (2026-10-19).** The shipped surface is documented normatively in [`docs/API.md`](API.md) — this sketch remains the design record. Deltas worth noting against the sketch above: `Errc` is the concrete nine-value set (preset_parse, include_cycle, shader_compile, unsupported_feature, invalid_argument, vk_init_failed, vk_device_lost, vk_out_of_memory, vk_error); `Device` additionally exposes `readPixels` (headless readback used by the golden suite), `validationErrors()` and `properties()`; a convenience `Image` class (VMA-backed 2D image + view, `update()` per-frame upload) covers host-side input/target allocation; `Chain::resize` takes input *and* output sizes; `Chain::passOutputSize` exposes per-pass FBO geometry; `DeviceOptions` carries instance/device extension lists (swapchain use) and the validation-layer switch. The core layer (`slv::preset`) is fully public for hosts building custom load loops — with the `CollectSlangPragmaNames` seeding contract (USAGE.md, Consuming).

## 7. Slang Spec Conformance Scope

`docs/SLANG_SPEC.md` — the fork's per-key, per-pragma contract with its Supported / Partial / Divergent status matrix — moves into this repo as the normative `docs/SPEC.md`. Every row marked *Partial* or *Divergent* becomes a tracked work item with a v1 disposition: either implemented to spec, or documented divergence with a test pinning the behavior. The Divergent-class items already known and fixed in the fork set the tone — RetroArch's order-independent preset parsing and the history ring direction are both now covered by dedicated tests, and the lib inherits those tests rather than re-deriving the semantics.

One scope note: the fork's translator hard-fails on a handful of constructs (vertex-stage samplers, sampler arrays, multi-UBO) that the 2026-10-14 audit confirmed are **exercised by zero of 2,658 corpus presets**. v1 keeps those as hard errors with clear messages rather than speculative support; if the corpus ever grows a user, the audit pipeline will surface it.

## 8. Vulkan Runner Design

**Version floor and feature use.** Vulkan 1.1 minimum for driver reach. The
runner uses classic render passes + core-1.0 stage-masked barriers on every
device — the sync2/dynamic-rendering fast path once planned "with M2's
capability detection" never shipped and is recorded as a formal v1
divergence / post-1.0 lever (§13; PROVENANCE M1 divergence #2). Push
carriers: push constants for pass matrices; one descriptor set per pass
binds the param/metadata UBO plus N combined image samplers (source,
`PREV`/`PASSPREV` taps, aliases, LUT textures, feedback targets).

**Targets and formats.** FBO formats per preset (`RGBA8`, `RGBA16F`, `R16F`,
`R32F`, sRGB variants), honor `scale_type` / fractional / absolute scaling,
with `max_fbo_width/height` clamps implemented (2026-10-18: the clamped FBO
size is what downstream Source-scale passes resolve against). Sampler cache
is bounded and keyed by (wrap S/T, filter, mipmap).

**History and feedback.** `OriginalHistory#` rings, `PREV`/`PASSPREV` taps and per-pass feedback are double-buffered image sets rotated with the exact slot ordering the fork's F5 fix pinned — the ported `test_history_ring` / field presets (`feedback.slangp`, `feedback-noncausal.slangp`, `history.slangp`) are the gate.

**Memory and sync.** VMA-backed allocation with per-chain pools; explicit
stage-masked barriers between passes; acquire/present semaphores for
swapchain mode; timeline semaphores reserved for a future async-upload
path. The `trim()` hook once sketched here is a recorded post-1.0 lever
(§13; PROVENANCE divergence #6) — v1 caches are per-`Chain` and freed
wholesale, so there is nothing incremental to trim.

**Output modes.** (a) Final pass renders into a host-provided swapchain image view, with the lib handling letterbox rect, orientation flip, and final resolve — this is DevilutionX's mode, matching its existing presentation ownership. (b) Offscreen export for hosts that composite themselves, and for CI.

## 9. DevilutionX Integration Contract

The library stays SDL-free; surface creation remains host-side. On the fork's SDL3 build, `gpu_scaler` keeps its public interface (`ScalerWanted` / `IsActive` / `HasFailed` / `WindowHasSwapchainHistory`) and swaps its SDL_GPU runner for a libslang-vk `Chain` when the backend is Vulkan: the SDL window is created with `SDL_WINDOW_VULKAN`, `SDL_Vulkan_CreateSurface` + instance are handed to `Device::adopt`, and the preset browser, ini persistence, OSD overlay, and `(Unavailable)` fallback flows continue unchanged on top.

**Status (2026-10-18).** The adapter spike is underway on the fork's
[`libslang-vk`](https://github.com/VTSTech/DevilutionX/tree/libslang-vk)
branch (a separate agent works the integration there; this repository stays
library-only). It targets exactly the contract above — `gpu_scaler`
interface preserved, host context adopted, dual-path with the SDL_GPU
runner until corpus parity (§12 risk 5). The spike closes when the lib path
passes the hardware field sweep on Windows + Linux (§11 M4); library-side
M0–M4 gates it consumes are green (PROVENANCE.md verification record).
Hardware sessions (Windows RX 560, a Linux llvmpipe run, then the HDR
preset family on Windows) are logged in PROVENANCE.md's **hardware field
log**: five sessions / six host runs across two OSes — 40 preset loads
over 29 unique presets, 14,509 frames counted — including 26- and 25-pass
chains, the hqx/ScaleFX/ScaleHQ/FSR scale-rule families, two in-game
level-load transitions survived with no Reinit, and (session 4, Linux)
the fit-and-center letterbox present path on mismatched
game/swapchain sizes — zero skipped frames and no fallbacks throughout. Open: heavy-suite
steady-state perf (crt-maximus-royale ~19 fps post-load window; 4.05 s
synchronous load), a real-GPU (dGPU) Linux run, history/feedback/format/
mipmap coverage targets, and the VRAM soak.

Two integration consequences are worth stating openly. First, **dual-path period**: the fork will run both its SDL_GPU runner (Metal, and as a safety net) and the libslang-vk runner (Vulkan) behind the same scaler interface until the lib reaches corpus parity — whether the SDL_GPU runner is then retired is an open question (§13). Second, **upstream merge leverage**: `docs/merge-plan-draft-r2.md` gates its PR series on shader-stack findings; once the preset layer and runner live in an MIT library, the mod's mergeable surface shrinks to a thin adapter, which is exactly the shape upstream review tolerates best.

One honest caveat on the "static + dylib" decision: the API uses C++23 vocabulary types (`std::expected`, `std::span`, `std::string_view`, `std::variant`), so **ABI stability across toolchains is explicitly not promised**. Shared builds are distributed the way the fork's releases are — built in CI per platform/toolchain — and the docs will say so.

## 10. Testing and Conformance Strategy

- **Unit (core, no GPU):** ported from `tools/audit/` — parser order-independence (P1 regression), include-cycle handling (`r4_cycle_*`), history ring depth and direction (F5), uniform name mapping, viewport derivation, mipmap planning, rotation/format handling, plus the round-1..4 field tests.
- **Compile sweep:** the nightly `corpus` workflow (`.github/workflows/corpus.yml`, 2026-10-18) loads and compiles every preset headlessly (lavapipe, `noLutImages` mode) and uploads the ledger artifact — the same ledger format the fork's campaign already produces. Policy: any preset that loaded in the previous release must not regress without an audit-documented reason. **Gate on since 2026-10-18 (miss list #5):** the no-regression baseline is pinned at `corpus/baseline-ledger.txt` (corpus revision recorded in its header; 2,658 presets full-depth, 826 clean / 1,832 pre-existing broken with corpus-verified causes). The nightly checks the corpus out at the pinned revision (fetch-by-SHA) so the gate measures library regressions deterministically, fails on any baseline-LOAD → now-FAIL transition, and refreshes of the baseline are a documented reviewed procedure (workflow header). **Verdict after the M4.03 dead-code texture fix (2026-10-18): 2,637 LOAD / 21 FAIL**, the survivors verified per preset as upstream-broken or malformed and pinned as FAIL in the refreshed ledger.
- **Golden images:** shipped 2026-10-18 (M4.04) as `conformance golden` (`examples/conformance.cpp` + `tests/golden/`): a 20-entry manifest covering every chain semantic (blit, multi-pass, feedback ping-pong, history ring, mipmap refill, FBO formats, push-fragment uniforms, parameters, size uniforms, LUT families, rotation, includes, the demo presets) is rendered from a deterministic procedural testcard at a fixed target and compared against committed PNGs — per-channel-delta + mismatched-fraction tolerances, `--update` regenerates and stamps the generating device, a different local device warns once. Byte-exact on lavapipe; the original "crt-royale / Mega Bezel / pHash" wording is recorded as a refined scope (CHECKLIST miss list #6): corpus-heavy goldens are backend-specific and pHash buys nothing at byte-exact stability.
- **Perf budgets:** shipped 2026-10-18 (M4.04) as `conformance perf`: warmup + timed wall-clock around the synchronous fenced `Chain::render` per preset, TAB `--report` ledger, and a `--budgets` gate (`path<TAB>max-ms`, breach exits 1, unmeasured tolerated). Reference budgets for the sandbox renderer live in `tests/golden/budgets-lavapipe.txt` (format documentation); real-hardware budgets come from running `--report` on the target machine (CHECKLIST miss list #7) — first committed example: `tests/golden/perf-rx560.txt` (Radeon RX 560, Windows: means 0.29–1.20 ms, 835–3,457 fps) with derived `tests/golden/budgets-rx560.txt` (`max(1.5 × mean, 1.0 ms)` regression guard, tightest cross-run headroom 38%).
- **Sanitizers and validation:** ASan/UBSan on the unit suite; Vulkan validation layers enabled in every headless run, **with synchronization validation** (instance-level `VkValidationFeaturesEXT`, enabled 2026-10-18 whenever `VK_LAYER_KHRONOS_validation` is on — miss-list #2).
- **Field loop:** the fork remains the hardware testbed — its crash-logger, heartbeat, and load-phase diagnostics carry over to the adapter, and preset failures found on real machines flow back as corpus cases.

## 11. Milestones

| ID | Scope | Exit criteria | Est. |
|---|---|---|---|
| **M0** | Repo scaffolding: MIT + `PROVENANCE.md`, CMake (static+shared targets), CI (Linux/Windows, sanitizers, validation layers), vendor glslang/VMA/Vulkan-Headers | Ported core trio (`types`/`parser`/`translate`/`compile`) builds and passes ported unit tests on CI | 1–2 wk |
| **M1** | Vulkan runner skeleton: `Device` adopt/create, single-pass offscreen execution, blit round-trip | Single-pass preset renders headless on lavapipe *and* a real GPU; golden blit matches | 2 wk |
| **M2** | Full chain semantics: multipass, aliases, feedback, history rings, wrap/mipmap, FBO formats/scaling, runtime parameters | Fork's field-preset set (feedback / history / format / include / cycle) passes; parameter updates live | 3–4 wk |
| **M3** | Conformance + perf: full-corpus sweep in CI, golden-image suite, perf budgets | Sweep ledger ≥ fork's current coverage; no validation-layer errors; ScaleFX 4x ≥ 240 fps @1080p, crt-royale ≥ 60 fps @4K on the reference GPU | 2 wk |
| **M4** | DevilutionX spike: adapter on fork branch `libslang-vk` (§9); menu/persistence/OSD unchanged; fallback verified | Vulkan path through the lib passes a hardware field sweep on Windows + Linux; SDL_GPU path untouched | 2 wk |
| **M5** | 1.0: API freeze, packaging (CMake config + pkg-config), `docs/API.md`, tagged release + example app | Third-party project can `FetchContent` and run the example against its own swapchain | 1 wk |

Durations assume the solo-maintainer + AI-assisted audit loop that produced the fork's round-1..16 ledger; M2 and M4 are the risk-carrying phases.

**Implementation status (2026-10-18).** The runner landed under its own
working milestone numbers (M1 skeleton → M2 chain semantics → M3 LUTs +
corpus-sweep scaffolding → M4 presentation, per PROVENANCE.md). Mapped onto
this table:

| Plan milestone | Status | Notes |
|---|---|---|
| M0 scaffolding | ✅ done | Ported core trio, MIT + provenance ledger, CI (`.github/workflows/ci.yml`: Linux tests on lavapipe + asan/ubsan build, Windows MinGW `libslangvk.dll/.a` artifacts), vendored deps |
| M1 runner skeleton | ✅ done | Headless golden blit on lavapipe, zero validation errors |
| M2 chain semantics | ✅ done | Field canaries pass on the Vulkan runner (`test_runner_m2`) |
| M3 conformance + perf | ✅ done (2026-10-18) | LUT upload + `corpus_sweep` tool (repo "M3"); full-corpus nightly ledger gate ON (miss #5 — pinned baseline, 2,637 LOAD / 21 FAIL after M4.03); golden-image suite + perf budgets shipped as the `conformance` tool (M4.04); §11's fixed reference-GPU fps numbers retired in favor of the per-host budget workflow |
| M4 DevilutionX spike | 🔶 in progress | Presentation half done — plan §8 mode (a) shipped as repo "M4" (`SwapchainFrame`, letterbox, rotation); adapter spike underway on fork branch `libslang-vk`, hardware field sweep is the exit gate |
| M5 1.0 release | 🔶 in progress | API freeze, packaging; `docs/API.md` drafted early (2026-10-19); freeze review executed + 4 maintainer decisions applied 2026-10-19 ([API-freeze-review.md](API-freeze-review.md)) — **the freeze is not imminent: M4.xx keeps iterating until the surface is ready, and the review will be redone against the then-current headers**; freeze stamp + packaging + tagged release remain |

Where a working milestone label and this table disagree on scope,
PROVENANCE.md's per-milestone sections are authoritative.

## 12. Risk Register

| # | Risk | L | I | Mitigation |
|---|---|---|---|---|
| 1 | GPL contamination accusation (RetroArch semantics learned "too well") | M | H | Clean-room policy (§4), `scan_codefindings.py` as CI gate, `PROVENANCE.md` ledger, spec-and-black-box references only |
| 2 | Vulkan-only excludes macOS users of the mod | H | M | SDL_GPU/Metal path retained in fork; MoltenVK as optional tier; revisit post-1.0 |
| 3 | glslang static size (~10–15 MB) bloats hosts | M | M | Levers implemented as build options (2026-10-18, miss-list #13): `SLANGVK_LTO` (IPO + `-Oz` release codegen) and `SLANGVK_SYSTEM_GLSLANG` (link the host's glslang instead of the pinned FetchContent copy); artifact sizes documented in ARTIFACTS.txt/USAGE.md |
| 4 | RetroArch semantic divergence discovered late (e.g., sRGB, rotation edge cases) | M | M | `SPEC.md` status matrix is normative; divergence items pinned by tests in M2, corpus sweep in M3 catches the rest |
| 5 | Drift between fork's SDL_GPU runner and lib runner during dual-path period | M | M | Time-box the period; corpus ledger is shared; M4 A/B sweep is the convergence gate |
| 6 | Cold-start cost of no-disk-cache choice on huge presets (crt-royale suites) | M | L | Measured (hardware worst 4.05 s, warm-cache collapse 196→73 ms); `conformance perf --report` (M4.04) is the standing measurement vehicle for per-host load/render numbers; in-`Preset` memoization covers repeated selections in one session; disk cache is the documented post-1.0 lever |
| 7 | Maintainer bandwidth across two repos | M | M | The fork's audit-driven AI loop is the working model; conformance automation (M3) is what keeps the lib cheap to own |

## 13. Open Questions

1. **Runner convergence** — once libslang-vk reaches corpus parity, does the fork retire its SDL_GPU runner (leaving Metal unsupported on the fork) or maintain both indefinitely?
2. **Reload semantics** — the fork closed FEAT-01 with in-process preset reload; should `Preset` expose an explicit `reload()` or is replace-and-recreate the v1 answer?
3. **Disk SPIR-V cache** — revisit as post-1.0 extension keyed by (source hash, glslang version, options); the fork's `shaders/cache/` layout is prior art.
4. **MoltenVK tier** — best-effort CI or fully unsupported with a docs disclaimer?
5. **Reflection surface** — expose per-pass uniform/texture introspection for hosts building debug OSDs, or keep v1 minimal?
6. **Extraction mechanics** — clean-room retype into the new repo vs. mechanical move with per-file relicense headers; decision affects `PROVENANCE.md` granularity.
7. **Deferred perf/memory levers (recorded 2026-10-18)** — sync2 barriers + dynamic rendering (PROVENANCE divergence #5), the `Chain::trim()` memory hook (divergence #6), async upload via timeline semaphores, and the disk SPIR-V cache (#3 above): all deliberately out of v1. Revisit as a post-1.0 perf round with the corpus ledger + perf budgets in hand.
8. **Load-path budgets (recorded 2026-10-19 — closes the Risk-6 residual)** — hardware numbers: 4.05 s worst cold start on the heaviest field preset, warm-cache collapse 196→73 ms. Posture: no hard load-time gate pre-1.0 — `conformance perf --report`/`--budgets` is the per-host standing vehicle (render side measured, e.g. `tests/golden/perf-rx560.txt` + derived `budgets-rx560.txt`), in-`Preset` memoization covers repeated selections within a session, and the disk SPIR-V cache (#3 above) remains the documented post-1.0 lever for cold starts.

## 14. References

**Fork (VTSTech/DevilutionX):** `docs/GPU_SHADERS.md` (user guide), `docs/SLANG_SPEC.md` (format contract), `docs/audit.md` + `docs/audit/deltas.md` (ledger), `docs/audit/shader-support-audit-2026-10-14.md` (corpus audit), `docs/diablo-handoff.md` (architecture history), `docs/merge-plan-draft-r2.md` (upstream plan), `tools/audit/` (harnesses + field presets).

**libretro:** slang shader development docs (docs.libretro.com), `libretro/slang-shaders` corpus (2,658 presets), RetroArch `gfx/video_shader_parse.c` (reference behavior only — no code).

**Dependencies:** glslang, Vulkan Memory Allocator, Vulkan-Headers; Vulkan 1.3 spec (dynamic rendering, timeline semaphores, synchronization2).
