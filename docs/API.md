# libslang-vk API Reference (draft)

| | |
|---|---|
| **Status** | **Draft — pre-freeze.** Documents the delivered public API as of M4.06 (2026-10-21). The API freeze is the M5 gate (PLAN §11): until then, names/semantics may still change; this document is the review baseline for the freeze. |
| **Audience** | Host-application authors consuming libslang-vk; maintainers reviewing the M5 freeze. |
| **Companion docs** | [SPEC.md](SPEC.md) (preset-format contract) · [PLAN.md](PLAN.md) (design rationale, §6 sketch → this document) · [CHECKLIST.md](CHECKLIST.md) (implementation status) · [CHANGELOG.md](CHANGELOG.md) |
| **Header of record** | Everything here is generated from the headers under `include/slangvk/`; if code and this file disagree, the headers win and this file gets fixed. |

---

## 1. Overview and layering

libslang-vk parses, translates and compiles **libretro slang shader presets** (`.slangp` + `.slang`) and executes them as Vulkan render chains. The public surface is two layers in one library, layered behind one umbrella header:

```cpp
#include <slangvk/slangvk.hpp>   // umbrella: core + runner (default build)
```

| Layer | Namespace | Headers | Needs Vulkan? |
|---|---|---|---|
| **Core** — preset data model, `.slangp` parsing, `.slang` → per-stage GLSL translation, glslang → SPIR-V compilation | `slv::preset` | `slangvk/core/types.hpp`, `preset_parser.hpp`, `slang_translate.hpp`, `slang_compile.hpp` | No (`vulkan.h` never included) |
| **Runner** — Vulkan device management, chain execution, presentation, host image helpers | `slv` | `slangvk/runner.hpp` | Yes (runtime loader via volk; no link-time dependency) |

Build-time switches that affect what the umbrella header pulls in:

- `SLANGVK_ENABLE_VK` (CMake option, default `ON`) builds the runner. `OFF` gives a core-only library.
- `SLANGVK_NO_DEFAULTS` (preprocessor macro) stops the umbrella header from including `runner.hpp` even when the runner is built — for consumers that include only the core headers they need.
- The core headers are always includable directly (e.g. `#include <slangvk/core/preset_parser.hpp>`) regardless of both switches.

The load-bearing boundary: the core layer never touches Vulkan. It consumes shader source + preset files and emits ordered passes, resource bindings, parameter blocks and SPIR-V blobs. The runner consumes exactly that IR. This is what keeps the "Vulkan only" decision cheap to revisit (PLAN §5.1).

## 2. Linking, distribution and ABI

- **CMake targets:** `slangvk` (STATIC) and `slangvk-shared` (`OUTPUT_NAME slangvk`; produces `libslangvk.so` / `libslangvk.dll` + import lib). Option `SLANGVK_BUILD_SHARED` (default `ON`) builds the shared variant alongside the static lib.
- **Export macro:** every public symbol is annotated `SLANGVK_API` (`slangvk/export.h`). Static builds need no annotation; shared builds export/import annotated symbols (`SLANGVK_BUILDING_SHARED` / `SLANGVK_SHARED`).
- **ABI contract:** the API uses C++23 vocabulary types (`std::expected`, `std::span`, `std::string_view`, `std::variant`) — **ABI stability across toolchains is explicitly not promised**. Shared builds are distributed per platform/toolchain as CI-built artifacts, the way the reference host's releases are. Static linking per toolchain is the recommended consumption.
- **Vulkan at link time:** none. The runner loads the Vulkan library at runtime through volk internally; consumers need no Vulkan SDK to link — only the Vulkan headers that ship alongside `runner.hpp` for the handle types. `Device::create` fails with `vk_init_failed` when no loader/runtime is present.
- **Version macros:** `SLANGVK_VERSION_MAJOR/MINOR/PATCH` (currently 0/1/0). Pre-1.0, expect surface changes; the freeze commits to semver post-1.0.
- **C++ standard:** C++23. No C API and no non-C++ bindings in v1 (PLAN §2 non-goal).

## 3. Error handling: `std::expected` + the failure contract

Every fallible operation returns `std::expected<T, slv::Error>`. The library never throws across the API boundary, never terminates, and never writes to stdout/stderr uninvited (logging is absent by design in v1; diagnostics live in `Error::message`, translator `warnings`, and `Device::validationErrors`). Two stderr writers are documented exceptions by design: (1) a **warn-once** render-time notice when a texture tap is not causal this frame and the current source is bound instead (SPEC §4.5 runtime tolerance; one line per texture name per chain — `libslang-vk: texture '…' is not causal in pass N; binding the current source instead`), a pack-author debugging aid with no receiving API surface; (2) when the environment variable `SLANGVK_FILL_DEBUG` is set, every per-pass UBO member fill is traced to `stderr` (block, member, offset/size, resolved value, preset-override hit). Both go to `stderr` only — never stdout — and they are the only two writers the library contains.

