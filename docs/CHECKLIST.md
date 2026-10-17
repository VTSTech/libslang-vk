# CHECKLIST — M0–M4 implementation status

| | |
|---|---|
| **Date** | 2026-10-18 (Task 10 pass-cap field fix + miss #5 corpus baseline pinned) |
| **Audited against** | `docs/PLAN.md` §2 goals, §5.1 module map, §6 API sketch, §8 runner design, §10 testing strategy, §11 milestone table; `PROVENANCE.md` per-milestone records + hardware field log; `tests/`, `.github/workflows/ci.yml`, `src/`, `include/` |
| **Purpose** | Item-by-item audit so nothing is claimed that the repo doesn't back |

Legend: ✅ done + verified · 🔶 partial (evidence on one side only) · ⬜ open · ⚠️ drift (docs claim ≠ code).
Where this file and PROVENANCE.md disagree, PROVENANCE.md is authoritative and this file's ⚠️ rows are the corrections to make.

---

## Evidence base (what every status below cites)

**20/20 ctest** (lavapipe-class software rasterizer, Release GCC 14.2 — PROVENANCE M4 exit + the maintainer's `runner_pushblock` field canary, 2026-10-18):
`preset_parser` · `slang_translate` · `slang_compile` · `field_round1..4` · `history_ring` · `mipmap_plan` · `uniform_names` · `viewport_derive` · `rotation_format` · `runner_headless` · `runner_m2` · `runner_m3` · `runner_m4` · `runner_pushblock` · `example_preset_info_demo-bright` · `example_preset_info_demo-glow` · `example_corpus_sweep_smoke`.
Also verified per milestone exit: ASan/UBSan build, core-only `SLANGVK_ENABLE_VK=OFF` (14/14), live validation-error gate (proven non-vacuous by a deliberate-error probe collecting 2 VUIDs). 2026-10-18: the suite additionally runs clean **under synchronization validation** (A/B-verified locally on a user-space SwiftShader ICD with user-space Khronos validation layers — no sudo required — after the gate's first live CI run exposed real defects, see the miss list #2 note).

**CI** (`.github/workflows/ci.yml`): running since M0 — 19 runs by 2026-10-17 (original: Linux Release/Debug matrix on lavapipe **with `vulkan-validationlayers` installed**, asan/ubsan job, mingw-w64 job; MSVC job dropped by maintainer decision 2026-10-17). 2026-10-18: artifact-upload extension adopted — runs #14–#19 green; **artifact from run #18 (commit `c63b96d`) verified end-to-end**: PE32+ x86-64 `libslangvk.dll` 6,263,032 bytes with glslang 16.6.0 linked in, `libslangvk.dll.a` import 44,442 B, `libslangvk.a` static 1,808,164 B (no glslang embedded, per contract), full header set, commit-pinned `ARTIFACTS.txt`. Audit note: a 2026-10-18 search missed the hidden `.github/` directory and reported "no CI in repo" — wrong; what CI lacked was artifact uploads, not CI. The first artifact-upload ci.yml pass regressed the Debug matrix and validationlayers package; the merged workflow restores both.

**Hardware field log** (PROVENANCE, fork adapter builds `338827ff4`→`ff3999a6f`→`a98c6d5`): 4 sessions / 5 host runs across **2 OSes** — Windows RX 560 (dGPU) + Linux llvmpipe (software): 24 loads over 20 presets, max 26 passes, 9,093 frames counted, zero skipped/fallbacks/validation output/crashes. Session 4 (Linux) exercised the mismatched-size letterbox present path (game 1366x768 → swapchain 1299x736).

---

## M0 — repo scaffolding (plan §11)

| Item | Status | Evidence / gap |
|---|---|---|
| MIT + third-party license appendix | ✅ | `LICENSE` (VMA/stb appendix reflects actual deps) |
| `PROVENANCE.md` ledger | ✅ | File-by-file table, per-milestone records, divergences, field log |
| CMake static + shared targets | ✅ | `slangvk` (STATIC) + `slangvk-shared` (`OUTPUT_NAME slangvk`); install rules basic — full package config is M5 |
| Deps vendored/pinned | ✅ | FetchContent SHA-256: glslang 16.6.0, Vulkan-Headers 1.4.364, volk 1.4.363.0, VMA 3.3.0; stb v2.30 pinned copy |
| CI Linux | ✅ | Matrix job (Release+Debug) on lavapipe since M0; artifact uploads added 2026-10-18 (Release only) |
| CI Windows | ✅ | mingw-w64 job since M1 (`aed9cef`); artifact-producing since 2026-10-18 — first artifact verified (run #18, `c63b96d`) |
| CI sanitizers | ✅ | asan+ubsan job running since the original workflow. Windows sanitized build: **declared out of scope for v1** (maintainer decision 2026-10-18 — MinGW ASan experimental; Linux job covers the suite; ci.yml header) |
| Validation layers in CI | ✅ | Original CI installed `vulkan-validationlayers`; the 2026-10-18 ci.yml replacement dropped the package — **restored in the merged workflow**. Synchronization validation (plan §10) enabled 2026-10-18: instance-level `VkValidationFeaturesEXT` syncval whenever the layer is on (`device.cpp`) → miss list #2 closed. **The gate is not decorative: its first live CI run failed 5/20 runner tests and exposed two real defect families** (zero-access layout-transition barriers; the never-bound re-homed push/params UBO) — fixed the same day, suite green with syncval on |
| Exit: core trio passes ported unit tests on CI | ✅ | Passing on CI since M0 (12/12 then, 19/19 now) |
| Provenance gate (plan §4) | ✅ | `tools/scan_codefindings.py` (2026-10-18, miss list #9): RetroArch-fingerprint scan with inline `provenance-ok:` waivers; runs as the `provenance` CI job on every push; repo scans clean (6 self-waivers in the scanner's own pattern table) |

## M1 — Vulkan runner skeleton (plan §11)

| Item | Status | Evidence / gap |
|---|---|---|
| `Device::adopt` / `Device::create` | ✅ | `adopt` exercised on hardware by the fork adapter; `create` on lavapipe; surface-extension lists added in M4 |
| Single-pass offscreen execution | ✅ | `runner_headless`; `Offscreen` mode is also the CI conformance vehicle (§5/§8) |
| Golden blit round-trip | ✅ | Byte-identical on lavapipe |
| Exit: "headless on lavapipe **and** a real GPU" | 🔶 | lavapipe half ✅; real-GPU half rides the fork's field loop (documented ownership) — 9,093 frames of RX 560 evidence through the full path |
| Classic render passes (1.1 floor) | ✅ | What ships — everywhere |
| **sync2 / dynamic-rendering preferred path (§8)** | ✅ (divergence) | Resolved 2026-10-18 as a formal **v1 divergence** (miss list #1): classic render passes + core-1.0 barriers everywhere; sync2/dynamic rendering recorded as post-1.0 perf levers (plan §13, PROVENANCE divergence #5). Stale comments fixed (`chain.cpp` header, `images.cpp:185`); M1 divergence #2 rewritten to say what actually happened. Classic path is the verified conformance vehicle (byte-exact goldens + live gate + 9,093 hardware frames) |
| Push constants (pass matrices) / per-pass descriptor sets | ✅ (corrected 2026-10-18) | PROVENANCE M1; §8 contract. **Field bug found + fixed 2026-10-18**: the translator re-homes `push_constant` blocks to binding 1 of the stage UBO set, but the runner only ever declared/wrote/filled binding 0 — every field shader reading `params` from the fragment stage (fxaa/smaa shape) got zeros. Invisible to the old canaries (unused push blocks are dead-code-eliminated by glslang); caught by the maintainer's `runner_pushblock` canary + syncval VUID-07988/08114; fix = binding-0/1 set layouts, per-block UBOs/descriptor writes, per-block per-frame fill |
| Documented divergences (MVP orientation, case-insensitive params, synchronous render) | ✅ | PROVENANCE M1 §divergences |

## M2 — full chain semantics (plan §11)

| Item | Status | Evidence / gap |
|---|---|---|
| Multi-pass chains, scale rules (source/viewport/absolute, fractional) | ✅ | `runner_m2`; hardware: hqx x2/x3/x4 viewport+source rules (session 3), absolute scaling (session 2) |
| Aliases + `#pragma name` | ✅ | `runner_m2`; hardware: `maximus_refpass` alias (session 2) |
| History rings `OriginalHistory#` (F5) | ✅ | `runner_m2` (T-N indexing, seeding/warmup, gap taps 1+3); hardware: guest afterglow, 7 refs (session 3) |
| Feedback double-buffering (F2) | ✅ lib / ⬜ hw | `runner_m2` ping-pong goldens + final-pass resolve blit; **never exercised on hardware** (still-open sweep item) |
| Per-pass FBO formats (`#pragma format`, sRGB/float) | ✅ | R16F chain test; hardware: srgb+float framebuffers (session 2) |
| `mipmap_input` refill (F1) | ✅ lib / ⬜ hw | Exact box-average golden level 6; **not exercised on hardware** |
| Sampler policy (Source gets pass sampler, rest nearest/clamp) | ✅ | M2 §5 divergence pinned by fixtures |
| UBO `<Name>Size` → owning pass | ✅ | M2 §6 fix, golden-pinned |
| Include + cycle handling | ✅ | `field_round2..4` (`r4_cycle_*`), causality gate negative control |
| Live parameters (case-insensitive) | ✅ lib / ⬜ hw | `runner_m2`; not yet exercised on hardware |
| Resize → ring re-seed + FBO recreation | ✅ lib / ⬜ hw | `runner_m2`; hardware resize/fullscreen toggle still open (incl. swapchain-rebuild interplay) |
| Exit: fork's field-preset set passes; parameters live | ✅ | On lavapipe (`runner_m2`), per exit wording |

## M3 — conformance + perf (plan §11)

| Item | Status | Evidence / gap |
|---|---|---|
| LUT uploads (SPEC 3.5) | ✅ | `runner_m3` byte-exact goldens; hardware: smaa family (session 1); divergences documented (stb decode surface, 8-bit, `User#Size` extension, borrowed views) |
| Load-path hardening (F9 case, git-lfs detect, 256 MiB cap) | ✅ | M3 §4, named-error tests. 2026-10-18 field fix: pass-count cap (ROB-02) raised **32 → 128** — the tool-generated crt-adamant family declares ~94 passes and failed the old cap on hardware (`preset pass index 93 exceeds the limit of 32`); `#reference` merge stays intentionally uncapped; boundary tests added (sparse 40-pass parse; `shader128` and `shader2147483647` still rejected) |
| Validation-gate integrity fix | ✅ | Was vacuously green; probe collects 2 VUIDs; M1/M2 re-verified under live gate |
| `corpus_sweep` tool | ✅ | `examples/corpus_sweep.cpp`, `--render --lut --fail-on-broken`; smoke in ctest. 2026-10-18: machine ledgers added — `--ledger FILE` (sorted TAB records, corpus-rev header), `--baseline FILE` (no-regression diff gate: fails only baseline-LOAD → now-FAIL transitions), `--corpus-rev SHA`; full-depth scan via the `ScanPresets(root, maxDepth)` overload (the browser's default cap of 3 lists only 1,253 of the stock corpus's 2,658) |
| **Full-corpus CI ledger (2,658 presets, no-regression policy)** | ✅ | Miss list #5 closed 2026-10-18: baseline pinned at `corpus/baseline-ledger.txt` (corpus rev `e1d7563`, 2,658 presets: 826 load+compile clean, 1,832 pre-existing broken — verified corpus-side: Mega_Bezel's missing `resource/param_values` tree, SMAA's missing `SMAA.hlsl`, tool-generated junk). Nightly `corpus.yml` now pins the corpus to the baseline's revision (fetch-by-SHA), runs the `--baseline` regression gate, uploads fresh ledger + log; fails on any LOAD→FAIL flip. Refresh procedure documented in the workflow header. `--fail-on-broken` remains available for strict mode |
| **Golden-image suite (§10: crt-royale, Mega Bezel, xBR, ScaleFX, interlacing/history; pHash, bounded tolerance)** | ⬜ | Only bundled-fixture byte-exact goldens exist (M1–M4). No corpus preset goldens, no pHash infra → miss list #6 |
| **Perf budgets (ScaleFX 4x ≥240 fps @1080p; crt-royale ≥60 fps @4K)** | ⬜ | "Reference GPU" never defined; **crt-royale not yet run on hardware at all**; RX 560 data (ScaleFX ~35–44 fps @1080p vsynced, maximus-royale ~19 fps post-load window, 4.05 s cold load) is real but not comparable to the budgets → miss list #7 |
| Exit: no validation-layer errors | ✅ | Everywhere on lavapipe (live gate); zero validation output on hardware |
| Risk-6 cold-start measurement | 🔶 | Measured ad hoc on hardware (4.05 s worst, warm-cache collapse 196→73 ms); not folded into a budget or §13 note |

## M4 — presentation + DevilutionX spike (plan §11)

| Item | Status | Evidence / gap |
|---|---|---|
| `SwapchainFrame` present path (host acquires/presents) | ✅ | `runner_m4`: acquire/render/present/readback on `VK_EXT_headless_surface`, byte-exact vs offscreen reference; negative matrix |
| Letterbox (fit-and-center + explicit rect) | ✅ | Byte-exact goldens; integer-scaling stays host-side via explicit `outputRect` (documented divergence), field-verified on/off |
| Rotation (Cw90/180/270 + `Rotation` uniform) | ✅ | Per-quarter-turn texel goldens |
| `Image::update` per-frame upload | ✅ | Round-trip test; used by the adapter every frame |
| Per-format pipeline caching | ✅ | M4 §4 |
| Adapter: `gpu_scaler` interface preserved, dual-path, browser/ini/OSD | 🔶 | Underway on fork branch per §9; working in 4 hardware sessions (2 OSes) |
| Exit: hardware field sweep **Windows + Linux** | 🔶 | Windows half strong (RX 560, 3 sessions; vsync on/off, integer scaling, two ShowProgress level loads w/o Reinit). **Linux half: covered on llvmpipe** (session 4 — first Linux host run, letterbox present on mismatched sizes, clean); a **real-GPU (dGPU) Linux run** remains open to fully match the exit wording |
| Exit: "fallback verified" | ⚠️ | Field log says "no fallbacks" — i.e. never triggered. **The `(Unavailable)` fallback has not been deliberately provoked on the lib path** (broken preset → stock renderer + recovery) → miss list #11 |
| Exit: SDL_GPU path untouched | ✅ | Dual-path per §9; final A/B convergence check rides the spike gate |
| Hardware remainder (PROVENANCE still-open) | ⬜ | Feedback readers, `mipmap_input`, live params, resize/fullscreen toggle, VRAM-teardown soak, real-GPU (dGPU) Linux run, validation layer enabled on hardware |

---

## Cross-cutting contract claims (§2 goals, §8 design, §4 policy)

| Claim | Status | Note |
|---|---|---|
| Goal 1: full spec support incl. `max_fbo_*` clamping | ✅ | `max_fbo_width/height` implemented 2026-10-18 (miss list #3): parse + 0..16384 clamp + `#reference` adoption (`preset_parser.cpp`), runner clamp in `ResolvePassSizes` (clamped FBO = what downstream Source-scale reads), parser pins + `maxfbo.slangp` runner case. Zero corpus users (2026-10-14 audit) — Goal 1 completeness, now backed by code + tests |
| `histories` key | ✅ | Parsed + clamped 1..8, `#reference` adoption, now **pinned by parser tests** (miss list #4, 2026-10-18) and documented (SPEC §3.2 + PROVENANCE corpus note): the ring deliberately derives depth from the max referenced `OriginalHistoryN`; declared-but-unreferenced depth allocates nothing — informational key |
| Goal 1: parser/translate/compile semantics | ✅ | Corpus-validated in the fork's campaign; order-independence (P1) + causality gates ported |
| Goal 2: Vulkan-only, 1.1 floor | ✅ | Classic path only (see M1 sync2 row) |
| Goal 3: host integration (`adopt`, no SDL) | ✅ | `gpu_scaler` contract §9 |
| Goal 4: C++23 `std::expected` API | ✅ | §6 sketch delivered: `strict`, `noLutImages`, `parameterInfo()`, `resize()`, `setParameter()` all exist |
| Goal 6: dual distribution | ✅ | Now distributed per toolchain by CI artifacts (same-toolchain ABI documented, `export.h`) |
| §8: VMA-backed allocation, stage-masked barriers | ✅ | PROVENANCE M1/M2 |
| §8: sampler cache bounded+keyed / `trim()` hook | ✅ (divergence) | Sampler cache in; **`trim()` hook moved to the post-1.0 lever list** (miss list #8, 2026-10-18): PROVENANCE divergence #6 + plan §13 item 7 — v1 caches are per-`Chain` and freed wholesale, so no incremental trim surface exists to expose |
| §8: timeline semaphores | ✅ | Explicitly "reserved for future async-upload path" — not a gap |
| §4: `scan_codefindings.py` provenance CI gate before every release | ✅ | `tools/scan_codefindings.py` + `provenance` CI job (2026-10-18, miss list #9 — runs every push, stricter than the release-only plan) |
| §5.1 module map (`src/interop/`, `corpus/`, `tools/`) | ✅ | Map corrected 2026-10-18 (miss list #13): interop types → `runner.hpp`, corpus fixtures → `tests/data/`, `tools/` now real (scanner) |
| Risk 3 levers: `SLANGVK_SYSTEM_GLSLANG`, LTO/`-Oz` | ✅ | Implemented as opt-in CMake options 2026-10-18 (miss list #13): `SLANGVK_SYSTEM_GLSLANG` (find_package CONFIG vs pinned FetchContent), `SLANGVK_LTO` (IPO + `-Oz` release codegen, configure-time capability check); default build unchanged |

## M5 boundary (listed for completeness — not started)

⬜ API freeze · ⬜ CMake package config + pkg-config · ⬜ `docs/API.md` · ⬜ provenance gate wired into the release/tag pipeline (gate already in CI since 2026-10-18) · ⬜ tagged release + example app.

---

## The miss list (actionable, priority order)

1. [x] **sync2/dynamic-rendering path** — resolved 2026-10-18 as a formal **v1 divergence** (PROVENANCE M1 divergence #2 rewritten + divergence #5 + plan §13 item 7): classic path stays (it is the verified conformance vehicle), sync2/dynamic rendering recorded as post-1.0 perf levers. Stale comments fixed (`chain.cpp:41`, `images.cpp:185`).
2. [x] **Synchronization validation** — enabled 2026-10-18: instance-level `VkValidationFeaturesEXT` with `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT` whenever `DeviceOptions::validationLayers` is on and the layer is present (`device.cpp`; silent no-op without the layer). CI installs `vulkan-validationlayers`, so the live gate (now syncval-augmented) runs on every push. **First live run bit, as designed (2026-10-18)**: 5/20 runner tests red on CI — root causes (a) `BarrierMipLevel` accepted access masks but never assigned them + zero-mask barriers in `Image::create/update/readPixels`, LUT upload and target-init paths (READ_AFTER_WRITE / WRITE_AFTER_WRITE on every readback, mip refill and upload), (b) the push/params UBO interface bug (see M1 row), (c) the m4 test's own readback ordering that syncval cannot see through host `waitIdle` (restructured onto an acquire-semaphore chain). Fixed same day; reproduction method: user-space SwiftShader ICD (agent-browser Chrome bundle, patched ICD json) × user-space validation layers debs — Run A (no layers) isolated the params bug via the pushblock golden, Run B reproduced all 5 CI failures exactly; both runs 20/20 after the fix.
3. [x] **`max_fbo_width/height` clamps** — implemented 2026-10-18: `Preset::maxFboWidth/Height` (types.hpp), parser keys + 0..16384 clamp + `#reference` adoption, runner clamp in `ResolvePassSizes` (clamped FBO = downstream Source-scale base), SPEC §3.2 row, parser pins + `runner_headless` `maxfbo.slangp` case (64→16 clamp; the chain-wide cap re-clamps the 2x downstream pass to 16). PROVENANCE corpus-note wording corrected. (2026-10-18 follow-up: the case's downstream expectation originally said 32 — inconsistent with the SPEC's cap-on-every-pass semantics and untested at the time; test + fixture comment corrected in the syncval fix batch.)
4. [x] **`histories` key** — parse + clamp (1..8) + quoted-value + `#reference` adoption pinned in `test_preset_parser` (2026-10-18); documented in SPEC §3.2 ("informational") and the PROVENANCE corpus note: ring depth = max referenced tap; declared-but-unreferenced depth allocates nothing.
5. [x] **Full-corpus CI ledger** — closed 2026-10-18: baseline pinned at `corpus/baseline-ledger.txt` (corpus rev `e1d7563`; 2,658 presets full-depth — 826 load+compile clean, 1,832 pre-existing broken, failure taxonomy verified corpus-side). `corpus_sweep` gained machine ledgers (`--ledger`, `--baseline` regression gate, `--corpus-rev`) and full-depth scanning (`ScanPresets(root, maxDepth)` overload — the browser's default depth-3 cap only reached 1,253). Nightly `corpus.yml` pins the corpus to the baseline revision (fetch-by-SHA), runs the gate, uploads the fresh ledger + log; baseline refresh is a documented reviewed procedure. `--fail-on-broken` stays available for strict mode.
6. [ ] **Golden-image suite** over real presets (crt-royale, a Mega Bezel variant, xBR, ScaleFX, interlacing/history) with pHash + bounded tolerance. *Unchanged — rides M3; the nightly corpus ledger is its prerequisite plumbing.*
7. [ ] **Perf budgets** — define the reference GPU, run crt-royale (never yet run on hardware) and an uncapped-fps ScaleFX 4x measurement, then set §11 numbers. *Hardware-bound; unchanged.*
8. [x] **`trim()` hook** — moved to the post-1.0 lever list (2026-10-18): plan §13 item 7 + PROVENANCE divergence #6 (v1 caches are per-`Chain`, freed wholesale — no incremental trim surface to expose; plan §8 wording updated).
9. [x] **Provenance gate** — `tools/scan_codefindings.py` written (RetroArch fingerprint patterns + `provenance-ok:` inline waivers + `--allow`), repo scans clean; wired as the `provenance` CI job on every push (2026-10-18). Release pipeline wiring stays an M5 item.
10. [x] **CI artifact run verified** — runs #14–#19 green after adoption; artifact from run #18 (`c63b96d`) downloaded and verified (PE32+ dll with glslang 16.6.0, import lib, static lib, headers, commit-pinned manifest). The audit's original "no CI" finding is retracted (hidden-dir search miss); the merged workflow restores the Debug matrix + `vulkan-validationlayers` the first pass had regressed.
11. [ ] **Fallback drill on hardware** — deliberately load a broken preset on the fork and verify `(Unavailable)` → stock renderer → recovery, so the M4 exit's "fallback verified" is backed by an event, not an absence. *Hardware/fork-side; unchanged.*
12. [ ] **Hardware sweep remainder** — feedback double-buffer readers (named + numeric forms), `mipmap_input` chain, live parameter changes, resize/fullscreen toggle (incl. swapchain rebuild), VRAM soak, **real-GPU Linux session** (session 4 covered llvmpipe), validation layer enabled on hardware. *Hardware-bound; Linux software-rasterizer half now done.*
13. [x] **Doc drift sweep** — done 2026-10-18: PLAN §5.1 module map matches reality (note added); risk-3 levers implemented (`SLANGVK_SYSTEM_GLSLANG`, `SLANGVK_LTO`) and risk table updated; M1 divergence #2 + stale sync2 comments rewritten; PROVENANCE max_fbo/histories corpus-note wording corrected.
14. [x] *(Optional)* Windows sanitized build — **declared out of scope for v1** (maintainer decision recorded in the ci.yml header, 2026-10-18): MinGW-w64 ASan support is experimental; the Linux asan+ubsan job covers the whole suite. Revisit only on a Windows-only memory bug.
