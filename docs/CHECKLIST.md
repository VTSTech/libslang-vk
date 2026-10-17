# CHECKLIST — M0–M4 implementation status

| | |
|---|---|
| **Date** | 2026-10-18 (working tree ahead of HEAD `4ae7268`) |
| **Audited against** | `docs/PLAN.md` §2 goals, §5.1 module map, §6 API sketch, §8 runner design, §10 testing strategy, §11 milestone table; `PROVENANCE.md` per-milestone records + hardware field log; `tests/`, `.github/workflows/ci.yml`, `src/`, `include/` |
| **Purpose** | Item-by-item audit so nothing is claimed that the repo doesn't back |

Legend: ✅ done + verified · 🔶 partial (evidence on one side only) · ⬜ open · ⚠️ drift (docs claim ≠ code).
Where this file and PROVENANCE.md disagree, PROVENANCE.md is authoritative and this file's ⚠️ rows are the corrections to make.

---

## Evidence base (what every status below cites)

**19/19 ctest** (lavapipe, Release GCC 14.2 — PROVENANCE M4 exit):
`preset_parser` · `slang_translate` · `slang_compile` · `field_round1..4` · `history_ring` · `mipmap_plan` · `uniform_names` · `viewport_derive` · `rotation_format` · `runner_headless` · `runner_m2` · `runner_m3` · `runner_m4` · `example_preset_info_demo-bright` · `example_preset_info_demo-glow` · `example_corpus_sweep_smoke`.
Also verified per milestone exit: ASan/UBSan build (19/19), core-only `SLANGVK_ENABLE_VK=OFF` (14/14), live validation-error gate (proven non-vacuous by a deliberate-error probe collecting 2 VUIDs).

**CI** (`.github/workflows/ci.yml`): running since M0 — 19 runs by 2026-10-17 (original: Linux Release/Debug matrix on lavapipe **with `vulkan-validationlayers` installed**, asan/ubsan job, mingw-w64 job; MSVC job dropped by maintainer decision 2026-10-17). 2026-10-18: artifact-upload extension adopted — runs #14–#19 green; **artifact from run #18 (commit `c63b96d`) verified end-to-end**: PE32+ x86-64 `libslangvk.dll` 6,263,032 bytes with glslang 16.6.0 linked in, `libslangvk.dll.a` import 44,442 B, `libslangvk.a` static 1,808,164 B (no glslang embedded, per contract), full header set, commit-pinned `ARTIFACTS.txt`. Audit note: a 2026-10-18 search missed the hidden `.github/` directory and reported "no CI in repo" — wrong; what CI lacked was artifact uploads, not CI. The first artifact-upload ci.yml pass regressed the Debug matrix and validationlayers package; the merged workflow restores both.

**Hardware field log** (PROVENANCE, RX 560 / Windows, fork adapter builds `338827ff4`→`ff3999a6f`): 3 sessions / 4 host runs — 23 loads over 20 presets, max 26 passes, 9,093 frames, zero skipped/fallbacks/validation output/crashes.

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
| CI sanitizers | ✅* | asan+ubsan job running since the original workflow. *Windows sanitized build: not attempted — declare out-of-scope?* |
| Validation layers in CI | ✅/🔶 | Original CI installed `vulkan-validationlayers`; the 2026-10-18 ci.yml replacement dropped the package (validation gate vacuous on CI) — **restored in the merged workflow**. Synchronization validation (plan §10) still not configured → miss list #2 |
| Exit: core trio passes ported unit tests on CI | ✅ | Passing on CI since M0 (12/12 then, 19/19 now) |

## M1 — Vulkan runner skeleton (plan §11)