```cpp
enum class Errc {
    preset_parse,        ///< .slangp/.slang syntax or reference problem
    include_cycle,       ///< #include / #reference cycle detected
    shader_compile,      ///< glslang front-end rejected a stage
    unsupported_feature, ///< valid slang/preset construct not yet implemented (see message)
    invalid_argument,    ///< caller passed mismatched sizes/formats/layouts
    vk_init_failed,      ///< loader/instance/device creation failed
    vk_device_lost,      ///< VK_ERROR_DEVICE_LOST during execution
    device_lost_recovery_failed, ///< Device lost recovery attempt failed
    vk_out_of_memory,    ///< host or device allocation failed
    vk_error,            ///< any other Vulkan VkResult failure
};

struct Error {
    Errc code;
    std::string message; ///< Human-readable context (paths, pass indices, glslang log).
    std::string file;    ///< Library-side source location of the report site.
    int line = -1;
};
```

Failure contract, inherited verbatim from the reference host: **any** load or run error comes back as the error channel — the host owns degradation (in DevilutionX: fall back to the stock renderer, never `app_fatal`). A failed `Preset::load` leaves no partial state; a failed `Chain::render` leaves the chain reusable for the next frame. The one exception is `vk_device_lost`: the device is gone, so the chain rebuilds the device, the preset's textures and itself (M4.06) and then reports the error — the host must recreate its own images before rendering again.

## 4. Core layer (`slv::preset`) — GPU-agnostic

The core layer is fully public so hosts can build custom load loops (custom UIs, compile-only sweeps, asset pipelines). Most hosts should just use `slv::Preset::load` (§5.3), which orchestrates all of this with the right wiring.

### 4.1 Data model — `slangvk/core/types.hpp`

| Type | Fields (defaults) | Notes |
|---|---|---|
| `WrapMode` | `ClampToEdge, Repeat, MirroredRepeat` | Spec's `clamp_to_border` maps to `ClampToEdge` (documented divergence, SPEC §7.7) |
| `FilterMode` | `Nearest, Linear` | |
| `ScaleType` | `Source, Viewport, Absolute` | |
| `ScaleRule` | `type = Source`, `scale = 1.0` | Per-axis output size rule |
| `PassDef` | `shaderPath`, `filter`, `wrap`, `scaleX/scaleY`, `srgbFramebuffer`, `floatFramebuffer`, `mipmapInput`, `alias`, `frameCountMod`, `pragmaFormat` | One chain pass; paths relative to the preset file's directory |
| `TextureDef` | `name`, `path`, `filter`, `wrap`, `mipmap` | A LUT texture; name is what shaders declare as `sampler2D` |
| `ParamOverride` | `name`, `value` | Preset-side parameter value (modern RetroArch semantics: one float) |
| `Preset` | `filePath`, `dir`, `displayName`, `relativePath`, `passes`, `textures`, `parameters`, `feedbackPass = -1`, `histories = 1`, `maxFboWidth = 0`, `maxFboHeight = 0` | Parsed preset. `maxFbo*` = 0 means no clamp; otherwise every pass FBO dimension is capped (SPEC §3.2) |
| `PresetEntry` | `displayName`, `relativePath` | One `.slangp` found by a scan |
| `PresetDirListing` | `subdirs`, `presets` | One directory level for settings-UI browsing |

Note: `feedbackPass` is parsed but unused by the runner — double-buffering is derived from actual feedback reads (SPEC §3.2/§5.3); `histories` is informational — the ring depth derives from the max referenced `OriginalHistoryN` (SPEC §3.2).

### 4.2 Parsing and scanning — `slangvk/core/preset_parser.hpp`

| Function | Signature | Contract |
|---|---|---|
| `ParsePreset` | `bool (const std::string& path, Preset& out, std::string& error)` | Parses one `.slangp` (chains, filters, wraps, scales, srgb/float FBOs, aliases, `frame_count_mod`, `mipmap_input`, LUTs + modifier keys, parameter overrides, `feedback_pass`, `histories`, `max_fbo_*`, `#reference` merge). Values may be quoted; `#` comments stripped. On failure `out` is untouched and `error` explains |
| `ScanPresets` | `std::vector<PresetEntry> (const std::string& rootDir, int maxDepth = 3)` | Recursive `.slangp` scan, sorted by relative path (deterministic menu order), not cached. Depth 3 is browser-tuned; full-corpus tooling passes a deeper bound (stock slang-shaders keeps presets up to 7 levels deep) |
| `ListPresetDir` | `PresetDirListing (const std::string& rootDir, const std::string& relativeDir)` | One directory level (`""` = root). Cache folder + hidden dirs excluded |
| `ShaderRootDir` / `SetShaderRootDir` | `std::string ()` / `void (const std::string&)` | Runtime shader root; defaults to a relative `shaders` folder. Call the setter once at startup before any parsing/scanning/translation. Not thread-safe against concurrent preset access |
| `ResolvePresetPath` | `std::string (const std::string& relativePath)` | Root-relative preset path → absolute; empty string when missing |
| `ResolvePresetFilePath` | `std::string (const std::string& presetDir, const std::string& referencedPath)` | Resolves any preset-referenced file (pass shader, LUT, `#reference`): absolute passes through; relative tries the preset's own folder, then up to 6 ancestor folders, then the shader root — first existing wins; a hit outside the shader root is **refused** (empty string returned, never read — the SEC-01 sandbox behaving deliberately: `../../`-style ancestor walks stop at the root boundary); on total miss the conventional preset-relative join is returned (for error messages). This is what makes redistributed pack layouts load (SPEC §3.7) |
| `IsPathInsideShaderRoot` | `bool (const std::filesystem::path&)` | SEC-01 sandbox predicate: references from untrusted preset files may only reach files inside the shader root (symlinks resolved) |

