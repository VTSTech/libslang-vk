# EXAMPLES — the libslang-vk API by example

A worked example for every part of the public surface. The reference is
[docs/API.md](API.md) (the contract); [USAGE.md](../USAGE.md) covers
building, consuming and the tools.

- [The four-call embed](#the-four-call-embed)
- [The Vulkan runner](#the-vulkan-runner)
- [Async frames](#async-frames)
- [Frame pipelining](#frame-pipelining)
- [The SPIR-V cache enabler](#the-spirv-v-cache-enabler)
- [Device-lost recovery](#device-lost-recovery)
- [The per-frame upload path](#the-per-frame-upload-path)
- [The C API](#the-c-api)
- [The core layer on its own](#the-core-layer-on-its-own)

---

## The four-call embed

```cpp
#include <slangvk/slangvk.hpp>

auto device = slv::Device::create({.appName = "myhost"});
auto preset = slv::Preset::load(*device, "shaders/crt-royale.slangp");
auto chain  = slv::Chain::create(*device, *preset, {.outputWidth = 1280, .outputHeight = 720});
chain->render(inputFrame, outputTarget, {});   // once per frame
```

`Device::create` makes a headless device (CI, tools); a host with its own
Vulkan context uses `Device::adopt(instance, physicalDevice, device, queue,
graphicsFamily)` instead and keeps owning the context.

## The Vulkan runner

```cpp
// Headless device (lavapipe/CI) or slv::Device::adopt(instance, pd, dev, queue, family)
auto device = slv::Device::create({.appName = "myhost"});

auto preset = slv::Preset::load(*device, "shaders/crt-royale.slangp");

slv::ChainOptions opts{.outputWidth = 1280, .outputHeight = 720};
auto chain = slv::Chain::create(*device, *preset, opts);

// One frame: input texture in SHADER_READ_ONLY_OPTIMAL, output into your view.
// .image fields are needed for history/mipmap presets and feedback-read
// final passes (the runner blits from/into them; content is never modified).
chain->render(
    {/* .view = */ inputView, /* .image = */ inputImage, VK_FORMAT_R8G8B8A8_UNORM, 640, 480,
     slv::Rotation::None},
    slv::Offscreen{outputView, outputImage, VK_FORMAT_R8G8B8A8_UNORM, 1280, 720},
    {/* .frameCount = */ 0, 16666.7F, 1});
```

Presenting into a swapchain image the host acquired:

```cpp
chain->render(
    {/* .view = */ inputView, /* .image = */ inputImage, VK_FORMAT_R8G8B8A8_UNORM, 640, 480,
     slv::Rotation::None},
    slv::SwapchainFrame{/* .swapchainImageView = */ acquiredView,
                        /* .format = */ swapchainFormat,
                        /* .width = */ 1280, /* .height = */ 720,
                        /* .outputRect = */ {},            // all-zero = fit and center
                        /* .acquireSemaphore = */ acquireSemaphore},
    {/* .frameCount = */ 0, 16666.7F, 1});
// The image rests in PRESENT_SRC_KHR; the host presents it.
```

Live parameters (case-insensitive; a value whose id matches no
`#pragma parameter` is stored but inert):

```cpp
chain->setParameter("GAIN", 1.5F);   // applied on the next frame
```

## Async frames

`render()` waits for the frame to retire. A host that would rather keep a
frame in flight can split it:

```cpp
auto fence = chain->render_async(inputFrame, outputTarget, {});  // returns immediately
// ... the host's own work for this frame ...
chain->wait_async(*fence);                                       // before the output is used
```

## Frame pipelining

`ChainOptions::framesInFlight` (1..8) says how many frames the chain may
have on the GPU at once. With more than one, each frame gets its own
uniform buffers and descriptor sets, so the frames never overwrite each
other's parameters or texture bindings, and the host's CPU work overlaps
the GPU's:

```cpp
slv::ChainOptions options{.outputWidth = 1280, .outputHeight = 720, .framesInFlight = 3};
auto chain = slv::Chain::create(*device, *preset, options);
// ... record frame N+1 while frame N is still on the GPU ...
```

**One host-side rule:** a frame borrows its input image and its output
target for the frame's duration, so frames that overlap in time need one
input image and one output target each. With `framesInFlight == 1` one of
each is enough, as before.

## The SPIR-V cache enabler

The library keeps **no disk cache** — that is the host's business, and the
reference host already keeps one (`shaders/cache/`); two caches would
disagree. What only the library can do is hand the host a **correct key**, so
the host's own cache cannot serve a stale shader:

```cpp
slv::PresetLoadOptions options;
options.spirvCache.load = [](std::string_view key) -> std::optional<slv::PassSpirv> {
        // Your cache lookup, by the library's content key.
        return myCache.get(std::string(key));
};
options.spirvCache.store = [](std::string_view key, const slv::PassSpirv &spirv) {
        myCache.put(std::string(key), spirv);
};
auto preset = slv::Preset::load(*device, "shaders/crt-royale.slangp", options);
```

The key is a SHA-256 over the exact GLSL handed to glslang plus the
compiler's version and target — so it changes **exactly** when the compiled
output would. A host caching by it can never serve a stale shader, not even
when the preset `#include`s other files (which a host-side hash of the
`.slangp` alone would miss). The host needs no knowledge of glslang, the
translation or the include graph — only the key and the bytes.

The C surface mirrors this (`slangvk_spirv_cache`, load/store function
pointers).

## Device-lost recovery

If the Vulkan device is lost (driver crash, GPU reset), the chain rebuilds
it, the preset's textures and itself, and reports
`slv::Errc::vk_device_lost`:

```cpp
if (!chain->render(input, target, {})) {
        if (chain->impl().lastErrorCode() == slv::Errc::vk_device_lost) {
                // The device was rebuilt; the host's own images died with
                // the old device, so recreate them and render again.
                recreateHostImages();
                chain->render(input, target, {});
        }
}
```

Hosts can also drive the recovery themselves:

```cpp
auto recovered = device->recover();   // rebuilds the VkDevice, pool and allocator
if (recovered) {
        preset->rebuildLuts(*device);  // the preset's textures went with the device
        chain->rebuild();              // and so did the chain's resources
}
```

`Device::generation()` is a counter bumped by every successful recovery, so
an object can tell that the device it was built on is gone.

## The per-frame upload path

A host that uploads every frame allocates a persistently mapped staging
buffer once and memcpys into it each frame; the chain records the copy at
the top of its own frame submit (shader-read → transfer-dst → copy →
shader-read barriers, before history blits and passes run). One command
buffer, one submit, one fence wait per frame — the standalone
`Image::update` round-trip's second submit + fence drain disappears, along
with the per-frame staging alloc/free:

```cpp
auto buffer = device->createMappedBuffer(frameBytes);
std::memcpy(buffer.mapped, myCpuFrame, frameBytes);

slv::InputFrame input;
input.view = inputView;
input.image = inputImage;
input.pixelBuffer = buffer.buffer;      // the chain copies it as part of the frame
input.format = VK_FORMAT_R8G8B8A8_UNORM;
input.width = 640;
input.height = 480;

chain->render(input, target, {});
```

`Image::update` stays for headless and test use.

## The C API

```c
/* abridged — the header's opening comment is the full walkthrough */
slangvk_device dev;  slangvk_image img;
slangvk_preset preset;  slangvk_chain chain;

slangvk_device_create(&dev_opt, &dev);            /* or slangvk_device_adopt(...) */
slangvk_image_create(dev, &img_opt, &img);        /* input texture */
slangvk_preset_load(dev, "shaders/crt/royale.slangp", NULL, &preset);
slangvk_chain_create(dev, preset, SWAP_W, SWAP_H, swap_format, &chain);

/* per frame: */
slangvk_image_update(img, my_cpu_framebuffer, GAME_W * GAME_H * 4);
slangvk_image_get_handles(img, &in.view, &in.image, &in.format, &in.width, &in.height);
if (slangvk_chain_render_swapchain(chain, &in, &out, &fi) != SLANGVK_OK)
        fprintf(stderr, "render failed: %s\n", slangvk_last_error());
```

The C surface also carries the M4.06/M4.07 additions:
`slangvk_device_recover()` (rebuild a lost device), `slangvk_chain_create_ex()`
with `slangvk_chain_options` (including `frames_in_flight`), and
`slangvk_spirv_cache` (the SPIR-V cache enabler, with load/store function
pointers).

```c
/* the SPIR-V cache enabler */
static int cache_load(const char *key, slangvk_spirv *out, void *user)
{
        struct my_cache *c = user;
        return my_cache_get(c, key, out);   /* 0 = miss */
}

static void cache_store(const char *key, const slangvk_spirv *spirv, void *user)
{
        my_cache_put((struct my_cache *)user, key, spirv);
}

slangvk_preset_options options = {0};
options.spirv_cache.load = cache_load;
options.spirv_cache.store = cache_store;
options.spirv_cache.user_data = &myCache;

slangvk_preset_load(dev, "shaders/crt-royale.slangp", &options, &preset);
```

## The core layer on its own

The core layer (parse → translate → compile) is GPU-agnostic and can be
used without the Vulkan runner — for a host that rolls its own pipelines:

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
fragment textures set 2, fragment UBOs set 3) are exactly what the Vulkan
runner consumes; a host rolling its own pipeline can use them too.

Hosts rolling their own translate loop should also seed the translation's
texture universe with `#pragma name` pass names before translating: presets
that tap a pass's output as `<name>` (this frame) or `<name>Feedback` (the
previous frame) fail with `unknown texture reference` otherwise.
`slv::preset::CollectSlangPragmaNames(shaderPath, names)` collects them with
the same include semantics as `TranslateSlang`; `Preset::load` does this
automatically.
