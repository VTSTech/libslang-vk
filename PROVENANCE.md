# PROVENANCE

libslang-vk v0.1.0 (M0) — port of the reference host's preset layer.

**Reference host:** `VTSTech/DevilutionX`, branch `master`, commit
`96af9b5fc8f5a4e4aef13e5276ebc604c3eaf299` (docs commit `c665d3b00` carries the
same preset-layer sources).

**Licensing statement:** the ported preset layer was authored inside the
VTSTech/DevilutionX fork by VTSTech. VTSTech holds the copyright and relicenses
these files to MIT as part of libslang-vk (see the file-by-file table). No
upstream `diasurgical/DevilutionX` code is included in this repository; no
RetroArch (GPL-3) code is included — the implementation is clean-room against
the libretro slang documentation, the fork's independent implementation, and
black-box behavior comparison (plan §4). The fork's `scan_codefindings.py`
methodology is the intended CI provenance gate for releases.

## File-by-file

| This repository | Origin (`VTSTech/DevilutionX` @ master) | Change summary |
|---|---|---|
| `include/slangvk/core/types.hpp` | `Source/engine/render/gpu/preset/preset_types.hpp` | Mechanical: namespace `devilution::gpu_scaler::preset` → `slv::preset` |
| `include/slangvk/core/preset_parser.hpp` | `Source/engine/render/gpu/preset/preset_parser.hpp` | Namespace; `slangvk/export.h` annotations; `ShaderCacheDir()` removed (cache is a v1 non-goal); `SetShaderRootDir()` added (SDL-free root override); root-dir doc updated |
| `include/slangvk/core/slang_translate.hpp` | `Source/engine/render/gpu/preset/slang_translate.hpp` | Namespace; export annotations; descriptor-convention doc wording |
| `include/slangvk/core/slang_compile.hpp` | `Source/engine/render/gpu/preset/slang_compile.hpp` | Reduced API: `CompilePass(translation, out, error)` only — sidecar/disk-cache API (`CachePathFor`, `SidecarSpvPathsFor`, `DeleteCachedSpv`, `SpirvSource`) removed by design |
| `include/slangvk/export.h` | new | Visibility macros (MIT, authored for this repo) |
| `include/slangvk/slangvk.hpp` | new | Umbrella header (MIT, authored for this repo) |
| `src/core/compat.hpp` | new, derived | `StrCat` shim (fork `Source/utils/str_cat.hpp`, usage surface), `ParseInt<T>` (fork `Source/utils/parse_int.hpp` std::from_chars core), log-callback replacing `utils/log.hpp` |
| `src/core/preset_parser.cpp` | `Source/engine/render/gpu/preset/preset_parser.cpp` | Mechanical namespace/includes; `USE_GPU_SCALER`/`USE_SDL1` guards removed; `SDL_GetBasePath()` root resolution replaced by `SetShaderRootDir()` (default relative `shaders`); `ShaderCacheDir()` removed |
| `src/core/slang_translate.cpp` | `Source/engine/render/gpu/preset/slang_translate.cpp` | Mechanical namespace/includes; guards removed; fork utils shims via `compat.hpp` |
| `src/core/slang_compile.cpp` | `Source/engine/render/gpu/preset/slang_compile.cpp` | Kept: glslang runtime guard + stage compile/link + `CompilePass`. Removed by design: precompiled-sidecar fast path, content-addressed disk cache (picosha2 hashing, cache version salt, atomic temp-file writer), fork logging calls |
| `cmake/deps.cmake` | `3rdParty/glslang/CMakeLists.txt` | Same glslang 16.6.0 pin + SHA-256 + option set, restructured for FetchContent include; Vulkan-Headers/VMA declarations reserved for M1 |
| `tests/test_preset_parser.cpp` | new | P1 order-independence pin, `#reference` merge pin, scan/listing contracts, sandbox checks, `SetShaderRootDir` checks |
| `tests/test_slang_translate.cpp` | new | Minimal slang → stage split, parameter capture, Source binding, set-2 rewrite |
| `tests/test_slang_compile.cpp` | new | glslang end-to-end: SPIR-V magic/alignment, clean failure paths |
| `tests/test_field_round2.cpp` | `tools/audit/test_field_round2.cpp` | Include paths → `slangvk/core/...`; `using namespace slv::preset`; scratch-dir rename; `SetShaderRootDir(tmp)` for the SEC-01 include sandbox |
| `tests/test_field_round3.cpp` | `tools/audit/test_field_round3.cpp` | Same adaptations as round 2 |
| `tests/test_field_round4.cpp` | `tools/audit/test_field_round4.cpp` | Same adaptations; fixture path via `SLANGVK_TESTS_DATA_DIR`; `SetShaderRootDir(data dir)` |
| `tests/reference/test_field_round1.cpp` | `tools/audit/test_field_round1.cpp` | Verbatim port |
| `tests/reference/test_history_ring.cpp` | `tools/audit/test_history_ring.cpp` | Verbatim port (F5 history-ring direction pin) |
| `tests/reference/test_mipmap_plan.cpp` | `tools/audit/test_mipmap_plan.cpp` | Verbatim port |
| `tests/reference/test_uniform_names.cpp` | `tools/audit/test_uniform_names.cpp` | Verbatim port |
| `tests/reference/test_viewport_derive.cpp` | `tools/audit/test_viewport_derive.cpp` | Verbatim port |
| `tests/reference/test_rotation_format.cpp` | `tools/audit/test_rotation_format.cpp` | Verbatim port |
| `tests/data/field-presets/test/**` | `tools/audit/field-presets/test/**` | Verbatim fixture copy (presets + shaders + include-cycle fixtures) |
| `docs/SPEC.md` | `docs/SLANG_SPEC.md` | Header note adapted to this repository; normative content unchanged |
| `docs/PLAN.md` | `docs/libslang-vk-plan.md` (this project's plan) | Verbatim |

## M1 — Vulkan runner (all new code, MIT, authored for this repo)

The `src/vk/` runner layer, `include/slangvk/runner.hpp` and
`tests/test_runner_headless.cpp` are new implementations written against the
plan's API sketch (§6) and runner design (§8). Semantics that the fork pinned
by test were ported as *semantics*, not code: pass-size resolution with
half-up rounding (`test_viewport_derive`), the aspect-corrected viewport base
(F6), UBO fill order and vec4 write rules (`FillBlockBytes`), the
`Rotation`-fills-0 / FrameDirection-1 / FrameTime-µs conventions, and the
parameter resolution order (preset override → pragma default → zero).

### Vendored dependencies (M1)

| Dependency | Version | License | Purpose |
|---|---|---|---|
| `Vulkan-Headers` | v1.4.364 (SHA-256 pinned in `cmake/deps.cmake`) | Apache-2.0 OR MIT | Vulkan API definitions |
| `volk` | vulkan-sdk-1.4.363.0 (SHA-256 pinned) | MIT | Runtime Vulkan loader (no link-time libvulkan) |
| `VulkanMemoryAllocator` | v3.3.0 (SHA-256 pinned) | MIT | Device memory suballocation |

### M1 documented divergences

1. **MVP orientation (Vulkan-native).** The fork's SDL_GPU host paired
   GL-style UVs with clip-space positions and cancelled per-draw mirrors
   through a FlipY matrix on intermediate passes (field bisects A1/A2/C2/E1).
   The Vulkan runner's fullscreen quad uses unit-quad positions in [0,1] with
   uv = position and an MVP mapping (x,y) → NDC (2x−1, 2y−1); Vulkan NDC
   (−1,−1) is top-left, matching uv (0,0), so no mirror exists anywhere in
   the chain and the flip-cancel dance is unnecessary.
2. **Classic synchronization for the 1.1 floor.** M1 uses core-1.0
   `vkCmdPipelineBarrier` + classic render passes (plan §8's "classic
   fallback" path); the sync2/dynamic-rendering fast path lands with M2's
   capability detection.
3. **Case-insensitive parameter/size matching.** The parser lowercases
   `.slangp` keys (RetroArch behavior); the translator's UBO-member whitelist
   already matched case-insensitively, so the runner's UBO fill, parameter
   metadata merge and `setParameter()` match names case-insensitively for a
   single consistent rule.
4. **Synchronous render.** `Chain::render()` records + submits one command
   buffer and fences (M1 skeleton); batching/timeline semaphores are the M3
   perf work per plan §8.

## M2 — Full chain semantics (all new code, MIT, authored for this repo)

M2 implements the SPEC §5 runtime contract on the Vulkan runner as a
black-box semantic port of the reference host's behavior (field-canary
fixtures reused verbatim; no RetroArch or reference-host code copied).

### M2 semantics (black-box from the reference host + SPEC)

1. **History ring (SPEC 5.2, audit fix F5).** `depth + 1` RGBA8 slots at
   game resolution (depth = max referenced `OriginalHistoryN`); the chain
   input is blitted into the ring before any pass runs; `OriginalHistoryN`
   binds `(slot - N) mod count` = frame T-N; all slots are seeded with the
   current frame on the first render and after every input-size change
   (RetroArch history warmup). Slot bookkeeping happens before descriptor
   writes so bindings reflect the post-advance ring.
2. **Feedback double-buffering (SPEC 5.3, F2).** Demand-driven: a pass read
   through `PassFeedback#` or `<name>Feedback` (case-insensitive, alias or
   `#pragma name`) gets two output buffers; readers bind the buffer not
   being written (frame N-1); buffers flip after the frame. The second
   buffer starts cleared to opaque black. `feedback_pass` stays
   parsed-but-unused per F2.
3. **Final-pass feedback resolve.** A feedback-read final pass renders into
   its own double-buffered target and a 1:1 nearest blit copies the result
   into the host output image (requires the new `Offscreen::image`); other
   final passes keep rendering into the host target directly.
4. **Mipmap refill (SPEC 5.4, audit fix F1).** Pass outputs read by a
   `mipmap_input` pass allocate full mip chains and are refilled between
   render passes with a linear blit chain; pass 0 reads the chain input
   through a mip-capable RGBA8 copy blitted and refilled every frame. Mip
   filter linear, min/mag per `filter_linearN`; history/feedback/alias
   bindings keep the nearest/clamp default sampler (deliberate divergence,
   SPEC 5.4).
5. **Sampler policy (SPEC 5.4).** Only `Source` uses the reading pass's
   filter/wrap/mipmap; `Original`, `OriginalHistory#`, `PassOutput#`,
   `PassFeedback#`, `<name>Feedback` and plain aliases bind
   nearest/clamp-to-edge. (M1 bound every name with the reading pass's
   sampler; the field fixtures were 1:1, masking the difference.)
6. **UBO fix.** Alias/`#pragma name` `<Name>Size` members resolve to the
   OWNING pass's output size (M1 used the reading pass's size — only
   invisible when every pass rendered at the same resolution).
