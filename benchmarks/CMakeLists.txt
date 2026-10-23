cmake_minimum_required(VERSION 3.22)

# Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
# AI Assisted by Super-Z (GLM-5.3-Flash)
# License: MIT

# libslang-vk benchmark suite (M0).
# Measures performance of key operations: parsing, translation, compilation,
# and Vulkan chain setup.
#
# Built as part of the main project (SLANGVK_BUILD_BENCHMARKS=ON) so the
# benchmarks link the in-tree `slangvk::slangvk` target with its transitive
# dependencies (glslang, volk, Vulkan headers) — no system installs and no
# machine-specific paths. google/benchmark comes in via FetchContent, pinned,
# with its own tests/GTest disabled.

include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1)
FetchContent_MakeAvailable(benchmark)

set(SLANGVK_TESTS_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tests/data")

# Shared link/define helper (applies to every bench target)
function(slangvk_bench_common target)
    target_link_libraries(${target} PRIVATE benchmark::benchmark)
    target_link_libraries(${target} PRIVATE slangvk::slangvk)
    target_compile_definitions(${target} PRIVATE
        SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
    target_compile_features(${target} PUBLIC cxx_std_23)
endfunction()

# --- Benchmark: preset parsing ---
add_executable(bench_preset_parser bench_preset_parser.cpp)
slangvk_bench_common(bench_preset_parser)
target_compile_definitions(bench_preset_parser PRIVATE
    BENCHMARK_MODE=PRESET_PARSER)

# --- Benchmark: slang translate ---
add_executable(bench_slang_translate bench_slang_translate.cpp)
slangvk_bench_common(bench_slang_translate)
target_compile_definitions(bench_slang_translate PRIVATE
    BENCHMARK_MODE=SLANG_TRANSLATE)

# --- Benchmark: slang compile ---
add_executable(bench_slang_compile bench_slang_compile.cpp)
slangvk_bench_common(bench_slang_compile)
target_compile_definitions(bench_slang_compile PRIVATE
    BENCHMARK_MODE=SLANG_COMPILE)

# --- Benchmark: Vulkan chain creation ---
add_executable(bench_vk_chain bench_vk_chain.cpp)
slangvk_bench_common(bench_vk_chain)
target_compile_definitions(bench_vk_chain PRIVATE
    BENCHMARK_MODE=VK_CHAIN)

# --- Benchmark: render iteration ---
add_executable(bench_vk_render bench_vk_render.cpp)
slangvk_bench_common(bench_vk_render)
target_compile_definitions(bench_vk_render PRIVATE
    BENCHMARK_MODE=VK_RENDER)

# --- Benchmark: render iteration with FXAA ---
add_executable(bench_vk_render_fxaa bench_vk_render_fxaa.cpp)
slangvk_bench_common(bench_vk_render_fxaa)
target_compile_definitions(bench_vk_render_fxaa PRIVATE
    BENCHMARK_MODE=VK_RENDER_FXAA)

if(NOT DEFINED BENCHMARK_INSTALL_DIR)
    set(BENCHMARK_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()

install(TARGETS
    bench_preset_parser
    bench_slang_translate
    bench_slang_compile
    bench_vk_chain
    bench_vk_render
    bench_vk_render_fxaa
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
