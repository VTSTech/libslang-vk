# libslang-vk

[![ci](https://github.com/VTSTech/libslang-vk/actions/workflows/ci.yml/badge.svg)](https://github.com/VTSTech/libslang-vk/actions/workflows/ci.yml)

Parse, translate and compile **libretro slang shader presets** (`.slangp` /
`.slang`) as an embeddable C++23 library — pure-C binding included — with a
**Vulkan** execution path.

```
.slangp ──parser──▶ pass chain ──translator──▶ per-stage GLSL ──glslang──▶ SPIR-V ──runner──▶ Vulkan
```

The core layer is extracted from the proven preset stack of
[VTSTech/DevilutionX](https://github.com/VTSTech/DevilutionX)'s GPU shader mod
(corpus-validated against all 2,658 libretro presets; provenance and
divergences documented in [PROVENANCE.md](PROVENANCE.md)). The format contract
is [docs/SPEC.md](docs/SPEC.md); the project plan with milestones, API sketch
and risk register is [docs/PLAN.md](docs/PLAN.md).

```cpp
auto device = slv::Device::create({.appName = "myhost"});
auto preset = slv::Preset::load(*device, "shaders/crt-royale.slangp");
auto chain  = slv::Chain::create(*device, *preset, {.outputWidth = 1280, .outputHeight = 720});
chain->render(inputFrame, outputTarget, {});   // once per frame
```

## Reference implementations

Two hosts consume the library; both were exercised against the M4.07 build.

| Host | What it is |
|---|---|
| **[DevilutionX-libslang-vk](https://github.com/VTSTech/DevilutionX/tree/libslang-vk)** | The DevilutionX fork branch (plan §11 M4): `gpu_scaler` keeps its public interface and runs the library's Vulkan runner dual-path with the SDL_GPU runner — preset browser, ini persistence, OSD overlay and the `(Unavailable)` fallback unchanged. Smoke-tested at **140+ FPS with FXAA.slangp**. |
| **[Chocolate-Doom-VK](https://github.com/VTSTech/chocolate-doom-vk)** | A Chocolate Doom port with a Vulkan path. Presets load and render with the shaders working; its frame rate is **locked to 35 FPS by the engine**, so it exercises the shader path without measuring the shader cost. |

## Status

**M4.07** — async rendering, frame pipelining, device-lost recovery, an
allocation-free render path and a SPIR-V cache enabler. All library-side
M0–M4 gates are green: **31/31 ctest**, byte-exact goldens, zero
validation-layer errors on lavapipe. The corpus sweep loads 2,637 of the
2,658 libretro presets (21 upstream-broken, zero regressions).

The freeze is **not** imminent — M4.xx keeps iterating until the surface is
ready. The full status, the milestone history, the design trade-offs and the
known limitations are in **[docs/STATUS.md](docs/STATUS.md)**.

## Documentation

| Document | What it covers |
|---|---|
| **[USAGE.md](USAGE.md)** | Building, consuming, the tools, the CI artifacts, troubleshooting. |
| **[docs/API.md](docs/API.md)** | The public API reference — the contract. |
| **[docs/EXAMPLES.md](docs/EXAMPLES.md)** | The API by example: every surface, worked. |
| **[docs/STATUS.md](docs/STATUS.md)** | Where the project is, the reference hosts, the trade-offs, the limits. |
| [docs/SPEC.md](docs/SPEC.md) | The preset-format contract (parsing, translation and execution semantics). |
| [docs/PLAN.md](docs/PLAN.md) | Milestones, the API sketch, the risk register. |
| [docs/CHECKLIST.md](docs/CHECKLIST.md) | Per-item M0–M4 implementation status, audited against the plan. |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | Notable changes per milestone, plus the unreleased roadmap. |
| [audit.md](audit.md) · [deltas.md](deltas.md) | The codebase audit: the open findings, and the closed ones with what shipped. |
| [PROVENANCE.md](PROVENANCE.md) | Upstream provenance, documented divergences, pinned dependency hashes, the corpus ledger, the hardware field log. |

## Building

glslang, Vulkan-Headers, volk and VMA are fetched and SHA-256-pinned
automatically — no Vulkan SDK needed at link time:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Then try the bundled demo — it discovers presets, renders chains and
switches presets, input sources and shader parameters live:

```sh
./build/examples/shader_demo                                 # menu over the bundled presets
./build/examples/shader_demo path/to/slang-shaders           # browse any preset pack
./build/examples/shader_demo --no-menu --out shots           # headless: PNG per preset, exit
```

## License

MIT — see [LICENSE](LICENSE). The plan (§4) defines the provenance policy:
clean-room against RetroArch's GPL slang implementation; no upstream
DevilutionX code; vendored dependencies limited to permissively licensed
ones. The core layer is a black-box semantic port of the reference host's
behaviour — see [PROVENANCE.md](PROVENANCE.md).