### 4.3 Translation — `slangvk/core/slang_translate.hpp`

```cpp
bool TranslateSlang(const std::string& shaderPath,
                    const std::unordered_set<std::string>& knownTextureNames,
                    int passIndex, int passCount,
                    SlangTranslation& out,
                    const std::vector<ParamInfo>& presetParameters = {});
```

Translates one `.slang` into per-stage GLSL with the libslang-vk descriptor conventions (§4.5) — stage split, includes (depth 24, include-once), pragma extraction, UBO/push re-homing, sampler rewrite with the preprocessed-text dead-code oracle (SPEC §4.5, M4.03), semantic validation. `knownTextureNames` is matched **case-insensitively** and names everything the shaders may reference besides the builtins: LUT names + pass aliases, and — critically — pass names from `#pragma name` (later passes may tap an earlier pass's output by its identifier, plain or with a `Feedback` suffix).

**Host contract (the seeding rule):** before calling `TranslateSlang` for each pass, seed `knownTextureNames` with that pass's reachable `#pragma name`s:

```cpp
bool CollectSlangPragmaNames(const std::string& shaderPath,
                             std::unordered_set<std::string>& out);
```

It walks the pass's top-level file + `#include` graph with the same include-once semantics as translation. Soft-fail (`false` when the file is unreadable / include policy violated): ignore the result and let the real translation surface the load error. Skipping the seeding produces spurious "unknown texture reference" failures on `#pragma name`-tapping chains (crt-yah pattern).

Results (`SlangTranslation`):

- `vertex` / `fragment` — `StageTranslation { present, glsl, uniformBlocks, textures, usesMvp, usesFrameCount, usesFrameDirection, usesFrameTime, usesPassIndex, texelFetchCount, textureGatherCount }`. `uniformBlocks` carries the std140-reflected `UniformBlock { instanceName, binding, members }` layout (`UniformMember { name, type, arraySize, offset, size }`, with `type` one of the 14 `UniformType` values `Float, Vec2, Vec3, Vec4, Int, IVec2, IVec3, IVec4, Uint, UVec2, UVec3, UVec4, Bool, Mat4`); `textures` lists `TextureRef { name, binding }` in dense fragment-set order.
- `parameters` — `#pragma parameter` metadata (`ParamInfo { name, description, defaultValue, minimum, maximum, step }`).
- `pragmaName` / `pragmaFormat` — the `#pragma name` / `#pragma format` values.
- `sourceSize/sourceHash`, `mergedSize/mergedHash` — field diagnostics (raw file vs include-resolved merge).
- `error`, `warnings` — hard failure vs tolerated deviations (dropped dead samplers, unknown UBO members, ...). `strict` mode in the runner promotes warnings to errors.
- `ok()` — `error.empty() && (vertex.present || fragment.present)`.

### 4.4 Compilation — `slangvk/core/slang_compile.hpp`

```cpp
bool CompilePass(const SlangTranslation& translation, PassSpirv& out, std::string& error);
```

In-process glslang, Vulkan 1.0 client / SPIR-V 1.0, GLSL 450. `PassSpirv { vertex, fragment }` receives the bytecode. Nothing here touches the filesystem — v1 deliberately has no sidecar/disk cache (PLAN §2 non-goal); compilation happens at every `Preset::load`. Compile errors fail the preset load with the glslang info log in `error`. glslang's frontend is not thread-safe, so every frontend entry point (this compile path and the translator's preprocessing oracle) is serialized by one process-global mutex — concurrent `Preset::load`s are safe (§6).

### 4.5 Descriptor conventions (the core ↔ runner contract)

The rewrite assigns these sets/bindings; the runner consumes exactly them — hosts building custom consumers (or reading renderlogs) need this table:

| Resource | Set | Binding |
|---|---|---|
| Vertex-stage textures | 0 | dense, declaration order |
| Vertex UBO (plain) | 1 | 0 |
| Vertex push-constant block (re-homed as std140 UBO) | 1 | 1 if a plain vertex UBO exists, else 0 |
| Fragment textures | 2 | dense, declaration order |
| Fragment UBO (plain) | 3 | 0 |
| Fragment push-constant block (re-homed) | 3 | 1 if a plain fragment UBO exists, else 0 |

