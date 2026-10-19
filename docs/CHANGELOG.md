# Changelog

All notable changes to **libslang-vk** — a C++23 Vulkan runner for libretro
slang shader presets — are documented in this file.

Milestones follow the M-series plan in [PLAN.md §11](PLAN.md) (`M0` → `M5`).
Each milestone corresponds to the matching `M*` commit(s) on `main`, tagged
`M0` … `M4.04`. Format adapted from
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

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

- **shader_demo `params` now shows live values.** It printed the preset
  default as the *current* value, so after `set BRIGHTNESS 3.0` the display
  still read `1.250` — indistinguishable from "the set didn't take" (the
  exact confusion behind the maintainer's live-parameter field report).
  Runtime overrides are now tracked per session and reported with a
  `[runtime override]` tag; the lib path itself was never broken —
  verified by byte-different PNG dumps before/after `set` on llvmpipe.
  Help text and both CI artifact USAGE blocks document `reload` and the
  file-edits-need-reload rule.

### Planned

- M5 (1.0): API freeze, packaging, `docs/API.md`.
- The 21 ledger-FAIL presets are upstream-broken or malformed (see M4.03) —
  no library action planned; they stay pinned as FAIL in the baseline.

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

[Unreleased]: https://github.com/VTSTech/libslang-vk/compare/31519c8...HEAD
[M4.04]: https://github.com/VTSTech/libslang-vk/compare/89963da...31519c8
[M4.03]: https://github.com/VTSTech/libslang-vk/commit/89963da
[M4.02]: https://github.com/VTSTech/libslang-vk/commit/cf91f52
[M4.01]: https://github.com/VTSTech/libslang-vk/commit/3a6ec45
[M4]: https://github.com/VTSTech/libslang-vk/compare/b7c6583...3a6ec45
[M3]: https://github.com/VTSTech/libslang-vk/commit/b7c6583
[M2]: https://github.com/VTSTech/libslang-vk/commit/8a02171
[M1]: https://github.com/VTSTech/libslang-vk/compare/f1bd2b8...aed9cef
[M0]: https://github.com/VTSTech/libslang-vk/commit/25ce03d