| Item | Status | Evidence / gap |
|---|---|---|
| `Device::adopt` / `Device::create` | ✅ | `adopt` exercised on hardware by the fork adapter; `create` on lavapipe; surface-extension lists added in M4 |
| Single-pass offscreen execution | ✅ | `runner_headless`; `Offscreen` mode is also the CI conformance vehicle (§5/§8) |
| Golden blit round-trip | ✅ | Byte-identical on lavapipe |
| Exit: "headless on lavapipe **and** a real GPU" | 🔶 | lavapipe half ✅; real-GPU half rides the fork's field loop (documented ownership) — 9,093 frames of RX 560 evidence through the full path |
| Classic render passes (1.1 floor) | ✅ | What ships — everywhere |
| **sync2 / dynamic-rendering preferred path (§8)** | ⚠️ | **Never shipped.** M1 divergence #2 promised it "with M2's capability detection"; `src/vk/chain.cpp:41` says "Not yet"; `src/vk/images.cpp:186` comment still says "M2 introduces" (stale). No PROVENANCE divergence entry covers the deferral. lavapipe supports dynamic rendering → CI exercises only the fallback path → miss list #1 |
| Push constants (pass matrices) / per-pass descriptor sets | ✅ | PROVENANCE M1; §8 contract |
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
| Load-path hardening (F9 case, git-lfs detect, 256 MiB cap) | ✅ | M3 §4, named-error tests |
| Validation-gate integrity fix | ✅ | Was vacuously green; probe collects 2 VUIDs; M1/M2 re-verified under live gate |
| `corpus_sweep` tool | ✅ | `examples/corpus_sweep.cpp`, `--render --lut --fail-on-broken`; smoke in ctest |
| **Full-corpus CI ledger (2,658 presets, CSV, no-regression policy)** | ⬜ | §10/§11 M3. Tool ready; CI does not run it (needs slang-shaders checkout); only local partial-corpus greps done → miss list #5 |
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
| Adapter: `gpu_scaler` interface preserved, dual-path, browser/ini/OSD | 🔶 | Underway on fork branch per §9; working in 3 hardware sessions |
| Exit: hardware field sweep **Windows + Linux** | 🔶 | Windows half strong (see evidence base; plus field-verified: vsync on/off, integer scaling on/off, two ShowProgress level loads w/o Reinit). **Linux session: ⬜ not run** |
| Exit: "fallback verified" | ⚠️ | Field log says "no fallbacks" — i.e. never triggered. **The `(Unavailable)` fallback has not been deliberately provoked on the lib path** (broken preset → stock renderer + recovery) → miss list #11 |
| Exit: SDL_GPU path untouched | ✅ | Dual-path per §9; final A/B convergence check rides the spike gate |
| Hardware remainder (PROVENANCE still-open) | ⬜ | Feedback readers, `mipmap_input`, live params, resize/fullscreen toggle, VRAM-teardown soak, Linux session, validation layer on hardware |

---

## Cross-cutting contract claims (§2 goals, §8 design, §4 policy)

| Claim | Status | Note |
|---|---|---|
| Goal 1: full spec support incl. `max_fbo_*` clamping | ⚠️ | **`max_fbo_width/height` implemented nowhere** (`src/` + `tests/` grep: zero). 0 corpus users (2026-10-14 audit), but Goal 1 claims it and §8 says "honor … clamps"; PROVENANCE field-log calls max_fbo/histories "lib-tested paths" — inaccurate for max_fbo → miss list #3 |
| `histories` key | ⚠️ | Parsed + clamped 1..8 (`preset_parser.cpp:468`) but the ring derives depth from max referenced `OriginalHistoryN`; the key is unused at runtime, untested, undocumented → miss list #4 |
| Goal 1: parser/translate/compile semantics | ✅ | Corpus-validated in the fork's campaign; order-independence (P1) + causality gates ported |
| Goal 2: Vulkan-only, 1.1 floor | ✅ | Classic path only (see M1 sync2 row) |
| Goal 3: host integration (`adopt`, no SDL) | ✅ | `gpu_scaler` contract §9 |
| Goal 4: C++23 `std::expected` API | ✅ | §6 sketch delivered: `strict`, `noLutImages`, `parameterInfo()`, `resize()`, `setParameter()` all exist |
| Goal 6: dual distribution | ✅ | Now distributed per toolchain by CI artifacts (same-toolchain ABI documented, `export.h`) |
| §8: VMA-backed allocation, stage-masked barriers | ✅ | PROVENANCE M1/M2 |
| §8: sampler cache bounded+keyed / `trim()` hook | 🔶 | Sampler cache in; **`trim()` hook absent** (grep: only the string `Trim()`) — document as post-1.0 or add → miss list #8 |
| §8: timeline semaphores | ✅ | Explicitly "reserved for future async-upload path" — not a gap |
| §4: `scan_codefindings.py` provenance CI gate before every release | ⬜ | No `tools/` in repo; must exist before the 1.0 tag (risk 1) → miss list #9 |
| §5.1 module map (`src/interop/`, `corpus/`, `tools/`) | ⚠️ | None exist as such — folded into `runner.hpp` / `tests/data/` / `examples/`. Doc drift; fix the map, not the repo |
| Risk 3 levers: `SLANGVK_SYSTEM_GLSLANG`, LTO/`-Oz` | ⬜ | Unimplemented, untracked — fold into M5 packaging or strike from risk 3 |