Maximum one plain UBO per stage (hard error otherwise — spec-conformant, zero corpus hits). Push blocks are never real Vulkan push constants; they are per-frame-filled UBOs (pinned by `tests/test_runner_pushblock.cpp`).

## 5. Runner layer (`slv`) — Vulkan

### 5.1 `Device` — adopted or self-created context

```cpp
struct DeviceOptions {
    std::string_view appName;
    uint32_t minApiVersion = VK_API_VERSION_1_1;      // API floor the device must support
    bool validationLayers = false;                    // VK_LAYER_KHRONOS_validation when present
    std::vector<const char*> instanceExtensions;      // requested verbatim; missing name fails creation
    std::vector<const char*> deviceExtensions;        // requested verbatim (e.g. VK_KHR_swapchain)
};

class Device {
public:
    static auto adopt(VkInstance, VkPhysicalDevice, VkDevice,
                      VkQueue graphicsQueue, uint32_t graphicsFamily)
        -> std::expected<Device, Error>;              // host-owned context (DevilutionX mode)
    static auto create(const DeviceOptions&)
        -> std::expected<Device, Error>;              // self-created headless/CI mode

    auto instance() const -> VkInstance;
    auto physicalDevice() const -> VkPhysicalDevice;
    auto device() const -> VkDevice;
    auto graphicsQueue() const -> VkQueue;
    auto graphicsFamily() const -> uint32_t;
    auto properties() const -> const VkPhysicalDeviceProperties&;
    auto waitIdle() const -> std::expected<void, Error>;

    /**
     * @brief Rebuilds the Vulkan device after a device-lost event (M4.06).
     *
     * Recreates the VkDevice, its command pool and the library's memory
     * allocator from the same instance and physical device. Only devices
     * created by Device::create() can be recovered this way: a device
     * adopted from a host (Device::adopt) stays the host's to recreate, and
     * recover() reports that as unsupported_feature.
     *
     * Every object the lost device owned is gone after this call: images,
     * presets and chains created from it must be recreated (presets and
     * chains rebuild themselves on the next use) before they are used again.
     */
    auto recover() -> std::expected<void, Error>;

    /** @brief Monotonic counter bumped by every successful recover(). */
    auto generation() const -> uint64_t;

    auto readPixels(VkImage, VkFormat, uint32_t width, uint32_t height, VkImageLayout currentLayout)
        -> std::expected<std::vector<uint8_t>, Error>;
    auto validationErrors() const -> std::vector<std::string>;
};
```

- **Adopt mode:** the host keeps owning instance/device/queue; the library records the graphics family and layers its own allocator (VMA) + command machinery on top. The host must keep the instance/device alive for the lifetime of every object created through this `Device`. Surface creation stays host-side (the library is SDL-free); request the surface + swapchain extensions via `instanceExtensions` / `deviceExtensions`.
- **Create mode:** instance + device created internally (headless/CI). With `validationLayers = true` and the layer present, messenger-based ERROR collection is active **including synchronization validation** (instance-level `VkValidationFeaturesEXT`); collected errors surface via `validationErrors()` — the CI gate treats any entry as failure.
- **`readPixels`:** row-major tightly-packed readback, transition-safe (image moves to `TRANSFER_SRC` and back to `currentLayout`). The headless test/golden vehicle; hosts that composite themselves normally read their own targets.
- **`recover` / `generation` (M4.06):** the device-lost path — see §5.4. `recover()` releases the objects created on the device (chains, presets, images) *before* swapping the device, so they are destroyed while they still belong to a live device; `generation()` lets those objects notice that the device they were built on is gone.
- Move-only; owns `Impl` (pimpl — all runner types are move-only, never copyable).

### 5.2 `Image` — convenience owning image + view

```cpp
struct ImageDesc { uint32_t width, height; VkFormat format = VK_FORMAT_R8G8B8A8_UNORM; };

struct Image::CreateInfo {
    ImageDesc desc;
    VkImageUsageFlags usage = SAMPLED | COLOR_ATTACHMENT | TRANSFER_SRC | TRANSFER_DST;
    const void* pixels = nullptr;   // row-major tightly packed; may be null
    size_t      pixelBytes = 0;     // must equal width*height*bytesPerTexel when pixels != null
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

static auto Image::create(Device&, const CreateInfo&) -> std::expected<Image, Error>;
auto Image::update(const void* pixels, size_t pixelBytes) -> std::expected<void, Error>;
auto Image::image() const -> VkImage;    auto Image::view() const -> VkImageView;
auto Image::desc() const -> const ImageDesc&;  auto Image::layout() const -> VkImageLayout;
```

VMA-backed 2D image + view for hosts/tests without their own allocator: input frames, offscreen targets, LUT uploads, readback buffers. `update()` is the M4 per-frame host-upload path (synchronous staging copy; 1-mip images created by this class only; layout preserved). Hosts with their own allocator/graphics stack can skip `Image` entirely and hand raw `VkImage`/`VkImageView` to the frame types (§5.5).

