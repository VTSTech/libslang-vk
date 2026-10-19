# API freeze review — findings record (M5 wave opener)

| | |
|---|---|
| **Status** | **Review only — NOT the freeze.** Findings fold into `API.md` (and two header comments) before the freeze stamp; the four decisions in §4 need a maintainer call. Baseline: every statement checked against `550dfbd` on `main` (2026-10-19); `include/slangvk/` is the header of record per API.md's preamble. |
| **Method** | Line-by-line cross-read of the seven public headers vs `docs/API.md`; semantic spot-checks against `src/` (each finding carries file:line evidence); the five §10 freeze-review checklist items executed — including compiling and running the §7 walkthrough against the built library on lavapipe; ctest 26/26 at the baseline. |
| **Companion docs** | [API.md](API.md) (§10 checklist is what this record runs) · [CHECKLIST.md](CHECKLIST.md) (M5 boundary) · [PLAN.md](PLAN.md) (§11 M5 gate) |

## 1. §10 checklist results

| # | Item | Result | Evidence / gap |
|---|---|---|---|
| 1 | Every public name documented | **46/47 — one gap** | `slv::preset::UniformType` (14 values, `slang_translate.hpp:18-33`) is never enumerated in §4.3 — `UniformMember::type` is referenced but the enum itself is undocumented. Everything else in `include/` is covered: types.hpp 10/10, preset_parser.hpp 8/8, slang_translate.hpp 8/9, slang_compile.hpp 2/2, runner.hpp 18/18, umbrella gating + export/version macros ✓ (F-7) |
| 2 | Lifetime/threading rules have a test or PROVENANCE note | **PARTIAL** | Preset-outlives-Chain is pinned (PROVENANCE M3 borrowed-views note; the demo's staged-swap invariant encodes it). **The §5.3/§6 claim "glslang is process-globally guarded" is not true as written** — lifecycle yes, mutual exclusion no (F-1) |
| 3 | `Errc` coverage: every error path maps to the right category | **PASS (with one §3 wording exception, F-8)** | All 9 categories used — 111 sites: `vk_error` 33, `invalid_argument` 29, `vk_out_of_memory` 12, `vk_init_failed` 10, `shader_compile` 10, `preset_parse` 9, `unsupported_feature` 6, `vk_device_lost` 1, `include_cycle` 1. Mapping spot-checks all correct (table in §3) |
| 4 | §4.5 descriptor table matches the translator output | **PASS** | Pinned by `tests/test_slang_translate.cpp` (fragment texture rewrite → `set = 2` asserted at :284, :327, :413, :437 — integer `usampler2D` round-trip included); the runner consumes sets 1/2/3 per the same convention |
| 5 | §7 walkthrough compiles and runs against the headers | **PASS** | `-std=c++2b`, `-I include`, linked `build/libslangvk.so`: **zero changes to the doc body** (scaffold limited to pixel buffers, argv preset path, an error print, a readback sanity count). 60 frames via `Image::update` + `Chain::render`, letterbox present path, `Device::readPixels` → exact 640·480·4 bytes, 307,199/307,200 lit px, exit 0 (record in §5) |

## 2. Stale, wrong or missing — fix before the freeze

**F-1 (contract claim, §5.3 + §6): "glslang is process-globally guarded" — the guard does not exist.**
`GlslangRuntime` (`slang_compile.cpp:34-52`) is a lifecycle singleton only: `InitializeProcess()` once, `FinalizeProcess()` at exit. No lock is ever taken — `#include <mutex>` (`:21`) is dead weight. Two call sites run glslang frontends unserialized: `CompileStageToSpv` (`slang_compile.cpp:54-81`, `TShader::parse` + `TProgram::link` + `GlslangToSpv`) and the translate-time preprocessing oracle (`slang_translate.cpp:1136-1146`, `TShader::preprocess`). Two concurrent `Preset::load`s would race inside glslang, and nothing in CI exercises concurrency. Fix (recommended): one process-global `std::mutex` taken by both frontend entry points — loads are seconds-scale, contention is irrelevant, and the written claim becomes true. Alternative: weaken §5.3/§6 to "concurrent loads are not supported in v1; serialize `Preset::load`". Either way, docs and code must agree before the freeze (decision D-3).

**F-2 (wrong semantics, §5.4 + `runner.hpp`): `resize` is documented as doing things it does not do — and takes two parameters it ignores.**
API.md: "*`resize` takes **both** input and output sizes (the chain derives every pass FBO from the scale rules against these; history ring is re-seeded; feedback buffers are recreated)*." All three clauses are wrong as written: `Chain::resize` validates the output size and stores `targetWidth`/`targetHeight` — nothing else (`chain.cpp:1840-1848`; the input-size parameters are commented out of the signature). Pass FBOs derive from the **per-frame `InputFrame` size** plus the stored target (`ResolvePassSizes(impl, input.width, input.height, …)` at render, `chain.cpp:1924`); targets, the history ring and the mip copy are (re)created **lazily at render on size drift** — `outputWidth != allocatedWidth` → `MakePassTarget` (`chain.cpp:1953-1963`), `MakeHistorySlots` / `MakeSourceMipCopy` on input drift (`chain.cpp:2019-2029`). The `runner.hpp:363` comment ("Recreates pass FBOs for new input/output sizes") is stale by the same measure — and per API.md's preamble the headers win, so both need the fix. Resolution rides decision D-1.

**F-3 (missing contract, §8): swapchain extent changes must be preceded by `resize()`.**
Rendering with a `SwapchainFrame` whose size differs from the stored target is a hard `invalid_argument` — "swapchain size changed; call resize() with the new extent" (`chain.cpp:1894-1896`). §8's per-frame flow never says this; window resizes are the fork's daily path (and miss list #12's resize item). Add the host duty to §8 (and to the §5.4 `resize` bullet once D-1 settles the signature).

**F-4 (minor, undocumented render-time validations):** three rejects are absent from the docs; one sentence each covers them:
- offscreen target while `ChainOptions::outputFormat = VK_FORMAT_UNDEFINED` → `invalid_argument` (`chain.cpp:1901-1903`);
- `SwapchainFrame::format = VK_FORMAT_UNDEFINED` or a zero-texel format → `unsupported_feature` (`chain.cpp:1891-1893`);
- the *enforced* error for a null `InputFrame::image` when the preset needs history/mip (`invalid_argument` with a message naming the requirement, `chain.cpp:1916-1919`) — §5.5 documents the requirement but not the error.

**F-5 (minor, §4.2): `ResolvePresetFilePath` refuses out-of-root hits.**
The ancestor walk can reach above the shader root; a hit outside it is **refused (empty string returned), not read** (`preset_parser.cpp:943-946`). The §4.2 row (and the header comment) document only "first existing wins". One clause fixes it — and it belongs in the freeze text: it is the SEC-01 sandbox behaving correctly, hosts should know it is deliberate.

**F-6 (nit, §5.5): `SwapchainFrame::format` has a header default** (`= VK_FORMAT_UNDEFINED`, `runner.hpp:333`) that the API.md listing omits.

**F-7 (gap, §4.3): `UniformType` never enumerated** — 14 values `Float, Vec2, Vec3, Vec4, Int, IVec2, IVec3, IVec4, Uint, UVec2, UVec3, UVec4, Bool, Mat4` (`slang_translate.hpp:18-33`).

**F-8 (minor, §3 wording): two stderr writers contradict "logging is absent by design".**
- `chain.cpp:1450` — a **warn-once, unconditional** stderr notice when a non-causal texture tap falls back to the current source (SPEC 4.5 runtime tolerance; once per texture name per chain via `warnedTextureFallbacks`). Genuinely useful for pack authors, but it is outside the documented diagnostic channels (`Error::message` / translator `warnings` / `validationErrors()`) — there is no API surface that receives it.
- `chain.cpp:407` — a UBO-fill trace behind `SLANGVK_FILL_DEBUG` (env opt-in). Undocumented channel; either document it or drop it before the freeze.
Resolution rides decision D-4.

## 3. Verified-accurate spot checks (evidence appendix)

| API.md claim | Verdict | Evidence |
|---|---|---|
| §5.4 `setParameter`: case-insensitive; empty id → `invalid_argument`; unknown ids stored-but-inert; values written as given (no clamping) | ✓ | `chain.cpp:1850-1864` (`CaseInsensitiveEqual`, empty-id guard, `insert_or_assign`), `:2046-2060` (override merge; unmatched names reach no UBO member → inert) |
| §5.4 sampler cache "2 filters × 3 wraps × 2 mip modes, lazily created" | ✓ | `chain.cpp:513` (`unordered_map<uint64_t, VkSampler>` keyed wrap\|filter\|mipmap), `GetSampler` `:1210-1244` |
| §5.4 pipelines "cached per format" | ✓ | `chain.cpp:465` (`unordered_map<VkFormat, VkPipeline>` per render-pass format), present pipelines `:529` |
| §4.2 "up to 6 ancestor folders, then the shader root" | ✓ | `preset_parser.cpp:921` (`kMaxAncestors = 6`), root appended `:934-936` |
| §5.5 `FrameInfo::frameCount` "runner pre-wraps by frame_count_mod" | ✓ | `chain.cpp:288-289` (`count %= passDef.frameCountMod`) |
| §4.5 "maximum one plain UBO per stage (hard error)" | ✓ | `slang_translate.cpp:592` |
| §4.1 data-model table (all 10 types, all defaults) | ✓ | types.hpp compared field-by-field |
| §1/§2 CMake targets, option defaults, `SLANGVK_ENABLE_VK=1` PUBLIC definition, `OUTPUT_NAME slangvk` | ✓ | `CMakeLists.txt:40-41, 113, 140-146` |
| §1 umbrella gating (`SLANGVK_ENABLE_VK` / `SLANGVK_NO_DEFAULTS`) | ✓ | `slangvk.hpp:21-26` |
| §3 `Errc` category mapping (spot-checked: empty `setParameter` id, zero-size `resize`, swapchain size drift, format rejects, history/mip image requirement) | ✓ | `chain.cpp:1844, 1853, 1891-1907, 1916-1919, 1934-1936` |
| §6 move-only types; creation/destruction order; Preset-outlives-Chain | ✓ | runner.hpp (user-declared moves delete copies); PROVENANCE M3 note |
| §10 item 5 walkthrough shapes (designated init incl. nested `.desc`, skipped members with defaults, `expected` deref) | ✓ | compiled as written (§5 below) |

## 4. Decisions needed from the maintainer (pre-freeze)

**D-1 — `resize` signature.** The two input-size parameters are dead (F-2). Options: (a) **drop them** → `resize(uint32_t outputWidth, uint32_t outputHeight)`; call sites are ours (runner tests, demo, walkthrough). (b) Keep the 4-arg form and document the input sizes as accepted-but-unused (reserved for a future async path). Recommendation: (a) — freeze time is the one cheap moment to remove dead parameters; semantics become "store the output target; render reacts lazily to both".

**D-2 — parameter value query.** `Chain` has `setParameter` but no way to *read* the current merged value; hosts must track overrides themselves (shader_demo did exactly that, and Task 27's "`params` shows 1.250 after `set 3.0`" display bug was this gap biting). Options: (a) add `auto parameter(std::string_view id) const -> std::expected<float, Error>` (case-insensitive like `setParameter`; merged view: preset override → runtime override) or a `parameterValues()` span; (b) document the host-side duty as intended. Recommendation: (a) — it is the only known host-side workaround the current surface forces, and a UI-facing getter is cheap pre-freeze.

**D-3 — glslang guard (F-1).** (a) add the process-global mutex (recommended — makes the written claim true for ~3 lines); or (b) rewrite §5.3/§6 to "serialize `Preset::load` in v1".

**D-4 — stderr writers (F-8).** (a) amend §3 with the two named exceptions (warn-once non-causal fallback notice; `SLANGVK_FILL_DEBUG` trace) and document the env var; (b) route the fallback notice through a real surface (new API — weigh against freeze scope); (c) silence them. Recommendation: (a) — both messages have earned their keep in field debugging, and §3 stays honest with the exceptions named.

## 5. Walkthrough verification record (§10 item 5)

- **Compile:** `/usr/bin/c++ -std=c++2b -I include -I build/_deps/vulkan-headers-src/include walkthrough.cpp -L build -lslangvk -ldl -lpthread` — clean, exit 0, no doc-body edits required.
- **Run (lavapipe):** `tests/data/runner/blit.slangp` copied into a sandbox with the default relative `shaders/` root, so the doc's default-root contract is exercised exactly as written; 60 frames through `Image::update` + `Chain::render` (Offscreen 640×480 — the 1:1 final pass ≠ target, so the letterbox present path runs), `Device::readPixels` returns exactly 640·480·4 bytes, 307,199/307,200 lit pixels, exit 0.
- **Scaffold** (all marked in-source, nothing else touched): real pixel buffers for `myFrame`/`myNextFrame`; argv preset path (the doc's `shaders/crt/royale.slangp` line kept verbatim in the else-branch); a stderr print of `Error::message` on load failure (the doc discards it); a readback sanity count. The doc's designated-initializer shapes — `Device::create({ .appName = "minimal" })`, nested `.desc = { 320, 240 }`, skipped members with defaults, `expected` deref — all compile as published.
- This walkthrough is the seed for the M5 "example app" deliverable (API.md §10 item 5 already names it that).