7. **Live parameters.** `setParameter()` updates case-insensitively in
   place (case-variant duplicates in the override map would otherwise
   merge in hash order); the merged values land in the params UBO on the
   next render.

### M2 documented divergences

1. **LUT textures were load-gated (lifted in M3).** Presets declaring
   `textures =` failed `Preset::load` during M2; M3 ships the decode +
   upload path (next section).
2. **`Rotation` semantic stays 0.** The UBO member is filled with the
   identity value 0 (ported semantic); `InputFrame::rotation != None`
   returns `unsupported_feature` until the M4 presentation path.
3. **Non-causal alias taps fall back.** A plain alias / `#pragma name`
   tap of a LATER pass binds the current source texture with a one-shot
   stderr warning (SPEC 4.5 runtime fallback, "never a null bind").

## M3 — LUT texture upload + conformance scaffolding (all new code, MIT, authored for this repo)

M3 wires the deferred M2 capability (SPEC §3.5 lookup textures) into the
runner and adds the plan's M3 conformance scaffolding (corpus sweep tool).
The implementation is spec-driven and clean-room: decode + RGBA8 upload +
mip generation + name/`User#` binding semantics are written from the SPEC
text, not from RetroArch or the reference host.

### Vendored dependencies (M3)

| Dependency | Version | License | Purpose |
|---|---|---|---|
| `stb_image.h` | v2.30 (SHA-256 `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`, pinned copy in `vendor/stb/`) | Public domain / MIT dual | LUT image decode (PNG/JPEG/BMP/TGA/PSD/GIF/PNM → RGBA8) |