### 5.3 `Preset` — immutable compiled preset

```cpp
struct PresetLoadOptions {
    bool strict = false;      ///< Promote translator warnings to load errors
    bool noLutImages = false; ///< Skip LUT upload (headless/compile-only sweeps)
};

class Preset {
public:
    static auto load(Device&, const std::filesystem::path& slangp,
                     const PresetLoadOptions& options = {})
        -> std::expected<Preset, Error>;
    auto passCount() const -> uint32_t;
    auto parameterInfo() const -> std::span<const ParameterInfo>; // pragma defaults merged with preset overrides

    /**
     * @brief Re-uploads the preset's LUT textures (M4.06).
     *
     * The textures a preset owns live on the Vulkan device. After a
     * device-lost recovery the device is a new one and the old textures are
     * gone, so they have to be uploaded again before any chain that uses
     * them can render. The preset's CPU-side data (pass definitions,
     * translations, SPIR-V) survives a device loss and is not reloaded.
     *
     * Call this after Device::recover() and before using the preset's
     * chains. A preset loaded with noLutImages has no textures to
     * re-upload and the call is a no-op.
     */
    auto rebuildLuts(Device& device) const -> std::expected<void, Error>;
};
```

One call runs parse → translate (per pass, with `#pragma name` seeding done for you) → glslang compile of every stage. **Synchronous** — the accepted trade-off of the no-disk-cache decision; glslang frontend calls are serialized by a process-global mutex, so a host may warm several `Preset` objects on worker threads (§4.4, PLAN §5.3). Immutable after load; shareable across `Chain`s on the same device. `strict = true` is the conformance/lint mode (warnings become errors); `noLutImages = true` is what the corpus sweep uses to skip decoding LUT image files it never samples. `rebuildLuts` is the one exception to "immutable": it refreshes the preset's GPU textures after a device loss without touching its pass data (M4.06).

`ParameterInfo { name, description, defaultValue, minimum, maximum, step }` is the UI-facing merged view: `#pragma parameter` metadata with preset-file overrides applied to the default.

### 5.4 `Chain` — per-device runtime for one preset

```cpp
struct ChainOptions {
    uint32_t outputWidth = 0, outputHeight = 0;      // final output target size
    VkFormat outputFormat = VK_FORMAT_R8G8B8A8_UNORM;
    /**
     * @brief How many frames the chain may have in flight at once (1..8).
     *
     * 1 (the default) is the historical behaviour: render_async() waits for the
     * previous frame before recording the next. Higher values let the host keep
     * several frames on the GPU at once, so the GPU is not idle while the host
     * prepares the next frame.
     *
     * Each additional frame costs one more set of framebuffers, uniform buffers
     * and descriptor sets per pass. Values above 8 are clamped.
     */
    uint32_t framesInFlight = 1;
};

class Chain {
public:
    static auto create(Device&, const Preset&, const ChainOptions&)
        -> std::expected<Chain, Error>;

    auto resize(uint32_t outputWidth, uint32_t outputHeight)
        -> std::expected<void, Error>;               // stores the output target; render re-derives lazily
    auto setParameter(std::string_view id, float value)
        -> std::expected<void, Error>;               // applied to the params UBO on the next render
    auto render(const InputFrame&, const OutputTarget&, const FrameInfo&)
        -> std::expected<void, Error>;
    
    /**
     * @brief Renders one frame without waiting for the GPU to finish.
     *
     * Records and submits the frame, then returns a fence that signals when the
     * frame's GPU work has completed. The host may use the time between this
     * call and wait_async() for its own CPU work, and must call wait_async()
     * before it consumes the frame's output (presenting it, reading it back, or
     * overwriting it with the next frame).
     *
     * The same input/output contract as render() applies: the input image view
     * must be in SHADER_READ_ONLY_OPTIMAL and stays untouched, and swapchain
     * frames still wait on the host's acquire semaphore at
     * COLOR_ATTACHMENT_OUTPUT.
     *
     * At most ChainOptions::framesInFlight async frames may be in flight per
     * chain. Each of them has its own uniform buffers and descriptor sets, so
     * their parameters and texture bindings never overwrite each other. A slot
     * is reused only after its previous occupant retired, so the chain waits
     * there when the host runs ahead.
     *
     * The host must give every in-flight frame its own input image and its own
     * output target: the chain borrows both for the duration of the frame, so
     * two frames sharing one image would race on its content. With
     * framesInFlight == 1 a single image and target are enough, as before.
     *
     * The frames are submitted to the device's single graphics queue; that
     * queue's submission order is what makes a frame's read of the previous
     * frame's output (the feedback reader) safe.
     */
    auto render_async(const InputFrame&, const OutputTarget&, const FrameInfo&)
        -> std::expected<VkFence, Error>;

    /**
     * @brief Waits for a frame started by render_async() to complete.
     *
     * Blocks until the fence returned by render_async() is signalled, then
     * releases the frame's fence and command buffer. Must be called before the
     * frame's output is consumed.
     */
    auto wait_async(VkFence fence) -> std::expected<void, Error>;

    /**
     * @brief Rebuilds the chain's GPU resources (ROB-35).
     *
     * Releases the chain's pipelines, render passes, framebuffers, descriptor
     * sets, UBOs and pass targets, then creates them again from the preset the
     * chain was built from. Used after a device-lost recovery: the lost device
     * took all of those objects with it.
     *
     * The preset's LUT textures must have been re-uploaded first (see
     * Preset::rebuildLuts) — the chain borrows them.
     */
    auto rebuild() -> std::expected<void, Error>;
    
    auto passOutputSize(uint32_t passIndex) const
        -> std::expected<std::pair<uint32_t, uint32_t>, Error>;
};
```

