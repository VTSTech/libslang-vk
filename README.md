# libslang-vk

Parse, translate and compile **libretro slang shader presets** (`.slangp` /
`.slang`) as an embeddable C++23 library, with a **Vulkan** execution path.

```
.slangp ──parser──▶ pass chain ──translator──▶ per-stage GLSL ──glslang──▶ SPIR-V ──runner──▶ Vulkan
```

The core layer is extracted from the proven preset stack of
[VTSTech/DevilutionX](https://github.com/VTSTech/DevilutionX)'s GPU shader mod
(corpus-validated against all 2,658 libretro presets; provenance and
divergences documented in [PROVENANCE.md](PROVENANCE.md)). The format contract
is [docs/SPEC.md](docs/SPEC.md); the project plan with milestones, API sketch
and risk register is [docs/PLAN.md](docs/PLAN.md).

**Status: M1 (Vulkan runner skeleton).** The full M0 core suite passes, and
the runner now renders presets headlessly: `Device` adopt/create, offscreen
`Chain::render`, golden blit round-trips green on lavapipe with validation
layers clean. M2 adds history/feedback rings, LUT uploads, rotation and the
dynamic-rendering fast path per the plan.

## Consuming

```cmake
include(FetchContent)
FetchContent_Declare(libslang-vk
    GIT_REPOSITORY https://github.com/VTSTech/libslang-vk.git
    GIT_TAG        main)
FetchContent_MakeAvailable(libslang-vk)

target_link_libraries(myapp PRIVATE slangvk::slangvk)        # static
# or slangvk::shared for the shared build
```

```cpp
#include <slangvk/slangvk.hpp>

slv::preset::SetShaderRootDir("shaders");       // once, at startup
slv::preset::Preset preset;
std::string error;
if (slv::preset::ParsePreset(slv::preset::ResolvePresetPath("crt-royale.slangp"),
                             preset, error)) {
    for (const auto &pass : preset.passes) {
        slv::preset::SlangTranslation t;
        slv::preset::TranslateSlang(slv::preset::ResolvePresetFilePath(preset.dir, pass.shaderPath),
                                    {}, /*passIndex*/ 0, /*passCount*/ 1, t);
        slv::preset::PassSpirv spirv;
        slv::preset::CompilePass(t, spirv, error);
        // spirv.vertex / spirv.fragment: SPIR-V for the Vulkan runner
    }
}
```

The descriptor conventions the translated GLSL targets (vertex UBOs set 1,
fragment textures set 2, fragment UBOs set 3) are exactly what the upcoming
Vulkan runner consumes; a host rolling its own pipeline can use them too.

## Vulkan runner (M1)

The runner executes compiled presets on a Vulkan device. The host either
adopts its own context or goes headless; errors come back as
`std::expected<T, slv::Error>`:

```cpp
#include <slangvk/slangvk.hpp>

// Headless device (lavapipe/CI) or slv::Device::adopt(instance, pd, dev, queue, family)
auto device = slv::Device::create({.appName = "myhost"});

auto preset = slv::Preset::load(*device, "shaders/crt-royale.slangp");

slv::ChainOptions opts{.outputWidth = 1280, .outputHeight = 720};
auto chain = slv::Chain::create(*device, *preset, opts);

// One frame: input texture in SHADER_READ_ONLY_OPTIMAL, output into your view.
chain->render(
    {/* .view = */ inputView, VK_FORMAT_R8G8B8A8_UNORM, 640, 480, slv::Rotation::None},
    slv::Offscreen{outputView, VK_FORMAT_R8G8B8A8_UNORM, 1280, 720},
    {/* .frameCount = */ 0, 16666.7F, 1});
```

M1 semantics already live: multi-pass chains with per-pass scale rules and
aliases, the pinned UBO fill (sizes, FrameCount mod, parameters), and the
fork's viewport-derivation math. History/feedback rings, LUT image uploads,
rotation and swapchain targets return `slv::Errc::unsupported_feature` until
M2–M4. The M1 gate — golden blit round-trip headless on lavapipe with
validation layers clean — is `tests/test_runner_headless.cpp`.

Vulkan is loaded at runtime through vendored [volk](https://github.com/zeux/volk)
(no link-time libvulkan); memory comes from vendored
[VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).
Hosts adopting a context keep owning their instance/device/queue.

## Example

[`examples/preset_info.cpp`](examples/preset_info.cpp) is a complete
end-to-end demo in ~180 lines: it loads a `.slangp`, prints the pass chain,
scale rules, aliases and parameters, then translates and compiles every pass
to SPIR-V with glslang — printing the per-stage bytecode sizes and the dense
texture bindings the M1 Vulkan runner will create. Two demo presets are
bundled under [`examples/presets/`](examples/presets): a single-pass
brightness shader (`demo-bright`) and a two-pass glow chain exercising
per-pass scale rules, linear filtering and pass aliases (`demo-glow`).

```sh
cmake --build build --target preset_info
./build/examples/preset_info                       # bundled demo-bright
./build/examples/preset_info examples/presets/demo-glow.slangp
./build/examples/preset_info path/to/any.slangp    # any preset on disk
```

Each demo preset also runs as a ctest smoke test (`example_preset_info_*`),
so the example path is covered by CI. Exit code is 0 only when every pass
parses, translates and compiles — convenient for checking foreign presets.

Sample output (`demo-glow`, trimmed):

```
pass 0: shaders/demo_downsample.slang
  filter linear, wrap clamp-to-edge, mipmap off
  scale x: x0.5 source, y: x0.5 source
  alias 'blur', frame_count_mod 0, format rgba8
...
pass 1 shaders/demo_glow_combine.slang: OK
  vertex SPIR-V 1172 bytes (293 words), fragment SPIR-V 1428 bytes (357 words)
  set 2 textures: Original=0 blur=1
  parameters:
    GLOW = 0.600 (default 0.500, range 0.000..2.000 step 0.050) [preset]

result: 2/2 pass(es) compiled to SPIR-V — ready for the M1 Vulkan runner.
```

## Building and testing

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `SLANGVK_BUILD_SHARED` (default `ON`, builds `libslangvk.so/dll`
alongside the static lib), `SLANGVK_BUILD_TESTS` (default `ON`),
`SLANGVK_BUILD_EXAMPLES` (default `ON`, builds the `preset_info` demo),
`SLANGVK_ENABLE_VK` (default `ON`, builds the Vulkan runner; `OFF` gives a
core-only build), `SLANGVK_SANITIZE=address,undefined` (test builds).
glslang, Vulkan-Headers, volk and VMA are fetched and built automatically
(SHA-256 pinned; see [PROVENANCE.md](PROVENANCE.md)).

## License

MIT — see [LICENSE](LICENSE). The plan (§4) defines the provenance policy:
clean-room against RetroArch's GPL slang implementation; no upstream
DevilutionX code; vendored dependencies limited to permissively licensed ones.
