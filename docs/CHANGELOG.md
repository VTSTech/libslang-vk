# Changelog

All notable changes to **libslang-vk** — a C++23 Vulkan runner for libretro
slang shader presets — are documented in this file.

Milestones follow the M-series plan in [PLAN.md §11](PLAN.md) (`M0` → `M5`).
Each milestone corresponds to the matching `M*` commit(s) on `main` (no git
tags yet). Format adapted from
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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

### Planned

- M5 (1.0): API freeze, packaging, `docs/API.md`.
- The 21 ledger-FAIL presets are upstream-broken or malformed (see M4.03) —
  no library action planned; they stay pinned as FAIL in the baseline.

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

[Unreleased]: https://github.com/VTSTech/libslang-vk/compare/cf91f52...HEAD
[M4.02]: https://github.com/VTSTech/libslang-vk/commit/cf91f52
[M4.01]: https://github.com/VTSTech/libslang-vk/commit/3a6ec45
[M4]: https://github.com/VTSTech/libslang-vk/compare/b7c6583...3a6ec45
[M3]: https://github.com/VTSTech/libslang-vk/commit/b7c6583
[M2]: https://github.com/VTSTech/libslang-vk/commit/8a02171
[M1]: https://github.com/VTSTech/libslang-vk/compare/f1bd2b8...aed9cef
[M0]: https://github.com/VTSTech/libslang-vk/commit/25ce03d