- Owns pipelines (cached per format), FBOs, sampler cache (2 filters × 3 wraps × 2 mip modes, lazily created), history rings, feedback buffers, descriptor sets and per-pass UBOs. The descriptor sets and UBOs are **per frame slot** (see `ChainOptions::framesInFlight`), so concurrent frames never overwrite each other's parameters or texture bindings.
- **`render` is synchronous:** records + submits one command buffer and waits for the frame to retire. It is implemented as `render_async()` + `wait_async()`, so both entry points share one recording implementation. Single-threaded per `Chain` — documented API behavior, not an oversight (PLAN §5.3).
- **`render`/`render_async` take the preset's chain by reference:** the chain borrows its preset (a `Preset` must outlive every `Chain` made from it, and must not be moved while the chain exists) so a device-lost recovery can rebuild the chain from the preset's pass data and re-upload the preset's textures.
- `setParameter` matches case-insensitively; an empty id is an `invalid_argument` error. A value whose id matches no `#pragma parameter` is stored but **inert** (mirrors RetroArch's tolerance of unknown overlay keys) — the runner applies only ids that exist in the preset's parameter universe; values are written as given (no clamping). There is no getter in v1 — `setParameter` is write-only by design; UI-facing hosts track the merged view themselves (start from `Preset::parameterInfo()`, then mirror their own `setParameter` calls).
- **The history ring and the feedback buffers are indexed by a logical frame index**, not by the completion order, so `OriginalHistoryN` and the feedback reader stay correct when frames overlap. The history ring holds `depth + framesInFlight` slots.
- `resize` stores a new output target size (zero width/height is an `invalid_argument`) and touches no GPU resources. Render reacts to size drift lazily: pass FBO sizes derive from the per-frame `InputFrame` size plus the stored target, and pass targets, the history ring and the mip copy are (re)created at render when the allocated sizes no longer match. A swapchain extent change must be followed by `resize()` with the new extent before the next render (§8).

### Async rendering (M4.06)

The chain offers a synchronous and an asynchronous entry point:

- **Synchronous (`render`)** — records, submits, and waits for the frame to retire.
- **Asynchronous (`render_async` + `wait_async`)** — records and submits the frame, then hands the host a fence. The host can do its own CPU work before calling `wait_async()`.

```cpp
auto fence = chain.render_async(input, target, frame);   // does not block on the GPU
// ... the host's own work ...
chain.wait_async(*fence);                                  // before the output is consumed
```

**Limits.** The number of frames in flight is `ChainOptions::framesInFlight` (1..8, default 1). With 1 the chain drains the previous frame first, so the host still gets real CPU/GPU overlap between the two calls; with more, several frames are outstanding on the GPU at once and each has its own uniform buffers and descriptor sets. Command-buffer pooling and timeline semaphores are **not** implemented.

### Device-lost recovery (M4.06)

A Vulkan device can be lost (driver crash, GPU reset, hardware failure). The library can rebuild it:

- **Detection** — a `VK_ERROR_DEVICE_LOST` result surfaces as `Errc::vk_device_lost` (mapped in `vk::VkErrorCode`), so the chain recognises it from the error code alone.
- **`Device::recover()`** — recreates the `VkDevice`, its command pool and the library's allocator from the same instance and physical device. `Device::generation()` returns a counter bumped by every successful recovery, so objects can tell that the device they were built on is gone. An adopted device stays the host's to recreate and is reported as `unsupported_feature`.
- **`Preset::rebuildLuts(Device&)`** — re-uploads the preset's LUT textures on the new device (a const operation: the preset's pass data survives a device loss).
- **`Chain::rebuild()`** — recreates the chain's GPU resources from its preset.
- **Automatic path** — `Chain::render()` detects the loss, rebuilds the device, the preset's textures and the chain, and then reports `vk_device_lost` to the host **without retrying the frame**: the host's own images died with the old device and the library cannot recreate them. The chain is left rebuilt and ready; the host recreates its images and renders again.
- **Release before replacement** — the device keeps a registry of the objects created on it and releases them before swapping the device, so they are destroyed while they still belong to a live device. Objects remember the generation they were created on and forget their handles rather than destroying them through the new device; `Image::update()` reports `vk_device_lost` instead of using stale handles.