### M3 semantics

1. **Upload (SPEC 3.5).** LUT files decode to tightly packed RGBA8 and
   upload as `R8G8B8A8_UNORM`, sampled with the LUT's own
   `name_linear`/`name_wrap_mode` options. `name_mipmap = true` generates
   the full mip chain at upload (linear blit chain, shared with the M2
   `mipmap_input` machinery); LUTs are static, so the chain is built once
   at load, never per frame.
2. **Binding (SPEC 4.5/5.4, F9).** Fragment samplers resolve by declared
   LUT name case-insensitively, or positionally via `User#` over the
   declared list. The parser now materializes `preset.textures` in
   FIRST-DECLARATION order (it previously sorted by the lowercase map,
   which silently renumbered `User#` on unsorted lists) — pinned by a core
   parser test and the `lut_user.slangp` golden ("zeta;alpha": User0 must
   bind zeta, not the alphabetically-first alpha).
3. **`<LutName>Size` uniforms (SPEC 4.7).** The params UBO resolves
   LUT-size members (w, h, 1/w, 1/h) from the uploaded image. `User#Size`
   resolves to the positional LUT's size — an extension over the reference
   host, which left it at (0, 0) (SPEC 4.7 "Unimplemented"; no corpus
   preset observed using it).
4. **Load-path hardening.** The translator's known-name universe now
   carries lowercase forms alongside declared spellings (the translator
   validates against lowercased names; only-lowercase fixtures had masked
   the gap for mixed-case LUT names — F9 case-insensitive matching), plus
   `User#` aliases. `git-lfs` pointer files fail the load with the SPEC
   3.5 "POINTER file, not image data" diagnostic; missing/oversized/corrupt
   files fail with named errors (256 MiB cap, ROB-style).