## M5 boundary (listed for completeness — not started)

⬜ API freeze · ⬜ CMake package config + pkg-config · ⬜ `docs/API.md` · ⬜ provenance gate in release pipeline · ⬜ tagged release + example app.

---

## The miss list (actionable, priority order)

1. [ ] **sync2/dynamic-rendering path** — implement capability-gated preferred path (lavapipe would then exercise it in CI) **or** record it as a v1 divergence in PROVENANCE. Either way: fix the stale "M2 introduces" comments (`chain.cpp:41`, `images.cpp:186`) and the M1 divergence #2 promise. *(The silent-bug risk here is low — classic path is correct — but the docs currently promise a perf path that doesn't exist.)*
2. [ ] **Synchronization validation** in the headless gate (plan §10): enable `VkValidationFeaturesEXT` sync-validation with `VK_LAYER_KHRONOS_validation` in `Device::create`'s validation config, re-run 19/19 + ASan. Barrier bugs are the "renders but wrong" class' closest sibling.
3. [ ] **`max_fbo_width/height` clamps** — implement (cheap: clamp FBO dims in scale resolution, one test) or strike from Goal 1/§8/SPEC matrix. Either way correct the PROVENANCE "lib-tested paths" wording.
4. [ ] **`histories` key** — pin the parse+clamp with a parser test and document the derived-ring-depth decision (depth = max referenced tap; extra declared slots ignored).
5. [ ] **Full-corpus CI ledger** — nightly/scheduled job: shallow `libretro/slang-shaders` clone → `corpus_sweep --fail-on-broken` → CSV artifact. The no-regression policy can't start until a baseline ledger exists.
6. [ ] **Golden-image suite** over real presets (crt-royale, a Mega Bezel variant, xBR, ScaleFX, interlacing/history) with pHash + bounded tolerance.
7. [ ] **Perf budgets** — define the reference GPU, run crt-royale (never yet run on hardware) and an uncapped-fps ScaleFX 4x measurement, then set §11 numbers.
8. [ ] **`trim()` hook** — implement or move to the post-1.0 levers list (§13).
9. [ ] **Provenance gate** — port `scan_codefindings.py` into `tools/` + a CI job (release-gating; required before 1.0).
10. [x] **CI artifact run verified** — runs #14–#19 green after adoption; artifact from run #18 (`c63b96d`) downloaded and verified (PE32+ dll with glslang 16.6.0, import lib, static lib, headers, commit-pinned manifest). The audit's original "no CI" finding is retracted (hidden-dir search miss); the merged workflow restores the Debug matrix + `vulkan-validationlayers` the first pass had regressed.
11. [ ] **Fallback drill on hardware** — deliberately load a broken preset on the fork and verify `(Unavailable)` → stock renderer → recovery, so the M4 exit's "fallback verified" is backed by an event, not an absence.
12. [ ] **Hardware sweep remainder** — feedback double-buffer readers (named + numeric forms), `mipmap_input` chain, live parameter changes, resize/fullscreen toggle (incl. swapchain rebuild), VRAM soak, Linux session, validation layer enabled on hardware.
13. [ ] **Doc drift sweep** — PLAN §5.1 module map; M1 divergence #2 wording; PROVENANCE max_fbo/histories "lib-tested" wording; risk-3 levers tracked or struck.
14. [ ] *(Optional)* Windows sanitized build — declare in-scope or note out-of-scope in the CI header comment.
