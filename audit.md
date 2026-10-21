# Improvement & Enhancement Audit

**libslang-vk v0.4.06**

**Repository:** https://github.com/VTSTech/libslang-vk  
**Author:** VTSTech | **License:** MIT | **Date:** 2026-10-21  
**Audit commit:** 5fd2e72 (HEAD at refresh) | **Auditor:** Pi — codebase-audit skill v0.2.0  
**Files reviewed:** 47 source files, 15,000+ lines of C++23 code | **Scope:** Core parsing, compilation, Vulkan runner, C API, examples, tests

---

## Table of Contents

- [Executive Summary](#executive-summary)
- [Findings Summary](#findings-summary)
- [Detailed Findings](#detailed-findings)
  - [Security](#security)
  - [Robustness](#robustness)
  - [Maintainability](#maintainability)
  - [Performance](#performance)
  - [New Features](#new-features)
  - [Architecture](#architecture)
  - [Testing](#testing)
- [Priority Matrix](#priority-matrix)
- [Architecture Strengths](#architecture-strengths)

---

## Executive Summary

This audit covers libslang-vk, a C++23 library (~15,000 lines) that parses, translates, and compiles libretro shader presets (.slangp/.slang) with a full Vulkan execution path. The audit examined the core parsing layer, Vulkan runner implementation, C API surface, build system, test suite, and examples.

**Total findings: 8** across 7 categories. No High severity findings were identified — the codebase demonstrates strong security practices with proper error handling, input validation, and clean-room implementation against RetroArch. All 3 Medium severity findings (ROB-33, ROB-34, ROB-35) and 2 Low findings (SEC-07, PERF-09) have been closed; the 8 remaining findings are Low severity, spanning maintainability, performance, feature and testing.

Key strengths include excellent error handling with std::expected, clean separation of concerns between core and Vulkan layers, comprehensive test coverage with golden image comparisons, and robust build system with dependency pinning. The codebase successfully avoids RetroArch code contamination through the provenance gate and demonstrates mature Vulkan resource management patterns.

The most impactful remaining findings are the SPIR-V cache (PERF-08 — the library now supplies the key, the host-side store and the in-process memo remain) and the lack of a performance benchmarking framework (TEST-12), which is what would catch the next regression of the kind PERF-09 was.

---

## Findings Summary

| Prefix | Category |
|--------|----------|
| SEC | Security |
| ROB | Robustness |
| MAINT | Maintainability |
| PERF | Performance |
| FEAT | New Features |
| ARCH | Architecture |
| TEST | Testing |

| ID | Severity | Category | Title |
|----|----------|----------|-------|
| ROB-36 | Low | Robustness | Preset parser has complex state machine with edge cases |
| MAINT-22 | Low | Maintainability | Heavy use of pimpl pattern obscures Vulkan implementation details |
| MAINT-23 | Low | Maintainability | C API and C++ API have different lifetime rules |
| PERF-08 | Low | Performance | No caching of compiled SPIR-V shaders |
| FEAT-06 | Low | New Features | No shader hot-reloading during runtime |
| ARCH-04 | Low | Architecture | Single-threaded design limits concurrent use cases |
| TEST-11 | Low | Testing | Limited integration testing for full pipeline |
| TEST-12 | Low | Testing | No performance benchmarking framework |

Severity levels:
- **High** — Affects correctness, security, or data integrity. Fix soon.
- Medium — Impacts maintainability, reliability, or UX. Address in planned work.
- Low — Nice-to-have improvement. Address opportunistically.

---

## Detailed Findings

### Security


### Robustness


#### ROB-36: Preset parser has complex state machine with edge cases

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |
| **File(s)** | `src/core/preset_parser.cpp` |

The preset parser implements a complex state machine for handling .slangp files with multiple passes, includes, and references. While functional, it has subtle edge cases in include resolution, cycle detection, and parameter merging that could be difficult to maintain and debug.

**Current behavior:** The parser handles the libretro preset format correctly but uses intricate logic for path normalization, include resolution, and parameter validation. Some edge cases (like deeply nested includes or malformed preset files) may produce unexpected error messages.

**Recommendation:** Consider refactoring the parser into smaller, more focused components with clearer separation of concerns. Add more comprehensive unit tests for edge cases and error conditions.

**Impact:** Would improve maintainability and reduce the risk of subtle parsing bugs in edge cases.

---

### Maintainability

#### MAINT-22: Heavy use of pimpl pattern obscures Vulkan implementation details

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `include/slangvk/runner.hpp`, `src/vk/*.cpp` |

The library extensively uses the pimpl pattern to hide Vulkan implementation details, which is good for ABI compatibility but makes it difficult for developers to understand the Vulkan resource management and debugging issues.

**Current behavior:** All Vulkan objects (Device, Chain, Image) use pimpl implementations, so the header files only show the public API without revealing the underlying Vulkan handles or resource management patterns.

**Recommendation:** Consider providing documentation or examples that show the Vulkan resource patterns being used internally. For debugging purposes, consider adding optional debug methods that expose internal state.

**Impact:** Would help developers understand and debug Vulkan-related issues while maintaining the clean public API.

---

#### MAINT-23: C API and C++ API have different lifetime rules

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `include/slangvk/c.h`, `include/slangvk/runner.hpp` |

The C API and C++ API have different approaches to object lifetime management. The C++ API uses RAII with move semantics, while the C API uses explicit destruction functions. This creates cognitive overhead for developers using both APIs.

**Current behavior:** C++ objects follow RAII patterns (destruction in destructor), while C API requires explicit `slangvk_destroy_*` calls. The lifetime rules are similar but not identical.

**Recommendation:** Align the APIs more closely or provide clearer documentation about the lifetime differences. Consider adding RAII wrappers for the C API to make it more C++-friendly.

**Impact:** Would reduce confusion for developers working with both APIs and improve consistency across the library.

---

### Performance

#### PERF-08: No caching of compiled SPIR-V shaders

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Performance |
| **File(s)** | `src/vk/chain.cpp`, `src/core/slang_compile.cpp` |

The library compiles shaders to SPIR-V on every preset load but doesn't cache the compiled results. This means identical shaders are recompiled multiple times, which impacts startup time and memory usage.

**Current behavior:** Each time a preset is loaded, all its shaders are translated from slang to GLSL, then compiled to SPIR-V by glslang. Even if the same shader file is used by multiple presets, it's recompiled each time.

**Recommendation:** Add an optional SPIR-V cache system that stores compiled shaders to disk or memory. Cache keys could include shader content, glslang version, and target Vulkan version.

**Update (M4.07):** the library now supplies the key — the second half of the recommendation. `PresetLoadOptions::spirvCache` asks the host for a pass's compiled SPIR-V before the library invokes glslang (a hit skips the compile) and hands the freshly compiled bytes back afterwards. The key is a SHA-256 over the exact GLSL handed to glslang plus the compiler's version and target environment, so it changes exactly when the compiled output would: a host caching by it cannot serve a stale shader, not even when the preset includes other files, which a host-side hash of the `.slangp` alone would miss.

The **disk store stays the host's** — the reference host already keeps one (`shaders/cache/`), and a second cache in the library would disagree with it. What remains open: switching the reference host's cache over to the library's key, and the in-memory half of the recommendation (the same preset loaded twice in one process still recompiles).

**Impact:** Would significantly improve performance for applications that use many presets or reload presets frequently.

---


### New Features

#### FEAT-06: No shader hot-reloading during runtime

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | New Features |
| **File(s)** | `include/slangvk/runner.hpp`, `src/vk/chain.cpp` |

The library doesn't support hot-reloading shader files during runtime. Once a preset is loaded, changes to the underlying .slang files are not detected or applied until the preset is reloaded.

**Current behavior:** Presets are loaded and compiled once, then the resulting chains are used for rendering. There's no mechanism to detect changes to shader files or recompile on the fly.

**Recommendation:** Add a `Chain::reload()` method that can detect file changes and recompile shaders when needed. For development scenarios, consider adding a watch mode that automatically reloads changed shaders.

**Impact:** Would improve developer experience and enable dynamic shader experimentation without restarting applications.

---

### Architecture

#### ARCH-04: Single-threaded design limits concurrent use cases

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Architecture |
| **File(s)** | `include/slangvk/runner.hpp`, `src/vk/device.cpp` |

The library is designed as single-threaded per device, which limits its use in multi-threaded applications. While this simplifies the implementation, it prevents concurrent operations on the same device.

**Current behavior:** All operations on a Device are assumed to be single-threaded. There's no synchronization for concurrent access to the same device from multiple threads.

**Recommendation:** Add thread safety annotations and consider adding optional multi-threaded support for scenarios where concurrent access is needed. Document the current thread-safety guarantees clearly.

**Impact:** Would enable the library to be used in multi-threaded applications while maintaining the simple single-threaded API for most use cases.

---

### Testing

#### TEST-11: Limited integration testing for full pipeline

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Testing |
| **File(s)** | `tests/` |

While the library has comprehensive unit tests and golden image comparisons, it lacks integration tests that exercise the full pipeline from preset loading through rendering to output verification.

**Current behavior:** Tests are well-organized into unit tests (parser, translator, compiler), runner tests (headless, M2, M3, M4), and conformance tests, but there are few tests that verify the end-to-end workflow with real-world complexity.

**Recommendation:** Add integration tests that combine multiple components (preset parsing → translation → compilation → rendering) with complex presets and verify the complete output pipeline.

**Impact:** Would increase confidence in the library's ability to handle real-world scenarios and edge cases that span multiple components.

---

#### TEST-12: No performance benchmarking framework

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Testing |
| **File(s)** | `tests/` |

The library lacks a dedicated performance benchmarking framework to track performance regressions and measure optimization effectiveness. While the conformance tests include timing, they're focused on correctness rather than performance.

**Current behavior:** Performance is measured informally through conformance test timing and manual testing, but there's no systematic way to track performance over time or measure the impact of changes.

**Recommendation:** Add a benchmarking framework that can measure key metrics like compile time, render time, memory usage, and frame rates. Include it in CI to catch performance regressions.

**Impact:** Would enable systematic performance optimization and provide data-driven insights for improvement decisions.

---

## Priority Matrix

| Timeline | Findings |
|----------|----------|
| **Short term (v0.4.x)** | PERF-08 (SPIR-V caching) |
| **Medium term (v0.5.0+)** | MAINT-22, MAINT-23, FEAT-06, TEST-11, TEST-12, ROB-36, ARCH-04 |

Guidelines for timeline assignment:
- **Near term** — Medium severity findings that impact core functionality and developer experience
- **Short term** — Lower priority robustness and performance enhancements
- **Medium term** — Maintainability improvements and testing enhancements

---

## Architecture Strengths

The libslang-vk codebase demonstrates several excellent architectural patterns and design decisions that should be preserved:

1. **Excellent error handling** — The use of std::expected<T, Error> provides clear, structured error reporting with detailed messages and file locations. This pattern is consistent throughout the codebase and makes debugging much easier.

2. **Clean separation of concerns** — The library clearly separates the core parsing/compilation layer from the Vulkan execution layer. This allows the core to be used independently and makes the Vulkan implementation easier to maintain.

3. **Robust build system** — The CMake-based build system with pinned dependencies (glslang, Vulkan-Headers, volk, VMA) ensures reproducible builds and eliminates dependency conflicts.

4. **Comprehensive testing** — The test suite includes unit tests, conformance tests with golden images, and integration tests. The golden image comparisons provide excellent regression testing for visual correctness.

5. **Clean-room implementation** — The library successfully avoids RetroArch code contamination through the provenance gate and implements the libretro specification independently.

6. **Cross-platform Vulkan loading** — The use of volk for runtime Vulkan loading eliminates the need for a Vulkan SDK at link time while maintaining full Vulkan functionality.

7. **RAII and move semantics** — The C++ API properly uses RAII patterns and move semantics for resource management, preventing memory leaks and use-after-free issues.

8. **Well-documented public API** — The header files are comprehensive and include detailed documentation for all public interfaces, making the library easy to use and integrate.

These strengths form a solid foundation for the library and should be carefully preserved during any future refactoring or enhancement work.