5. **Validation-gate fix (M1 latent bug).** `Device::create` never enabled
   `VK_EXT_debug_utils`, and `Device::adopt` created its messenger before
   `volkLoadInstance` — in both paths the debug messenger silently never
   existed, so layer output reached stderr while `validationErrors()`
   stayed empty and the tests' zero-validation gate was vacuously green
   (proven by a deliberate-error probe that collected 2 VUIDs). Both paths
   now create the messenger after the volk tables bind and request the
   extension when validation is on; the gate is live for all runner tests.
6. **Corpus sweep (`examples/corpus_sweep.cpp`).** The plan §11 M3
   conformance tool: walks preset directories through the real load path
   (parse → translate → compile → LUT upload), optionally renders frames
   on the device (`--render`, 2 frames to exercise feedback), prints a
   per-preset ledger with named errors, and fails on any validation-layer
   error. Bundled-fixture smoke runs in ctest; the full shader-pack corpus
   sweep runs out of CI against a slang-shaders checkout.

### M3 documented divergences

1. **Decode surface.** stb_image replaces SDL_image: PNG/JPEG/BMP/TGA/PSD/
   GIF/PNM decode; `.dds`/`.webp`/`.exr` LUTs fail with an explicit error
   listing the supported formats (the reference host's SDL_image accepted
   them; corpus impact not observed).
