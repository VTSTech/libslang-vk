# Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
# AI Assisted by Super-Z (GLM-5.3-Flash)
# License: MIT

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
# display/Vulkan-surface is available). GLFW 3.4 hard-fails its own configure
# when a Linux windowing stack is only half present ("Failed to find
# wayland-scanner" on X11-only CI images, "RandR headers not found" without
# libxrandr-dev), so every required dev package is probed up front: X11 needs
# libx11/xrandr/xinerama/xext(xkb,shape)/xcursor/xi headers, Wayland needs
# libwayland-dev (client/cursor/egl + wayland-scanner) and libxkbcommon-dev.
# GLFW is only fetched when one full backend exists; with neither, the demo
# builds headless-only. First-configure overrides: -DGLFW_BUILD_X11=ON/OFF
# and -DGLFW_BUILD_WAYLAND=ON/OFF.
option(SLANGVK_DEMO_WINDOW "Build the shader demo with GLFW window support" ON)
# SLANGVK_DEMO_FETCH_SHADERS shallow-clones libretro/slang-shaders at configure
# time and seeds the demo's default preset search with it (large download;
# runtime paths work without it too).
option(SLANGVK_DEMO_FETCH_SHADERS "Fetch libretro/slang-shaders for the shader demo" OFF)

if(SLANGVK_ENABLE_VK)
        # Windowed-demo backend detection (rationale in the option comment
        # above). Every GLFW-required Linux dev package is probed before
        # FetchContent so a partial stack degrades to headless-only instead of
        # killing configure. Non-Linux hosts keep GLFW's always-available
        # Win32/Cocoa backends.
        set(SLANGVK_DEMO_BUILD_WINDOW OFF)
        set(SLANGVK_HAVE_X11 OFF)
        set(SLANGVK_HAVE_WAYLAND OFF)
        if(SLANGVK_DEMO_WINDOW)
                if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
                        set(SLANGVK_DEMO_BUILD_WINDOW ON)
                else()
                        find_package(PkgConfig QUIET)
                        # GLFW's Wayland branch runs pkg_check_modules(... REQUIRED
                        # wayland-client wayland-cursor wayland-egl xkbcommon),
                        # so xkbcommon gates the Wayland backend (the X11 branch
                        # uses Xlib's own XKB and does not need it).
                        set(SLANGVK_HAVE_XKBCOMMON OFF)
                        if(PKG_CONFIG_FOUND)
                                pkg_check_modules(SLANGVK_PKG_XKBCOMMON QUIET
                                    xkbcommon)
                                if(SLANGVK_PKG_XKBCOMMON_FOUND)
                                        set(SLANGVK_HAVE_XKBCOMMON ON)
                                endif()
                        endif()
                        # X11: core headers plus the six extension include dirs
                        # GLFW 3.4 fatals on (RandR, Xinerama, XKB, Xcursor,
                        # XInput2, XShape) — mirrored on FindX11's INCLUDE_PATH
                        # results, exactly the variables GLFW itself tests.
                        find_package(X11 QUIET)
                        if(X11_FOUND
                                AND X11_Xrandr_INCLUDE_PATH
                                AND X11_Xinerama_INCLUDE_PATH
                                AND X11_Xkb_INCLUDE_PATH
                                AND X11_Xcursor_INCLUDE_PATH
                                AND X11_Xi_INCLUDE_PATH
                                AND X11_Xshape_INCLUDE_PATH)
                                set(SLANGVK_HAVE_X11 ON)
                        endif()
                        # Wayland: wayland-scanner + the client libraries
                        # + xkbcommon (all REQUIRED by GLFW 3.4).
                        if(PKG_CONFIG_FOUND AND SLANGVK_HAVE_XKBCOMMON)
                                find_program(SLANGVK_WAYLAND_SCANNER_EXE
                                    wayland-scanner)
                                if(SLANGVK_WAYLAND_SCANNER_EXE)
                                        pkg_check_modules(SLANGVK_PKG_WAYLAND QUIET
                                            wayland-client wayland-cursor
                                            wayland-egl)
                                        if(SLANGVK_PKG_WAYLAND_FOUND)
                                                set(SLANGVK_HAVE_WAYLAND ON)
                                        endif()
                                endif()
                        endif()
                        if(SLANGVK_HAVE_X11 OR SLANGVK_HAVE_WAYLAND)
                                set(SLANGVK_DEMO_BUILD_WINDOW ON)
                        endif()
                        if(SLANGVK_HAVE_X11 AND SLANGVK_HAVE_WAYLAND)
                                message(STATUS "shader_demo: GLFW windowed with X11 + Wayland")
                        elseif(SLANGVK_HAVE_X11)
                                message(STATUS "shader_demo: GLFW windowed with X11 (Wayland dev stack not found)")
                        elseif(SLANGVK_HAVE_WAYLAND)
                                message(STATUS "shader_demo: GLFW windowed with Wayland (X11 dev stack not found)")
                        else()
                                message(STATUS "shader_demo: no complete Linux windowing dev stack found, building headless-only demo")
                        endif()
                endif()
        endif()

        # Re-detect our own auto-written backend toggles on reconfigure; an
        # explicit -DGLFW_BUILD_X11/GLFW_BUILD_WAYLAND that differs from the
        # auto mirror is user intent and is left untouched.
        foreach(_backend X11 WAYLAND)
                if(DEFINED GLFW_BUILD_${_backend}
                        AND DEFINED SLANGVK_AUTO_GLFW_${_backend}
                        AND GLFW_BUILD_${_backend} STREQUAL SLANGVK_AUTO_GLFW_${_backend})
                        unset(GLFW_BUILD_${_backend} CACHE)
                endif()
        endforeach()

        if(SLANGVK_DEMO_BUILD_WINDOW)
                include(FetchContent)
                set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
                set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
                set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
                set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
                if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                        if(NOT DEFINED GLFW_BUILD_X11)
                                set(GLFW_BUILD_X11 ${SLANGVK_HAVE_X11}
                                    CACHE BOOL "" FORCE)
                        endif()
                        if(NOT DEFINED GLFW_BUILD_WAYLAND)
                                set(GLFW_BUILD_WAYLAND ${SLANGVK_HAVE_WAYLAND}
                                    CACHE BOOL "" FORCE)
                        endif()
                        set(SLANGVK_AUTO_GLFW_X11 ${GLFW_BUILD_X11}
                            CACHE INTERNAL "Auto-detected GLFW X11 toggle")
                        set(SLANGVK_AUTO_GLFW_WAYLAND ${GLFW_BUILD_WAYLAND}
                            CACHE INTERNAL "Auto-detected GLFW Wayland toggle")
                endif()
                FetchContent_Declare(glfw
                    GIT_REPOSITORY https://github.com/glfw/glfw.git
                    GIT_TAG 3.4
                    GIT_SHALLOW TRUE)
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
        if(SLANGVK_DEMO_BUILD_WINDOW)
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

