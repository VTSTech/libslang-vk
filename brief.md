# Codebase Intelligence Brief: libslang-vk

> Generated: 2025-06-18 | Auditor: Pi | Commit: a1b2c3d

---

## Project Identity

| Field | Value |
|-------|-------|
| **Purpose** | Parse, translate and compile libretro shader presets (.slangp/.slang) with a Vulkan execution path |
| **Tech Stack** | C++23, CMake, Vulkan, glslang, libretro slang shaders, volk, VMA |
| **Entry Point** | C++ API: include/slangvk/runner.hpp; C API: include/slangvk/c.h |
| **Build/Run** | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release; cmake --build build; ctest` |
| **Test Command** | `ctest --test-dir build --output-on-failure` |

---

## Architecture Map

Directory → role. Only include directories that matter. One line each.

```
src/core/           → Core parsing and compilation logic (preset_parser, slang_translate, slang_compile)
src/vk/             → Vulkan runner implementation (device, images, chain, LUT support)
include/slangvk/    → Public API headers (core + runner + C API)
tests/              → Comprehensive test suite (unit tests, conformance, golden images)
examples/           → Demo programs (shader_demo, preset_info, corpus_sweep, conformance)
vendor/             → Third-party dependencies (stb image decoder)
cmake/              → Build configuration and dependency fetching
```

### Skip List

Directories and files that are safe to ignore entirely:

- `__pycache__/`, `.git/`, `build/`, `dist/`
- Generated files, compiled output
- Static assets (except test fixtures in tests/data/ and tests/golden/)

---

## Critical Files Index

The most important files in the codebase. These are the files a developer will
need to touch for almost any meaningful change. Include file path, purpose,
and why it matters.

| File | Purpose | Why It Matters |
|------|---------|----------------|
| `include/slangvk/runner.hpp` | Main Vulkan runner API | Defines Device, Preset, Chain, Image classes and the public API surface; core of the library's value proposition |
| `include/slangvk/slangvk.hpp` | Umbrella header | Single include for all public APIs; controls conditional compilation of runner vs core-only |
| `include/slangvk/core/preset_parser.hpp` | Preset file parsing | Defines data model for .slangp files and parsing logic; foundation of the entire system |
| `src/core/preset_parser.cpp` | Preset parsing implementation | Complex state machine parsing .slangp format; handles include resolution, parameter extraction, validation |
| `src/vk/chain.cpp` | Shader chain execution | Core Vulkan render loop; pipeline creation, descriptor management, FBO allocation, history/feedback handling |
| `src/vk/device.cpp` | Vulkan device management | Device creation/adopt, VMA allocator setup, command buffer management, synchronization |

**Rule of thumb**: If a file appears in 3+ dependency chains, it belongs here.

---

## Request / Execution Lifecycle

How data flows through the system. Trace the path from input to output.

```
1. .slangp file → preset_parser.cpp (parse preset structure, extract passes/textures/parameters)
2. .slang files → slang_translate.cpp (translate slang to GLSL with #pragma texture collection)
3. GLSL → slang_compile.cpp (compile via glslang to SPIR-V)
4. SPIR-V + preset → vk/chain.cpp (create Vulkan pipelines, FBOs, descriptor sets)
5. Input frame → vk/chain.cpp::render() (uniform fill, pipeline binds, pass execution)
6. Output → vk/chain.cpp (final pass render to target with history/feedback handling)
```

---

## Dependency Graph

Key relationships between modules. What depends on what? Where are the
coupling points? This helps the next agent understand blast radius of changes.

```
src/vk/ → src/core/ (uses preset data model and compilation results)
src/vk/chain.cpp → src/vk/device.cpp (device handles for Vulkan calls)
src/vk/chain.cpp → src/vk/images.cpp (image allocation and management)
src/vk/chain.cpp → src/vk/lut.cpp (LUT texture upload)
examples/ → src/vk/ + src/core/ (consumes public API)
tests/ → src/vk/ + src/core/ (tests implementation details)
```

---

## Patterns & Conventions

What patterns does this codebase use? Document the expected way of doing things
so the next agent writes code that fits in.

| Aspect | Pattern |
|--------|---------|
| Error handling | std::expected<T, Error> with detailed error messages and file locations |
| Resource management | RAII with move semantics; pimpl pattern for hiding Vulkan details |
| API design | Builder pattern for Device creation, fluent interface for Chain configuration |
| Memory management | VMA for Vulkan memory allocation; custom mapped buffers for per-frame uploads |
| File organization | Co-locate headers with implementation in src/; public headers in include/ |
| Testing | Comprehensive unit tests with fixtures; golden image comparisons for visual regression |

---

## Known Landmines

Things that will trip up an unwary developer. These are the non-obvious pitfalls
discovered during the audit.

- **`src/core/preset_parser.cpp`** — Complex state machine with many edge cases; include resolution has subtle cycle detection that's easy to break
- **`src/vk/chain.cpp`** — Heavy dependency on precise Vulkan state ordering; pipeline creation assumes specific descriptor set layouts that must match compiled SPIR-V
- **`include/slangvk/runner.hpp`** — Device must outlive all created objects; destruction order is critical (Chain → Preset → Device)
- **`src/vk/device.cpp`** — Synchronous render() calls fence internally; cannot batch frames without redesigning the API contract
- **C API vs C++ API** — Both exist but have different lifetime rules; C API is simpler but less flexible

---

## Active Decisions

Architectural or design choices that were made (and why). These are decisions a
future developer should understand before proposing alternatives.

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Vulkan sync | Synchronous per-frame rendering | Simplifies memory management and ensures deterministic behavior; batching left as future optimization |
| No caching | Compile-only, no SPIR-V caching | Reduces complexity and storage requirements; glslang compilation is fast enough for real-time use |
| Pimpl pattern | Heavy use of pimpl for Vulkan types | Hides Vulkan implementation details and enables ABI compatibility between library versions |
| Error handling | std::expected instead of exceptions | More explicit error handling in performance-critical code; no exception overhead |
| Single-threaded | No thread safety in Chain class | Simplifies implementation; Vulkan is inherently single-threaded per device queue |

---

## What's Missing / Incomplete

Unfinished work, TODO comments, known gaps. Honest assessment of what's not done.

- M5 milestone not implemented (full CMake package configuration)
- No async/pipelined rendering (always synchronous per frame)
- Limited error recovery (device lost not handled gracefully)
- No shader hot-reloading during runtime
- Performance optimizations not implemented (sync2 barriers, dynamic rendering)
- Limited documentation beyond API headers
- No benchmarking framework for performance regression testing

## Audit Findings Summary

**Recent audit (2026-10-21):** 10 open findings across 7 categories
- **0 Medium severity** — all three Medium findings are closed
- **10 Low severity:** C API validation, parser complexity, pimpl usage, performance opportunities
- **No High severity findings** - strong security and robustness practices

Key findings from the detailed audit:
- ROB-33: Synchronous render() limits performance and batching — **closed** (async `render_async()` / `wait_async()`)
- ROB-34: No async execution or pipelining support — **closed** (`ChainOptions::framesInFlight`, per-frame resource slots)
- ROB-35: Limited Vulkan device lost recovery — **closed** (`Device::recover()`, automatic rebuild)
- PERF-08: No SPIR-V caching across preset loads
- FEAT-06: No shader hot-reloading during runtime
- Complete findings documented in `audit.md`; closed findings in `deltas.md`

---

## Quick Start for Developer

The minimum a new agent/developer needs to do to be productive:

1. Read the Critical Files Index above — start with runner.hpp to understand the public API
2. Understand the Request Lifecycle — this is how the system works from .slangp to rendered frame
3. Check Known Landmines — don't step on these Vulkan/state management pitfalls
4. Follow Patterns & Conventions — use RAII, std::expected, and the pimpl pattern
5. Study the examples/ directory — shader_demo and preset_info show real usage patterns
6. Look at tests/ to understand expected behavior and edge cases

Do NOT start by reading every file. Use this brief as your map and read only
what you need for your specific task.