Recovery is best-effort: a driver that lost the device may also refuse to hand out a new one, in which case the host is expected to re-create the whole `Device`.
- `passOutputSize` exposes the currently-allocated FBO geometry of a pass (host OSD/debugging; also what `Offscreen` target sizing should match for a byte-exact final blit).

### 5.5 Frame types

```cpp
struct FrameInfo {
    uint64_t frameCount = 0;       // frames since preset load (runner pre-wraps by frame_count_mod)
    float    frameDeltaUs = 0.0F;  // microseconds since the previous frame (FrameTime uniform)
    int32_t  frameDirection = 1;   // spec: -1 while rewinding; no rewind in v1 hosts
};

struct InputFrame {                // the chain's input texture (host-owned)
    VkImageView view;              // must be SHADER_READ_ONLY_OPTIMAL
    VkImage     image;             // required for history / mipmap_input presets (runner blits it
                                   //   into the ring / mip copy; content untouched). Null otherwise;
                                   //   null when needed = invalid_argument render error (message names
                                   //   the requirement)
    VkFormat    format = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t    width, height;
    Rotation    rotation = Rotation::None;  // fills the Rotation uniform (0..3 = 0/90/180/270°)
};

struct Offscreen {                 // output variant: host composites (headless/CI)
    VkImageView view;
    VkImage     image;             // required only when the final pass is feedback-read
                                   //   (runner blits its double-buffered output into it)
    VkFormat    format = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t    width, height;     // mismatched params = invalid_argument error; an Offscreen target
                                   //   also requires ChainOptions::outputFormat to be a concrete
                                   //   format (not VK_FORMAT_UNDEFINED) and to match this format
};

struct SwapchainFrame {            // output variant: render into an acquired swapchain image
    VkImageView swapchainImageView;
    VkFormat    format = VK_FORMAT_UNDEFINED;  // the swapchain's current format;
                                               //   UNDEFINED or zero-texel format = unsupported_feature
    uint32_t    width, height;
    Rect        outputRect{};      // content rect; zero rect = fit + center (letterbox)
    VkSemaphore acquireSemaphore = VK_NULL_HANDLE;  // host acquire signal, waited before drawing
};

struct Rect { int32_t x, y; uint32_t width, height; };
enum class Rotation : uint32_t { None = 0, Cw90 = 1, Cw180 = 2, Cw270 = 3 };

using OutputTarget = std::variant<SwapchainFrame, Offscreen>;
```

Layout rules after `render` returns: the input image is untouched (back in `SHADER_READ_ONLY_OPTIMAL`); an `Offscreen` target image is in `SHADER_READ_ONLY_OPTIMAL` (ready to sample/composite); a `SwapchainFrame` image is in `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` (ready for `vkQueuePresentKHR`).

## 6. Object lifetimes and threading

- **Creation order:** `Device` → `Preset` (needs `Device&`) → `Chain` (needs `Device&` + `Preset&`). `Image` needs `Device&`.
- **Destruction order** is the reverse: `Chain`/`Image` before `Preset` before `Device`. The **`Preset` must outlive every `Chain` created from it** — LUT texture views are borrowed from the preset's uploads (M3). In adopt mode the host's `VkInstance`/`VkDevice` must outlive everything created through the `Device`.
- **Threading:** one thread per `Chain`; `Preset::load` may run concurrently with other loads on other threads (glslang is process-globally guarded); `SetShaderRootDir` is startup-only; sampler/pipeline caches are per-`Chain` and not shared.
- All public types are move-only (`Device`, `Preset`, `Chain`, `Image`); no copies, no `clone()`.

## 7. Minimal headless walkthrough

The smallest complete consumer — render a preset offscreen for a few frames and read the final one back. This is exactly the shape the golden-image conformance tool uses:

```cpp
#include <slangvk/slangvk.hpp>

using namespace slv;

int main() {
    // 1. Device (headless; no window system needed)
    auto device = Device::create({ .appName = "minimal" });
    if (!device) return 1;

    // 2. Preset: parse + translate + compile (synchronous)
    auto preset = Preset::load(*device, "shaders/crt/royale.slangp");
    if (!preset) return 2;

    // 3. Input frame + output target (convenience Image; or bring your own VkImage/View)
    auto input  = Image::create(*device, { .desc = { 320, 240 }, .pixels = myFrame, .pixelBytes = 320*240*4 });
    auto output = Image::create(*device, { .desc = { 640, 480 } });
    if (!input || !output) return 3;

    // 4. Chain + one frame
    auto chain = Chain::create(*device, *preset, { .outputWidth = 640, .outputHeight = 480 });
    if (!chain) return 4;

    InputFrame in { .view = input->view(), .image = input->image(),
                    .width = 320, .height = 240 };
    Offscreen  out { .view = output->view(), .image = output->image(),
                     .width = 640, .height = 480 };
    for (uint64_t f = 1; f <= 60; ++f) {
        input->update(myNextFrame, 320*240*4);              // M4 per-frame upload path
        auto r = chain->render(in, out, { .frameCount = f, .frameDeltaUs = 16666.0F });
        if (!r) return 5;
    }

    // 5. Readback (row-major RGBA8, tightly packed)
    auto pixels = device->readPixels(output->image(), VK_FORMAT_R8G8B8A8_UNORM, 640, 480,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!pixels) return 6;
    // ...consume pixels->data() (e.g. write a PNG)...
    return 0;  // RAII: output/input/preset/device tear down in reverse order
}
```

