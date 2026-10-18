# libslang-vk examples.
#
# preset_info — the M0 demo CLI: parse a .slangp preset, print its pass
# chain / resources, then translate + compile every pass to SPIR-V. Each
# bundled demo preset doubles as a ctest smoke test so the example path
# stays green on CI.
#
# shader_demo — the interactive host demo (plan §11 "embedding story"):
# loads presets, renders chains and switches presets/inputs/parameters
# live. Windowed (GLFW + M4 swapchain presentation) when a display exists,
# headless PNG dumps otherwise.

set(SLANGVK_EXAMPLES_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/presets")
set(SLANGVK_TESTS_DATA_DIR "${CMAKE_SOURCE_DIR}/tests/data")

add_executable(preset_info preset_info.cpp)
target_link_libraries(preset_info PRIVATE slangvk)
target_compile_definitions(preset_info PRIVATE
    SLANGVK_EXAMPLES_DATA_DIR="${SLANGVK_EXAMPLES_DATA_DIR}")

# shader_demo options. SLANGVK_DEMO_WINDOW fetches GLFW 3.4 and compiles the
# windowed path (runtime still falls back to headless PNG mode when no
# display/Vulkan-surface is available). Linux hosts need the usual GLFW dev
# packages at configure time (libx11-dev/libxkbcommon-dev, or the Wayland
# equivalents); turn the option OFF for a pure headless demo build.
option(SLANGVK_DEMO_WINDOW "Build the shader demo with GLFW window support" ON)
# SLANGVK_DEMO_FETCH_SHADERS shallow-clones libretro/slang-shaders at configure
# time and seeds the demo's default preset search with it (large download;
# runtime paths work without it too).
option(SLANGVK_DEMO_FETCH_SHADERS "Fetch libretro/slang-shaders for the shader demo" OFF)

if(SLANGVK_ENABLE_VK)
        if(SLANGVK_DEMO_WINDOW)
                include(FetchContent)
                set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
                set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
                set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
                set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
                FetchContent_Declare(glfw
                    GIT_REPOSITORY https://github.com/glfw/glfw.git
                    GIT_TAG 3.4)
                FetchContent_MakeAvailable(glfw)
        endif()

        if(SLANGVK_DEMO_FETCH_SHADERS)
                include(FetchContent)
                FetchContent_Declare(slang_shaders
                    GIT_REPOSITORY https://github.com/libretro/slang-shaders.git
                    GIT_TAG master
                    GIT_SHALLOW TRUE)
                FetchContent_MakeAvailable(slang_shaders)
        endif()

        add_executable(shader_demo shader_demo/shader_demo.cpp)
        target_link_libraries(shader_demo PRIVATE slangvk)
        # stb_image.h decodes the bundled input image (stb_image_write.h writes
        # the headless dumps/screenshots) — the vendored copies live next to the
        # library's own LUT decoder.
        target_include_directories(shader_demo PRIVATE
            ${CMAKE_SOURCE_DIR}/vendor/stb)
        if(SLANGVK_DEMO_WINDOW)
                target_link_libraries(shader_demo PRIVATE glfw)
                target_compile_definitions(shader_demo PRIVATE SLANGVK_DEMO_WINDOW=1)
        endif()
        set(SLANGVK_DEMO_DEFINITIONS
            SLANGVK_EXAMPLES_DATA_DIR="${SLANGVK_EXAMPLES_DATA_DIR}"
            SLANGVK_DEMO_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shader_demo/assets")
        if(SLANGVK_DEMO_FETCH_SHADERS)
                FetchContent_GetProperties(slang_shaders SOURCE_DIR slang_shaders_dir)
                list(APPEND SLANGVK_DEMO_DEFINITIONS
                    SLANGVK_DEMO_DEFAULT_CORPUS="${slang_shaders_dir}")
        endif()
        target_compile_definitions(shader_demo PRIVATE ${SLANGVK_DEMO_DEFINITIONS})
endif()

# corpus_sweep — M3 conformance scaffolding (plan §11): walks preset
# directories through the real load path (parse -> translate -> compile ->
# LUT upload) and optionally renders chains on the Vulkan device. The ctest
# smoke pins the bundled fixture trees; the full shader-pack corpus sweep
# runs out of CI against a slang-shaders checkout. Gated on the Vulkan
# runner like the runner tests (Device::create is a runner symbol).
if(SLANGVK_ENABLE_VK)
        add_executable(corpus_sweep corpus_sweep.cpp)
        target_link_libraries(corpus_sweep PRIVATE slangvk)
endif()

foreach(demo demo-bright demo-glow)
        add_test(NAME example_preset_info_${demo}
            COMMAND preset_info "${SLANGVK_EXAMPLES_DATA_DIR}/${demo}.slangp")
endforeach()

# Compile-only sweep over the bundled valid fixtures (field-preset negative
# controls live in tests/data/field-presets and are intentionally excluded).
if(SLANGVK_ENABLE_VK)
        add_test(NAME example_corpus_sweep_smoke
            COMMAND corpus_sweep "${SLANGVK_EXAMPLES_DATA_DIR}"
                "${SLANGVK_TESTS_DATA_DIR}/runner" "${SLANGVK_TESTS_DATA_DIR}/runner3"
                --fail-on-broken)

        # shader_demo smokes: --list needs no device (runs everywhere); the
        # headless dump path renders the bundled demo presets and skips
        # cleanly (exit 0) when no ICD is present.
        add_test(NAME example_shader_demo_list
            COMMAND shader_demo --list)
        add_test(NAME example_shader_demo_headless
            COMMAND shader_demo --no-menu --frames 2
                --out "${CMAKE_CURRENT_BINARY_DIR}/shader_demo_smoke"
                "${SLANGVK_EXAMPLES_DATA_DIR}")
endif()
