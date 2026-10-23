cmake_minimum_required(VERSION 3.22)

# Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
# AI Assisted by Super-Z (GLM-5.3-Flash)
# License: MIT

# libslang-vk benchmark suite (M0).
# Measures performance of key operations: parsing, translation, compilation,
# and Vulkan chain setup.

find_package(benchmark REQUIRED EXCLUDE_FROM_ALL)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SLANGVK_TESTS_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tests/data")

# Benchmark internal include directory
set(BENCHMARK_INCLUDE /home/vtstech/local/benchmark/include CACHE PATH "Benchmark internal headers")

# Library search paths
set(BENCHMARK_LIB /home/vtstech/local/benchmark/lib CACHE PATH "Benchmark libraries")
set(SLANGVK_LIB /home/vtstech/workspace/libslang-vk/build_bench/lib_build CACHE PATH "libslangvk libraries")

# --- Benchmark: preset parsing ---
add_executable(bench_preset_parser bench_preset_parser.cpp)
target_link_libraries(bench_preset_parser PRIVATE benchmark slangvk)
target_link_libraries(bench_preset_parser PRIVATE -L${BENCHMARK_LIB} -lbenchmark -L${SLANGVK_LIB} -lslangvk)
target_compile_definitions(bench_preset_parser PRIVATE
    SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
target_compile_definitions(bench_preset_parser PRIVATE
    BENCHMARK_MODE=PRESET_PARSER)
target_include_directories(bench_preset_parser PRIVATE
    ${BENCHMARK_INCLUDE}
    /home/vtstech/workspace/libslang-vk/include
    /home/vtstech/workspace/libslang-vk/build_bench/build_bench/_deps/vulkan-headers-src/include)
target_compile_features(bench_preset_parser PUBLIC cxx_std_23)

# --- Benchmark: slang translate ---
add_executable(bench_slang_translate bench_slang_translate.cpp)
target_link_libraries(bench_slang_translate PRIVATE benchmark slangvk)
target_link_libraries(bench_slang_translate PRIVATE -L${BENCHMARK_LIB} -lbenchmark -L${SLANGVK_LIB} -lslangvk)
target_compile_definitions(bench_slang_translate PRIVATE
    SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
target_compile_definitions(bench_slang_translate PRIVATE
    BENCHMARK_MODE=SLANG_TRANSLATE)
target_include_directories(bench_slang_translate PRIVATE
    ${BENCHMARK_INCLUDE}
    /home/vtstech/workspace/libslang-vk/include
    /home/vtstech/workspace/libslang-vk/build_bench/build_bench/_deps/vulkan-headers-src/include)
target_compile_features(bench_slang_translate PUBLIC cxx_std_23)

# --- Benchmark: slang compile ---
add_executable(bench_slang_compile bench_slang_compile.cpp)
target_link_libraries(bench_slang_compile PRIVATE benchmark slangvk)
target_link_libraries(bench_slang_compile PRIVATE -L${BENCHMARK_LIB} -lbenchmark -L${SLANGVK_LIB} -lslangvk)
target_compile_definitions(bench_slang_compile PRIVATE
    SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
target_compile_definitions(bench_slang_compile PRIVATE
    BENCHMARK_MODE=SLANG_COMPILE)
target_include_directories(bench_slang_compile PRIVATE
    ${BENCHMARK_INCLUDE}
    /home/vtstech/workspace/libslang-vk/include
    /home/vtstech/workspace/libslang-vk/build_bench/build_bench/_deps/vulkan-headers-src/include)
target_compile_features(bench_slang_compile PUBLIC cxx_std_23)

# --- Benchmark: Vulkan chain creation ---
add_executable(bench_vk_chain bench_vk_chain.cpp)
target_link_libraries(bench_vk_chain PRIVATE benchmark slangvk)
target_link_libraries(bench_vk_chain PRIVATE -L${BENCHMARK_LIB} -lbenchmark -L${SLANGVK_LIB} -lslangvk)
target_compile_definitions(bench_vk_chain PRIVATE
    SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
target_compile_definitions(bench_vk_chain PRIVATE
    BENCHMARK_MODE=VK_CHAIN)
target_include_directories(bench_vk_chain PRIVATE
    ${BENCHMARK_INCLUDE}
    /home/vtstech/workspace/libslang-vk/include
    /home/vtstech/workspace/libslang-vk/build_bench/build_bench/_deps/vulkan-headers-src/include)
target_compile_features(bench_vk_chain PUBLIC cxx_std_23)

# --- Benchmark: render iteration ---
add_executable(bench_vk_render bench_vk_render.cpp)
target_link_libraries(bench_vk_render PRIVATE benchmark slangvk)
target_link_libraries(bench_vk_render PRIVATE -L${BENCHMARK_LIB} -lbenchmark -L${SLANGVK_LIB} -lslangvk)
target_compile_definitions(bench_vk_render PRIVATE
    SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
target_compile_definitions(bench_vk_render PRIVATE
    BENCHMARK_MODE=VK_RENDER)
target_include_directories(bench_vk_render PRIVATE
    ${BENCHMARK_INCLUDE}
    /home/vtstech/workspace/libslang-vk/include
    /home/vtstech/workspace/libslang-vk/build_bench/build_bench/_deps/vulkan-headers-src/include)
target_compile_features(bench_vk_render PUBLIC cxx_std_23)

if(NOT DEFINED BENCHMARK_INSTALL_DIR)
    set(BENCHMARK_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()

install(TARGETS
    bench_preset_parser
    bench_slang_translate
    bench_slang_compile
    bench_vk_chain
    bench_vk_render
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)