Notes on the walk-through's implicit contracts: `Image::create` with `pixels` uploads at creation; the input image stays in `SHADER_READ_ONLY_OPTIMAL` across `render`; `input->image()` is passed only because a crt-royale-class preset reads history/mips — single-pass blits can leave it null.

## 8. Swapchain-mode contract (DevilutionX presentation)

Per frame, with the host owning acquire/present:

1. Host creates window + surface, enables the surface + `VK_KHR_swapchain` extensions (via `DeviceOptions` in create mode, or by passing an instance that already has them to `adopt`).
2. Host acquires the next image (`vkAcquireNextImageKHR`), optionally signaling a semaphore it hands to `SwapchainFrame::acquireSemaphore`.
3. Host calls `chain->render(in, SwapchainFrame{...}, frameInfo)` — the final pass draws into the acquired image's view (letterbox `outputRect`; zero rect = fit and center), leaving it in `PRESENT_SRC_KHR`.
4. Host presents — no wait semaphore needed (`render` is synchronous/fenced).

**Extent changes (window resize / `VK_SUBOPTIMAL_KHR`):** the host must call `chain->resize(newWidth, newHeight)` with the new extent before the next render — rendering with a `SwapchainFrame` whose size differs from the stored target is a hard `invalid_argument` (`render: swapchain size changed; call resize() with the new extent`). `resize()` stores the target only; FBO re-derivation happens lazily at the next render (§5.4).

The chain's internal FBOs keep rendering at their preset-derived sizes; only the presentation draw touches the swapchain image. Mismatched game/swapchain sizes are the tested letterbox path (fit-and-center or explicit `outputRect`; integer scaling stays host-side via an explicit rect — documented divergence). `Rotation` rotates the final presentation draw clockwise and sizes the fit rect from the rotated aspect.

## 9. v1 limitations and API-relevant divergences

The full format-level list is SPEC §6/§7; these affect API consumers directly:

- **Synchronous everything.** `Preset::load` compiles with glslang inline (no disk cache); `Chain::render` fences. Cold-load cost on huge presets is real (hardware worst measured ~4 s) — warm `Preset` objects ahead of need if load-time UX matters.
- **No reload.** Preset switching is replace-and-recreate; no `Preset::reload()` (PLAN §13.2 open question).
- **Parameters are not preprocessor macros.** `#if`/`#ifdef` on a parameter name is always dead code (SPEC §4.2, M4.03 note) — pack authors porting RetroArch-conditional shaders must define the gate themselves.
- **Vertex-stage textures, sampler arrays, > 1 UBO per stage, unsized arrays** — hard translate errors; zero corpus exposure (SPEC §7.9).
- **`OriginalHistory` depth cap 8** (spec unbounded; corpus max 7). `FrameDirection` is constant `+1` (no rewind state).
- **`clamp_to_border` wrap maps to clamp-to-edge** (SPEC §7.7); `A2B10G10R10_UINT_PACK32` format token falls back to RGBA8 with a warning.
- **Same-toolchain ABI only** for shared builds (§2); the static lib is the portable artifact.
- **No logging surface.** Load warnings are visible via `Preset::load` under `strict` (as errors) or through the core-layer `SlangTranslation::warnings` for custom loops; validation errors via `Device::validationErrors()`. The library's only two stderr writers (the warn-once non-causal-tap notice and the `SLANGVK_FILL_DEBUG` trace) are named in §3.

## 10. Versioning and the M5 freeze

`SLANGVK_VERSION_MAJOR/MINOR/PATCH` (0.4.06 pre-freeze). The freeze is not imminent: M4.xx keeps iterating until the surface is ready, so names and signatures may still change between minor versions. When a freeze is in scope, the M5 gate (PLAN §11) commits to: API freeze against this document, CMake package config + pkg-config, a tagged release and the example app — and from that tag, semver. Freeze-review checklist for maintainers:

1. Every public name in `include/slangvk/` is documented here (audit the headers against §4/§5).
2. Every documented lifetime/threading rule (§6) has a test or PROVENANCE note.
3. `Errc` coverage: every error path maps to the right category (grep `Error{` in `src/`).
4. The §4.5 descriptor table matches `slang_translate.cpp` output (pinned by translator tests + runner canaries).
5. The §7 walkthrough compiles and runs against the installed headers (example-app seed).

