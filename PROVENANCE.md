# PROVENANCE

libslang-vk v0.1.0 — provenance ledger for the M0 port of the reference
host's preset layer and for the M1–M4 runner milestones authored in this
repository (working milestone numbers; see docs/PLAN.md §11 for the mapping
onto the plan's milestones).

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
2. **Classic synchronization for the 1.1 floor.** M1 used core-1.0
   `vkCmdPipelineBarrier` + classic render passes (plan §8's "classic
   fallback" path). The M1 note promising the sync2/dynamic-rendering fast
   path "with M2's capability detection" was NOT fulfilled — M2–M4 shipped
   classic-only everywhere. Promoted to a formal v1 divergence 2026-10-18
   (miss-list #1): classic path is the verified conformance vehicle
   (byte-exact goldens, live validation gate, 9,093 hardware frames);
   sync2 barriers + dynamic rendering are recorded as post-1.0 perf levers
   (plan §13), not a pending milestone item. Stale "M2 introduces"
   comments fixed (`chain.cpp`, `images.cpp`).
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

## M4 — Presentation: swapchain output, letterbox, rotation (all new code, MIT, authored for this repo)

M4 implements plan §8 output mode (a) — the DevilutionX presentation mode:
the final pass draws into an acquired swapchain image, the host owns
acquire/present, and the library handles letterbox rect + orientation.
The letterbox geometry is black-box ported from the reference host's
present blit (`gpu_scaler.cpp`: fit scale → lround → overshoot clamp →
center); rotation semantics are defined from the SPEC (`Rotation` uint
0..3, gap #5) with the rotation applied by the presentation draw.

### M4 semantics

1. **Swapchain targets (`SwapchainFrame`).** The host acquires
   (`vkAcquireNextImageKHR`, optionally signaling `acquireSemaphore`),
   calls `render()`, then presents. The library waits the acquire
   semaphore at `VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT`, records
   the frame plus a present draw (final layout `PRESENT_SRC_KHR`), and
   fences — so the host presents immediately after `render()` returns,
   with no extra wait semaphore. The chain's pass FBOs keep rendering at
   their preset-derived sizes; only the present draw touches the swapchain
   image (cached framebuffer per swapchain view, per-format render pass +
   pipeline).
2. **Letterbox.** The final pass output is drawn into the output target as
   a quad: fit scale = min(tw/cw, th/ch), dest = lround + overshoot clamp,
   integer-centered (reference-host math); explicit rects come through
   `SwapchainFrame::outputRect` (validated inside the target). Zero rect =
   fit + center. Margins are the render pass clear (opaque black), the
   sampling filter is linear + clamp-to-edge — exactly the reference
   host's present blit contract. Offscreen targets take the same path when
   the final pass does not resolve to the target size (the M2 hard error
   is gone) or when rotation is set; equal sizes at rotation None keep the
   byte-exact direct path.
3. **Rotation.** The chain renders the input in source orientation and
   every stage's `Rotation` uniform carries `InputFrame::rotation`
   (SPEC gap #5 semantic; previously pinned 0). The present draw rotates
   the final output clockwise into the dest rect (uv mapping per quarter
   turn, pinned by byte-exact goldens for Cw90/180/270) and sizes the fit
   rect from the rotated aspect. The fork's SDL_GPU runner has no rotation
   source (frontend never rotates); this is an extension defined from the
   SPEC, not a port.
4. **Per-format pass pipelines.** Pass pipelines are cached per
   render-pass format: the final pass renders into its own FBO (pass
   `#pragma format`) under the present path but straight into the host
   target (host format) on the direct path — the two can differ, and a
   pipeline is only compatible with one. The default variant is built at
   `Chain::create` (load-time validation); other formats build lazily at
   first use. The present draw has its own per-format pipeline + render
   pass pair (UNDEFINED → `PRESENT_SRC_KHR` for swapchain images).
5. **`Image::update`.** Hosts upload the converted game frame every
   present (plan §5.2) through a synchronous staging copy; the image rests
   in its creation layout before/after. The DevilutionX adapter uses this
   per frame.
6. **Surface-capable `Device::create`.** `DeviceOptions` gained verbatim
   `instanceExtensions`/`deviceExtensions` lists so internally created
   contexts can opt into `VK_KHR_surface` + a platform surface and
   `VK_KHR_swapchain` (the fork adapter creates its own device, so this
   matters only for hosts/tests that create internally).

### M4 documented divergences

1. **Rotation at presentation, not per pass.** RetroArch applies the
   frontend rotation through the final pass MVP; the reference host has no
   rotation source at all. libslang-vk renders the chain in source
   orientation (shaders read `Rotation` to adapt) and rotates only the
   present draw — the chain output stays byte-identical across rotation
   values, which keeps golden contracts and the swapchain path
   independent.
2. **Synchronous swapchain frames.** v1 is single-threaded per `Chain`
   (plan §5): acquire/render/present happen on the host thread with the
   library fence as the only sync. Acquire/present semaphores from plan
   §8 reduce to the acquire semaphore wait; async presentation is a
   post-1.0 lever.
3. **No integer-scaling option.** The reference host floors the fit scale
   when its Integer Scaling option is set; the library always fits
   continuously and hosts that want integer scaling pass an explicit
   `outputRect` (the adapter keeps the option host-side).

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
5. **No sync2 / dynamic-rendering fast path (vulkan runner).** Classic
   render passes + core-1.0 barriers everywhere (M1 divergence #2, promoted
   2026-10-18). Deferred as post-1.0 perf levers (plan §13); no capability
   gating exists in v1 — the Vulkan 1.1 floor is the only requirement.
6. **No `trim()` memory hook.** Plan §8's "`trim()` hook for host memory
   pressure" is not implemented: every cache (samplers, render passes,
   framebuffers, pipelines) is per-`Chain` and freed wholesale in the
   destructor, so there is no incremental memory to trim in v1. A `trim()`
   surface (e.g. VMA defragmentation + stale-framebuffer sweep after
   resize storms) is a post-1.0 lever (plan §13) — added there 2026-10-18
   instead of shipping an untestable hook.

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

### M4 exit
- Release build (static + shared), Linux GCC 14.2, CMake Ninja: **19/19 tests
  pass** (M3 suite + new `runner_m4`).
- `runner_m4` on **lavapipe**, byte-exact goldens: fit-and-center letterbox
  (96x64 content in a 96x128 target, scale-1 rect, black bands); rotation
  Cw90/180/270 (32x16 content in a 32x32 target, per-quarter-turn texel
  mapping); `Rotation` uniform encodes the input frame's rotation value;
  `Image::update` re-upload round-trip; and the full negative matrix (null
  swapchain view, zero size, UNDEFINED format, size mismatch, out-of-bounds
  rect, offscreen render on a swapchain-only chain). **Zero validation-layer
  errors.**
- **Swapchain end-to-end on `VK_EXT_headless_surface`** (lavapipe presents
  BGRA8, minImageCount honored): three frames acquired → rendered →
  presented through the library (acquire semaphore waited at
  COLOR_ATTACHMENT_OUTPUT, images rest in PRESENT_SRC), read back and
  compared byte-exact against the offscreen reference — auto fit rect
  (16,0,96,64) and explicit rects (32,0) / (0,0). The case skips cleanly
  when the surface or swapchain extension is unavailable (Windows CI has no
  ICD; mingw still compiles the full suite).
- Debug + ASan/UBSan build: **19/19 tests pass** (sanitizers active across
  the swapchain + present path on lavapipe). (Test count is now 20 with the
  2026-10-18 `runner_pushblock` canary; the sanitizer re-run rides CI.)
- Core-only build (`SLANGVK_ENABLE_VK=OFF`): **14/14 tests pass**.
- Hardware half of the DevilutionX-mode exit: owned by the fork's M4
  adapter spike, underway on branch `libslang-vk` (dual-path with the
  SDL_GPU runner; field loop on Windows + Linux). The headless swapchain
  test is the device-agnostic CI gate for the same code path; the hardware
  sweep closes the spike per plan §11.

### CI (GitHub Actions)

CI has run on this repository since the M0 scaffolding commit (workflow
history: `25ce03d` M0 → `f1bd2b8`/`aed9cef` M1 mingw64 → `b7c6583` M3). The
original workflow carried the maintainer's 2026-10-17 decision to drop the
VS-generator MSVC job (5–8x slower than Ninja; hung the runner past any
plausible test runtime) — Windows coverage stayed on mingw-w64. An audit on
2026-10-18 missed the hidden `.github/` directory and reported "no CI in
repo"; that finding is retracted — what CI lacked was artifact uploads, not
CI, and that is what the 2026-10-18 extension added (per-toolchain
distribution per plan §6/export.h: `libslangvk.a`/`.so` + headers on Linux
Release; `libslangvk.dll` + `libslangvk.dll.a` + `libslangvk.a` + headers on
win64 MinGW-w64; every zip carries a commit-pinned `ARTIFACTS.txt`). The
first artifact-upload ci.yml pass regressed two properties of the original
(the Debug build type, the `vulkan-validationlayers` install — without it
the runner tests' validation gate is vacuous on CI); the merged workflow
restores both and preserves the MSVC-drop decision.

First artifact verified end-to-end: run for commit `c63b96d` (main) —
`libslangvk.dll` is a PE32+ x86-64 image, 6,263,032 bytes, with glslang
16.6.0 linked in; `libslangvk.dll.a` 44,442 bytes; `libslangvk.a`
1,808,164 bytes (no glslang embedded, per contract); complete public header
set; `ARTIFACTS.txt` manifest matches the run.

### Syncval gate bites: CI red run + fix batch (2026-10-18)

The merged workflow restored `vulkan-validationlayers`; the same day's
state also enabled instance-level synchronization validation (plan §10) and
added the maintainer's `runner_pushblock` field canary (the stock libretro
shape: global MVP UBO + a push-constant block read by the fragment stage).
The next CI run failed 5 of 20 tests — every Vulkan runner test — and the
failures were real, in three families:

1. **The re-homed push/params UBO was never bound (field bug).** The
   translator rewrites `push_constant` blocks into binding 1 of the stage
   UBO set (set 1 vertex / set 3 fragment), but the runner's set layouts
   only declared binding 0, only allocated/filled one UBO per stage and
   only wrote binding-0 descriptors. Every field shader reading `params`
   from the fragment stage (fxaa/smaa shape) rendered with zero-filled
   uniforms — invisible to the bundled canaries because glslang
   dead-code-eliminates unused push blocks, and invisible to lavapipe
   itself, which does not validate the shader/pipeline-layout interface
   (exactly the strict-driver failure the canary's header comment predicts:
   `VK_ERROR_INVALID_SHADER_NV` on AMD). Validation surfaced it as
   VUID-VkGraphicsPipelineCreateInfo-layout-07988 +
   VUID-vkCmdDraw-None-08114. Fix: binding-0/1 set layouts, per-block UBO
   allocation + descriptor writes, per-block per-frame uniform fill
   (`chain.cpp`).
2. **Zero-access layout-transition barriers.** Layout transitions are
   writes; the upload (`Image::create`/`update`), readback
   (`Device::readPixels`), mip-refill (`BarrierMipLevel` — which accepted
   access-mask parameters and silently dropped them), LUT-upload and
   pass-target-init barriers all carried `srcAccessMask = dstAccessMask =
   0`, so every `CopyBufferToImage` / `CopyImageToBuffer` / `BlitImage`
   against a freshly transitioned image was a validation hazard
   (READ_AFTER_WRITE / WRITE_AFTER_WRITE, 50 instances in one run). Fix:
   correct stage/access masks at every hand-rolled barrier site
   (`images.cpp`, `lut.cpp`, `chain.cpp`).
3. **Test-side present ordering.** `runner_m4` read back presented
   swapchain images after a host `vkDeviceWaitIdle` — spec-legal, but
   synchronization validation does not model host idle. Restructured onto
   an acquire-semaphore chain: one semaphore per re-acquire (a left-
   signaled semaphore must never feed the next acquire), duplicates on a
   headless presenter are dropped rather than re-presented, the readback
   transition matches WSI's acquired-images-read-UNDEFINED model, and the
   sweep is attempt-bounded with a finite acquire timeout (a headless
   presenter may never retire images).

Reproduction method (no sudo in the dev environment): A/B runs on a
user-space Vulkan stack — SwiftShader ICD extracted from the local
agent-browser Chrome bundle (ICD json `api_version` patched to 1.3) ×
user-space `vulkan-validationlayers` deb extraction, selected via
`VK_DRIVER_FILES` / `VK_LAYER_PATH`. Run A (no layers) isolated defect 1
through the pushblock golden (16,253-byte diff = zero-filled params); Run B
(layers) reproduced all five CI failures exactly; after the fix both runs
are 20/20. The gate stays on: it converted three silent-wrongness families
into hard failures on its first run — exactly what plan §10 bought it for.

Landing status: the chain.cpp UBO-binding core and an improved
`VkResultToString` (numeric fallback for unmapped result codes, so
driver-specific failures like the AMD pipeline rejection above log the
actual code) landed upstream as the maintainer's `6e693ec`; the completion
overlay carries the remaining pieces — chain.cpp barrier access masks,
images.cpp / lut.cpp upload·readback·mip·LUT barriers, the maxfbo preset's
binding-1 params tap, the runner_headless check relaxation, the runner_m4
acquire-semaphore present restructure, and these docs. Post-integration
verification on `6e693ec` + overlay: Run A 20/20, Run B (syncval) 20/20
with zero validation messages, ASan/UBSan 17/17.

### Pass-cap field fix + corpus baseline pinned (2026-10-18)

**Field fix: preset pass cap (ROB-02) raised 32 → 128.** A hardware session
(AMD RX 560, Windows, nonstatic fork build) failed to load the
tool-generated mega-preset `crt-adamant-default-ntsc` with
"preset pass index 93 exceeds the limit of 32"; the rest of the session was
healthy. The bound's real job is ROB-02 (a hostile `shader2000000000 = x`
key must not drive the lazy `passes.resize()` into a multi-gigabyte
allocation), but the corpus now contains generated chains far beyond the
old sanity value, so the guard was hitting real content. The cap moves to
128 (known corpus max ≈ 93–94, headroom included), the comment records why,
the `#reference` merge path stays intentionally unbounded (RetroArch
parity; resource failures already produce named errors), and boundary
tests pin the semantics: a sparse out-of-order 40-pass preset parses
(exercising the lazy resize), `shader128` still rejects with the documented
message shape, and `shader2147483647` keeps failing (ROB-02 regression
check). A sweep of chain.cpp's refactored UBO loops turned up four silent
`if (block.binding >= 2) continue;` guards (two write, two fill — the fill
ones index `vertexUbos[]`/`fragmentUbos[]` and would have been OOB, not
just skipped); they are hard `std::unexpected` errors now, so a future
translator emitting an out-of-range binding fails loudly instead of
re-creating the inscrutable-driver-rejection failure mode.

**Miss list #5 closed: corpus baseline pinned, nightly gate on.** The
`corpus_sweep` tool grew machine ledger support — `--ledger FILE` (sorted
TAB records: status, pass count, parameter count, path; `#` header carries
tool, corpus revision, mode, UTC date, totals; error text stays on stdout
so the committed file is stable and provenance-safe), `--baseline FILE`
(no-regression diff: fails only baseline-LOAD → now-FAIL transitions;
STILL-BROKEN / IMPROVED / NEW / GONE are reported for review), and
`--corpus-rev SHA` (recorded in the header). It also scans the corpus at
full depth now: `ScanPresets` gained a `maxDepth` parameter (default 3
keeps the settings-browser contract; the stock corpus keeps presets up to
7 levels deep and the old fixed cap listed only 1,253 of 2,658). The
baseline ledger was generated locally over the full corpus at revision
`e1d7563` (SwiftShader user-space stack, validation + syncval active, zero
validation errors): 2,658 presets, 826 load+compile clean, 1,832
pre-existing broken. Failure taxonomy spot-verified as corpus-side —
Mega_Bezel references a `resource/param_values/base/auto-settings.params`
tree that does not exist in the snapshot, the smaa presets `#include
"SMAA.hlsl"` with the file absent, plus tool-generated junk presets. The
nightly workflow pins the corpus to the baseline's revision (fetch-by-SHA)
so the gate measures library regressions deterministically, runs the
`--baseline` gate (with a belt-and-braces log grep, because Actions' default
shell has no pipefail and the tool's exit code would be swallowed by
`tee`), and uploads the fresh ledger + log. Baseline refreshes are a
documented reviewed procedure (workflow header): regenerate locally,
diff-review IMPROVED/NEW/STILL-BROKEN, commit ledger + revision together,
and never baseline away a library-side regression.

### Hardware field log (DevilutionX fork adapter, branch `libslang-vk`)

Verbatim-log summaries from the fork's field loop — the hardware half of
the M4 exit above. Hosts: Windows (Radeon RX 560 Series, vsync on,
game + swapchain 1920x1080, equal-size direct present path) for sessions
1–3 and 5; Linux (llvmpipe, LLVM 21.1.8 — Mesa software rasterizer, the CI
conformance vehicle's hardware-side cousin) for session 4. The adapter
build advances between sessions (`338827ff4` Oct 17 18:21:36 →
`ff3999a6f` Oct 17 18:57:45 → `a98c6d5` Oct 17 20:16:39 →
`01ea2bf7` Oct 17 23:37:02); builds are recorded per session. "0 skipped" in every heartbeat of every session so
far; no `(Unavailable)` fallback, no validation output, clean shutdowns
throughout.

**Session 1 — preset hot-swap matrix (7 presets, build `338827ff4`).** anamorphic (1 pass),
fxaa (1), fxaa+linear (2), smaa (4 — libretro preset declaring LUT
textures, so the M3 decode/upload/bind path ran on hardware),
smaa+sharpen (5), smaa+linear (5), crt-1tap-bloom (26 passes). Steady-state
~44–49 fps on the 26-pass chain; 1,836 frames total, no skipped.

**Session 2 — startup + heavy suite (4 presets, build `338827ff4`).** Cold start with
crt-1tap-bloom: device ready 104 ms after scaler init, preset loaded 466 ms
in, first present ~1.5 s in. Hot-swap to crt-maximus-royale (25 passes):
**4.05 s synchronous load** (parse + translate + glslang x25 + LUT uploads
+ history) — the heaviest data point yet for the documented no-disk-cache
trade-off (plan §13); rendered with zero skipped frames, post-load
heartbeat window ≈19 fps (single short window — steady-state fps on the
heavy suites is an open M3 validation item, plan §11 perf budgets).
Beyond presentation, the preset's declarations exercised M2 semantics on
hardware: srgb + float framebuffers (`srgb_framebuffer1`,
`float_framebuffer2`), a pass alias (`maximus_refpass`),
`frame_count_mod`, and absolute pass scaling. Its src shaders declare no
history/feedback taps, so those were NOT covered by this run.
crt-nobody (1 pass, 141 ms) and crt-hyllian-ntsc (5 passes, 281 ms)
followed; 1,330 frames total, no skipped.

**Session 3 — edge-smoothing families + in-game level loads (12 preset
loads across two host runs, build `ff3999a6f`).** Run A: crt-1tap-bloom
(26), crt-guest-advanced-hd (12 passes, **905 ms load** — second-heaviest
after maximus-royale), crt-geom-deluxe (5), crt-CreativeForce-SharpSmooth
(2); 1,228 frames. crt-guest-advanced-hd's chain includes the guest
afterglow pass, which taps `OriginalHistory` (7 references) — **history
rings ran on hardware** in this session. Run B (cold start on SharpSmooth,
first present ~1.1 s
in): hq2x/hq3x/hq4x (5 passes each — source-multiplier scale rules x2/x3/x4
and viewport-type scaling exercised on hardware), scalefx (6),
2x/4xScaleHQ (3), fsr (2, 331 ms).
Steady state ~52–58 fps on SharpSmooth and fsr, ~35–44 fps on scalefx
(heaviest edge smoother, as expected); warm-cache loads collapse
(hq2x 196 ms first-run → hq4x 73 ms). The chain also survived two in-game
level loads (ShowProgress transitions; the frame dips there are the game's
own loading screens) with no Reinit and no fallback; 4,699 frames, clean
shutdown.

**Session 4 — first Linux host run (build `a98c6d5`, exe
1.6.0-dev-Release-a98c6d5, Oct 17 20:16:39).** Host: Linux, Vulkan device
**llvmpipe (LLVM 21.1.8, 256 bits)** — Mesa's software rasterizer
(lavapipe-class): the conformance environment's hardware-side cousin, not
a dGPU. vsync=0. `fxaa+linear` (2 passes; already covered on Windows in
session 1) cold-loaded in **812 ms** on the software device; first
present ~943 ms in with game=1366x768 vs swapchain=1299x736 —
**mismatched sizes, so the fit-and-center letterbox present path
(plan §8 output (a), M4) ran on a second OS and a second device class**
(all Windows sessions were equal-size direct presents). Clean shutdown
~6 s in; no Reinit, no fallback, no validation output. The frame counter
did not report in this short session. A real-GPU (dGPU) Linux run remains
open against the M4 exit's "hardware field sweep on Windows + Linux"
wording — see the still-open list.

**Session 5 — HDR preset family + CRT suite hot-swap matrix (16 loads,
build `01ea2bf7`, exe 1.6.0-dev-Release-01ea2bf7d, Oct 17 23:37:02).**
Host: Windows, RX 560, vsync=1, equal-size 1920x1080 direct present.
First field exposure of the **HDR preset family**: crt-adamant-default-ntsc
(13 passes — the hand-written `hdr/` variant), crt-adamant-default (3),
crt-sony-megatron-default-hdr-NTSC (9), crt-sony-megatron-default-hdr (3),
hdr-config-adamant (1), then a CRT sweep — crt-1tap-bloom (26, repeat),
crt-geom-deluxe (5), crt-guest-advanced-hd (12), crt-hyllian-fast (1),
crt-maximus-royale (25), crt-mattias (1), crt-hyllian (5), crt-super-xbr
(11), smaa / smaa+sharpen / smaa+linear (4/5/5). Fifteen hot-swap
transitions, all ok; zero skipped / dest-skipped in every heartbeat;
no fallback, no validation output; clean shutdown ~141 s in; 5,416
frames counted. Frame pacing by weight: heavy 25/26-pass chains in the
~28–40 fps heartbeat windows, light chains ~45–60. Note for the parser
cap fix (this build predates it): the 13-pass `hdr/` adamant variant does
not approach the old 32-pass cap — the fix targets the generated
mega-preset adamant variants that failed with "preset pass index 93
exceeds the limit of 32" (kMaxPresetPasses 32→128 shipped in the same
cycle; see the parser hardening note above).

**Still open in the sweep:** a real-GPU (dGPU) Linux run (session 4 was
llvmpipe, software); feedback double-buffering
readers (named `<name>Feedback` / numeric `PassFeedback#` forms),
`mipmap_input` chains, live parameter updates, resize/fullscreen-toggle
FBO + swapchain recreation, and a long preset-swap soak for
chain-teardown VRAM stability. Field-verified with no regressions:
integer-scaling on/off (explicit `outputRect` path) and vsync on/off
(Windows) / vsync off (Linux).
Corpus note: the `histories` key and `max_fbo_*` clamps have **zero**
users across all 2,658 presets — lib-only paths. Both were lib-side
implemented + tested 2026-10-18 (miss-list round): `max_fbo_*` clamps
FBO sizes in the runner (`maxfbo.slangp` runner case + parser pins);
`histories` stays informational (ring depth derives from max referenced
tap) with parse/clamp/merge pinned in `test_preset_parser`.

**Cumulative after 5 sessions (6 host runs, 2 OSes):** 40 preset loads over
29 unique presets — max 26 passes — 14,509 frames counted (session 4's
short run uncounted), zero skipped, zero fallbacks, zero validation
output, clean shutdowns. Device coverage: RX 560 (dGPU) + llvmpipe
(software rasterizer); present-path coverage: direct equal-size +
letterbox fit-and-center; vsync on/off both field-verified.

**Session 6 (2026-10-19, Windows, RX 560, fork exe 1.6.0-dev-Release-
`996e5afcc`) — the fallback drill, fired for real.** Session flow:
`blendoverlay` loaded (1 pass) → 551 healthy frames, 0 skipped → preset
change to `test/feedback-noncausal.slangp` → **load rejected in 25 ms**
with the precise diagnosis `pass 0 'feedback-noncausal.slang': slang:
PassOutput0 is not causal in pass 0` (the same error class the corpus
ledger pins as an intentional negative and the `conformance drill`
`noncausal.slangp` case exercises) → the host honored the contract:
"Shader Filter is off for this session", scaler shutdown, "GPU scaler
unavailable, using the regular renderer instead", main window recreated
for the presentation path switch, full error surfaced on the OSD
("Shader: Custom (failed: pass 0 'feedback-noncausal.slang': slang:
PassOutput0 is not …") → next Reinit (~11.5 s later) created a fresh
device, loaded `scanlines-Modern` (1 pass) and rendered 278 frames, 0
skipped, clean shutdown. This is the event CHECKLIST miss #11 asked for:
broken preset → `(Unavailable)` → stock renderer → recovery, observed —
not asserted. In-repo standing regression for the library half shipped
the same day (`conformance drill`, CHECKLIST #11).

**Session 7 (2026-10-19, Windows, RX 560, `conformance.exe` from the CI
shader-demo Windows artifact) — the golden suite, cross-backend then
on-device.** First pixel-level comparison of the committed
(llvmpipe-generated) golden set against a real GPU:
`conformance golden --manifest tests-golden/manifest.txt --golden-dir
tests-golden tests-data .` passed **19/20 within the manifest
tolerances, every passing entry at worst delta ≤ 3** — feedback
ping-pong `worst 2 / 0.000%`, mipmap refill `worst 3 / 0.104%`, history
ring, FBO formats, the LUT family, rotation, push-fragment uniforms,
parameters and size uniforms all pixel-verified across backends. One
DIFF: `presets/demo-glow.slangp` (worst 156, 8.701% of pixels beyond
delta 2, mean 2.6). Root-caused to the fixture's construction, not a
library defect: pass 0 renders at exactly 0.5× source scale, so every
pass-0 output pixel center maps to source-space `2·ox+1` — an exact
texel boundary — and the ±1-texel box taps land on exact boundaries too;
a linear tap exactly on a boundary is a coin-flip decided by fp32
rounding of the interpolated UV + the 1/size offset, and the hard-edged
testcard turns each flip into a large local delta. `--update` followed
by an immediate re-compare on the RX 560: **20/20 byte-exact (worst 0)**
— run-to-run determinism on real hardware, proving the DIFF is
per-backend deterministic rounding, not a race or an uninitialized read.
The manifest now carries this fixture as the suite's only non-default
tolerance (`200 / 12.0`, ~28% headroom over the measured envelope;
same-backend compares stay byte-exact; a real alias/scale/filter
regression still lands far above it on any backend). Committed goldens
stay llvmpipe-generated — CI keeps its byte-exact baseline; hardware
runs pass through the documented tolerance. This session closes the
hardware half of the F2 numeric (`PassFeedback0`) and F1
(`mipmap_input`) checklist rows; still open on hardware: the named
`<name>Feedback` reader form, interactive live-parameter changes,
resize/fullscreen toggle, VRAM-teardown soak, dGPU Linux run,
validation layers enabled on hardware.

**Session 8 (2026-10-19, Windows, RX 560, fork exe `996e5afcc`) — the
sweep shortlist, first batch: named feedback verified, a real
present-path format bug found.** Two host runs through the fork:

- **Named feedback form verified.** `test/pragma-name.slangp` (the
  `MainPassFeedback` reader — the corpus canary pair) loaded in 38 ms
  and rendered ~1,166 frames across four heartbeats, 0 skipped. The
  named `<Name>PassFeedback` form is now hardware-covered on both
  halves (lib goldens + this run), closing the last open feedback item
  from Session 7's still-open list.
- **Generate-and-cache feedback pattern verified visually.**
  `misc/print-resolution.slangp` (2 passes; `TextGeneratePassFeedback`
  — a feedback texture that persists across frames to cache generated
  text) loaded (2 passes, 260 frames); the maintainer confirmed the
  expected output ("showed the current resolution in big yellow
  letters"), which exercises feedback persistence + the aliased
  generate pass end to end.
- Also clean: `test/feedback.slangp` (numeric `PassFeedback0`, 280
  frames — fork-side confirmation of Session 7's golden result),
  `test/frame_count.slangp` (227), `test/history.slangp` (281),
  `misc/pixel-weave-sync.slangp` (276, clean shutdown).
- **New failure — `test/format.slangp` fails to LOAD on the fork**
  (`preset load failed (format): pass pipeline: VkResult
  VK_ERROR_UNKNOWN`, 49 ms). The fallback contract held for the second
  time and for a second failure class (Vulkan pipeline error this
  time, not slang parse): clean session disable, window recreate,
  error on the OSD, full recovery next session. **[Root-cause
  attribution CORRECTED in Session 9.]** The Session 8 analysis below
  read the failure as the present-path final-pass own-FBO format
  problem and leaned on a golden-parity claim ("the same preset
  passed the golden compare and the perf runs earlier the same day").
  That parity was an illusion: the golden entry is
  `tests/data/field-presets/test/format.slangp` — our own 3-pass
  R16F/RGBA16F/sRGB fixture — while the fork ran the **upstream corpus
  `test/format.slangp`** (2 passes: R32_UINT pack → `usampler2D`
  decode), a different file with the same name. The maintainer's
  `test/` folder is the corpus `test/` folder (every Session 8 `test/`
  preset matches a corpus `test/` ledger row), so the golden suite
  never covered the file that failed. The original analysis, kept for
  the audit trail: the fork's swapchain path was thought to build the
  final pass pipeline against its own `R8G8B8A8_SRGB` FBO (the
  create-time loop's `pass.targetFormat` when the host passes
  `outputFormat = UNDEFINED`), with Session 2 evidence
  (`crt-maximus-royale`, `srgb_framebuffer1`, build `338827ff4`)
  ruling out "sRGB exists". An isolation kit (`diag/*.slangp`, 6
  presets sharing one trivial blit) shipped against that hypothesis.
- **Library follow-up shipped the same day:** pass/present-pipeline and
  render-pass errors now carry the pass index, shader path and target
  format — e.g. `pass 2 'shaders/fmt_srgb.slang': pass pipeline (format
  R8G8B8A8_SRGB): VkResult VK_ERROR_UNKNOWN` — so the next hardware log
  names the exact failing pipeline without a kit (`FormatShortName`
  helper in `chain.cpp`; 26/26 + golden 20/20 re-verified).

**Session 9 (2026-10-19, Windows, RX 560, fork exe `6dc2a8713` — the
first build carrying the Session 8 sharper errors; root cause found
locally on lavapipe the same day) — the diag kit reads "all load", the
maintainer's challenge re-opens the attribution, and the real cause is
a library bug with a local segfault repro: integer samplers never
joined the canonical descriptor layout.**

- **Diag kit round 1 on the fork: all 6 load and render.** One
  session, six hot-swaps on a single device (device created once,
  492 ms): `diag/control`, `final-r16f`, `final-rgba16f`,
  `final-srgb-key`, `final-srgb`, `mid-srgb`, plus `misc/relief` and
  `misc/accessibility_mods` — each load answered by the OSD
  (`Shader: <name> (Vulkan)`), 1,734 frames total, 0 skipped,
  ~57–60 fps steady, clean shutdown at 33.6 s. Per the kit README's
  decision tree this is the "All load" branch, and it eliminates,
  individually and on the fork present path: shader content (the kit
  blits are layout/body-identical to the fmt shaders), the sRGB final
  own-FBO pipeline (1-pass), R16F and RGBA16F final own-FBO pipelines
  (1-pass), the `srgb_framebuffer0 = true` key form, and the sRGB
  intermediate in a 2-pass chain (no regression since Session 2).
  New hardware datapoint on top: six consecutive preset hot-swaps at
  22–44 ms per load with zero device re-creates — the swap-path
  Reinit cycle is robust on this driver.
- **Maintainer challenge, in-session: "test/format is not supposed to
  load — one of the known non-library errors in the 21 failed set?"**
  Direct answer: no — the pinned ledger marks `test/format.slangp`
  LOAD, and the only `test/` entry among the 21 pinned failures is
  `test/feedback-noncausal.slangp` (the intentional causality negative
  the fallback drill reuses). But the challenge surfaced the real
  provenance error: there are TWO `format.slangp` files. The fork's
  `test/` folder is the corpus `test/` folder (Session 8's
  `pragma-name`/`frame_count`/`history`/`feedback` presets all match
  corpus `test/` rows), so the failing preset was the **upstream**
  file — our 3-pass field fixture only shares its name. Session 8's
  golden-parity claim and the present-path framing are retracted
  (correction applied in the Session 8 block above).
- **What upstream `test/format.slangp` actually is** (fetched at the
  pinned revision `e1d75632`): pass 0 `format.slang` — `#pragma format
  R32_UINT`, fragment outputs `uint FragColor` via `packUnorm4x8`;
  pass 1 `decode-format.slang` — reads the intermediate through an
  explicit-layout `usampler2D` (`layout(set = 0, binding = 1)`) +
  `texelFetch` + `unpackUnorm4x8`. An integer-FBO pack/unpack
  round-trip feature test. Nothing in this repo had ever EXECUTED it:
  the ledger verdict is compile-only, and no golden/perf entry covers
  the upstream file — the fork was the first runner to try.
- **Local repro on lavapipe: host segfault.** `corpus_sweep --render`
  over a verbatim reconstruction of the upstream preset crashes the
  host with SIGSEGV at draw (compile-only passes; binary sanity proven
  by re-rendering the 6-preset diag kit 6/6 in the same session).
  Isolation variants render fine — the R32_UINT write path with no
  downstream read, and even a float `sampler2D` reading the R32_UINT
  view — the crash requires the `usampler2D` pass.
- **Root cause (library bug, fixed this session).**
  `RewriteSamplers` re-homes only float `sampler2D` declarations: the
  rewrite regex requires `uniform\s+…sampler2D` and cannot match
  `usampler2D`/`isampler2D`, and the scan index deliberately skipped
  integer samplers. The upstream pass's explicit `layout(set = 0,
  binding = 1)` therefore reached glslang unmodified — SPIR-V expecting
  the combined image sampler at set 0/binding 1 — while the chain's
  canonical pipeline layout provides an **empty set 0** and hosts the
  texture at set 2. llvmpipe dereferences the unmapped descriptor at
  draw (host segfault); the AMD Windows driver validates the
  shader-layout interface at pipeline creation and returns
  `VK_ERROR_UNKNOWN` — the fork's Session 8 error, matching in class
  and trigger. Neither the present path, nor sRGB, nor final-FBO
  formats were involved; offscreen would have failed identically.
- **Fix (`src/core/slang_translate.cpp`):** the rewrite regex now
  matches `(?:u|i)?sampler2D` and re-emits the matched type at the
  canonical set/binding (precision qualifiers preserved);
  `IsSamplerDeclared` and `BuildSamplerScanIndex` treat integer
  samplers as declarations (u/i-prefix accepted only with a word
  boundary before it — `sampler2DArray` still excluded); the directive
  classifier keyword lists gain `usampler2D`/`isampler2D` so a
  declaration no longer counts as a "use" of its own name;
  `stage.textures` records integer samplers exactly like float ones
  (combined-image-sampler descriptors are type-agnostic — the image
  view format carries the integer-ness).
- **Evidence chain:** the verbatim upstream preset and the pinned
  corpus tree itself both render on lavapipe post-fix (corpus `test/`
  root: 5 loaded / 5 rendered, `feedback-noncausal` rejected exactly
  as pinned); 12 new unit-test assertions in `test_slang_translate`
  (usampler round-trip re-home, isampler tap, orphan-drop parity, type
  preservation, source-level layout removal); the golden suite gains
  the round-trip as entry 21 (`field-presets/test/format-uint.slangp`
  + verbatim ported shaders, byte-exact on llvmpipe, 21/21 PASS);
  ctest 26/26; drill 7/7; diag kit 6/6 re-rendered; corpus
  no-regression gate PASSED at the pinned revision (re-fetched by SHA)
  with a decisive blast-radius argument: the entire 2,658-preset
  corpus contains exactly ONE integer-sampler file — the exposing
  preset — gated directly alongside MBZ MEGATRON (43 passes) and
  crt-royale spot checks (3 loaded, 0 regressions).
- What survives from Session 8 independent of the wrong hypothesis:
  the diag kit's six present-path format results (sRGB/R16F/RGBA16F
  final own-FBO pipelines and the sRGB intermediate are all
  hardware-proven loadable) and the hot-swap robustness datapoint.
  The `diag/` kit stays with the maintainer as a useful
  format-isolation set; the round-2 shape matrix it was building
  toward is unnecessary — the failure is fully explained and fixed
  library-side.

**Session 10 (2026-10-19, Windows, RX 560, shader_demo pre-`550dfbd`
build) — miss-list #12's interactive-params item closed by the
maintainer's field test; the opening report ("changing the values …
image did not change") is fully explained: two demo gaps, zero library
bugs.**

- **Live `set` verified on hardware:** `set BRIGHTNESS 3.0` on
  `demo-bright` changed the image on the next frame — the lib's
  runtime-override path (per-frame UBO refill from the merged
  parameter map), already pinned by `runner_m2` in CI, now confirmed
  interactively on the RX 560.
- **The confusing half was demo-side, fixed upstream the same day
  (`550dfbd`):** (a) `params` printed the preset default as the
  current value, so a `set` looked like it had not taken; it now
  reports live values with a `[runtime override]` tag; (b) the
  maintainer's original attempt edited the `#pragma parameter` default
  inside the slang file — a compile-time value a running session never
  re-reads (presets are cached per session, no hot watch by design);
  the new `r`/`reload` command re-parses the current preset from disk
  and rebuilds the chain (fresh parse staged in a side slot, cache
  swapped only after the rebuilt chain replaced the old one; a failed
  re-parse keeps the old preset + chain intact).
- **Sweep bookkeeping:** #12's interactive-params item closes on this
  evidence; remaining hardware items are resize/fullscreen (swapchain
  rebuild), the VRAM-teardown soak, a real-GPU (dGPU) Linux run, and
  validation layers enabled on hardware. The deeper live-param
  protocol from the runbook (stepped sweep over the full range,
  hot-swap reset check, glow sweep) remains available as optional
  hardening.