2. **8-bit precision.** LUTs upload as R8G8B8A8_UNORM (matching the
   reference host's shipping path); float LUT preservation is a post-1.0
   lever (SPEC 3.5 "Partial").
3. **`User#Size` resolves.** Extension over the reference host (see
   semantics §3); SPEC matrix updated accordingly.
4. **LUT views are borrowed by chains.** A `Preset` must outlive every
   `Chain` created from it (new documented API contract; LUT GPU objects
   live in `Preset::Impl`, chains reference the views).

## Documented divergences (v1 decisions, plan §2/§5/§13)

1. **No SPIR-V disk cache / sidecars.** v1 compiles with glslang on every
   load; only in-`Preset` memoization applies. The fork's cache machinery is
   documented here and can be reintroduced post-1.0 if cold-start cost
   warrants it.
2. **SDL-free shader root.** `ShaderRootDir()` defaults to a relative
   `shaders` folder; hosts call `SetShaderRootDir()` (typically
   `<exe dir>/shaders`, which the reference host derived internally via
   `SDL_GetBasePath()`).
3. **Namespace.** `devilution::gpu_scaler::preset` → `slv::preset`; the public
   runner API (namespace `slv`) lands in M1 above this layer.
4. **Descriptor conventions unchanged.** The translated set/binding layout
   (vertex UBOs set 1, fragment textures set 2, fragment UBOs set 3, push
   block re-homed as std140 UBO) is kept byte-compatible so the reference
   host can adopt the library without a shader-pack migration.

## Verification record

### M0 exit
- Release build (static + shared), Linux GCC 14.2, CMake Ninja: **12/12 tests pass**.
- Debug + ASan/UBSan build: **12/12 tests pass** (sanitizers active incl.
  LeakSanitizer across the glslang compile path).

### M1 exit
- Release build (static + shared), Linux GCC 14.2, CMake Ninja: **15/15 tests pass**
  (M0 suite + example smoke tests + `runner_headless`).
- `runner_headless` on **lavapipe** (llvmpipe, Mesa 25.0.7, API 1.4): golden blit
  round-trip byte-identical, two-pass alias chain byte-identical, parameter-gain
  golden within tolerance, **zero validation-layer errors** with
  `VK_LAYER_KHRONOS_validation` active.
- Debug + ASan/UBSan build: **15/15 tests pass** (sanitizers active across the
  Vulkan execution path on lavapipe).
- "and a real GPU" half of the M1 exit criterion: the headless test is
  device-agnostic (skips cleanly without an ICD); hardware runs happen on the
  maintainer's machines and in the fork's field loop (plan §10).

### M2 exit
- Release build (static + shared), Linux GCC 14.2, CMake Ninja: **16/16 tests pass**
  (M0 suite + example smoke tests + `runner_headless` + new `runner_m2`).
- `runner_m2` on **lavapipe**: the fork's field canaries pass on the Vulkan
  runner — feedback ping-pong (frame 1 = src*0.5 from the cleared buffer,
  frame 2 = src*0.75 proving the previous-frame bind), history ring with gap
  taps OriginalHistory1+3 (frames 2 and 4 pin T-N indexing and seeding),
  3-pass format chain through an R16F FBO, r4 include/cycle preset renders
  with the pinned tint golden, mip refill = exact box average at level 6,
  resize re-seeds the ring and recreates FBOs, live case-insensitive
  parameters — **zero validation-layer errors** throughout.
- `feedback-noncausal` negative control: `Preset::load` rejects with
  "PassOutput0 is not causal in pass 0" (translator causality gate).
- Debug + ASan/UBSan build: **16/16 tests pass** (sanitizers active across
  the Vulkan execution path on lavapipe).
- Core-only build (`SLANGVK_ENABLE_VK=OFF`): **12/12 tests pass**.

### M3 exit
- Release build (static + shared), Linux GCC 14.2, CMake Ninja: **18/18 tests
  pass** (M2 suite + new `runner_m3` + `corpus_sweep` smoke).
- `runner_m3` on **lavapipe**, byte-exact goldens: named-LUT 1:1 sample ==
  tint.png texels; `User0` binds the FIRST declared texture (declaration-order
  golden, catches the map-order regression); `<tint4>Size` UBO fill encodes
  vec4(4, 4, 0.25, 0.25); mipmap LUT explicit LOD 1 = 1x1 average (±1); missing
  file / git-lfs POINTER / `noLutImages` bind all fail with named errors;
  **zero validation-layer errors with the now-live collector**.
- Validation-gate integrity: a deliberate-error probe collects 2 VUIDs through
  `Device::validationErrors()` (the gate previously never fired — see M3
  semantics §5); the M1/M2 render paths were re-verified clean under the live
  gate.
- `corpus_sweep --render --lut` on the LUT fixtures: 4/4 load, 4/4 render on
  lavapipe, no validation errors; compile-only smoke over bundled fixtures in
  ctest (--fail-on-broken).
- Debug + ASan/UBSan build: **18/18 tests pass**.
- Core-only build (`SLANGVK_ENABLE_VK=OFF`): **14/14 tests pass** (runner
  tests + corpus_sweep correctly excluded; Vulkan headers now propagate for
  the public header in core-only builds).