# conformance — M4.04: golden-image rendering + perf budgets (the M3
# leftovers). golden: renders every tests/golden/manifest.txt entry at a
# fixed size from the procedural testcard and compares the final frame
# against the committed PNGs in tests/golden/images (tolerances per entry;
# --update regenerates). perf: per-frame wall-clock around the synchronous
# fenced Chain::render, --budgets gate for real hardware, --report ledger.
# Both modes skip cleanly (exit 0) when no Vulkan device exists. The perf
# ctest is an informational smoke over a fixed whitelist without budgets so
# sandbox/CPU timing variance can never flake the suite.
if(SLANGVK_ENABLE_VK)
        add_executable(conformance conformance.cpp)
        target_link_libraries(conformance PRIVATE slangvk)
        target_include_directories(conformance PRIVATE ${CMAKE_SOURCE_DIR}/vendor/stb)
        target_compile_definitions(conformance PRIVATE
            SLANGVK_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")

        add_test(NAME example_conformance_golden
            COMMAND conformance golden
                "${CMAKE_SOURCE_DIR}/tests/data" "${CMAKE_SOURCE_DIR}/examples")
        add_test(NAME example_conformance_perf
            COMMAND conformance perf
                "${CMAKE_SOURCE_DIR}/tests/data"
                --only-from "${CMAKE_SOURCE_DIR}/tests/golden/perf-smoke.txt"
                --warmup 1 --timed 5)
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
        # cleanly (exit 0) when no ICD is present. The image run covers the
        # default static-upload path (testcard.png via the portable asset
        # lookup); the pattern run covers the per-frame Image::update path.
        add_test(NAME example_shader_demo_list
            COMMAND shader_demo --list)
        add_test(NAME example_shader_demo_headless
            COMMAND shader_demo --no-menu --frames 2
                --out "${CMAKE_CURRENT_BINARY_DIR}/shader_demo_smoke"
                "${SLANGVK_EXAMPLES_DATA_DIR}")
        add_test(NAME example_shader_demo_headless_pattern
            COMMAND shader_demo --no-menu --frames 2 --input pattern
                --out "${CMAKE_CURRENT_BINARY_DIR}/shader_demo_smoke_pattern"
                "${SLANGVK_EXAMPLES_DATA_DIR}")
endif()
