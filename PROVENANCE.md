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
