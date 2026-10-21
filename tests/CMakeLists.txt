# Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
# AI Assisted by Super-Z (GLM-5.3-Flash)
# License: MIT

# libslang-vk test suite (M0).
#
# Two tiers, inherited from the reference host's audit methodology:
#  - lib-backed: tests linking the real library (parser/translate/compile,
#    the field-round regression suite).
#  - reference pins: stdlib-only ports of the fork's standalone logic tests
#    (history ring direction, mipmap planning, uniform naming, viewport
#    derivation, rotation/format handling) — they pin the algorithms the
#    M1/M2 Vulkan runner must reproduce (see PROVENANCE.md).

set(SLANGVK_TESTS_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

# --- lib-backed tests -------------------------------------------------------

add_executable(test_preset_parser test_preset_parser.cpp)
target_link_libraries(test_preset_parser PRIVATE slangvk)
add_test(NAME preset_parser COMMAND test_preset_parser)

add_executable(test_slang_translate test_slang_translate.cpp)
target_link_libraries(test_slang_translate PRIVATE slangvk)
add_test(NAME slang_translate COMMAND test_slang_translate)

add_executable(test_slang_compile test_slang_compile.cpp)
target_link_libraries(test_slang_compile PRIVATE slangvk)
add_test(NAME slang_compile COMMAND test_slang_compile)

foreach(round 2 3 4)
        add_executable(test_field_round${round} test_field_round${round}.cpp)
        target_link_libraries(test_field_round${round} PRIVATE slangvk)
        add_test(NAME field_round${round} COMMAND test_field_round${round})
endforeach()

# Optional-corpus argument is not wired in CI; pass a slang-shaders checkout
# manually: ctest -R field_round --test-dir build --extra-args=... or run the
# binaries directly with the corpus root as argv[1].
target_compile_definitions(test_field_round4 PRIVATE
    SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")

# --- reference pins (stdlib-only) -------------------------------------------

foreach(t
    test_field_round1
    test_history_ring
    test_mipmap_plan
    test_uniform_names
    test_viewport_derive
    test_rotation_format)
        add_executable(${t} reference/${t}.cpp)
        add_test(NAME ${t} COMMAND ${t})
endforeach()

# --- Vulkan runner headless tests (M1 + M2) -----------------------------------
# M1: golden blit round-trip, two-pass alias chain, parameter fill.
# M2: full chain semantics — feedback ping-pong, history ring, FBO formats,
# include/cycle, mip refill, resize recreation, live parameters — on the
# fork's field-preset canaries. Skips cleanly when no Vulkan device is
# present (no ICD on Windows CI).
if(SLANGVK_ENABLE_VK)
        add_executable(test_runner_headless test_runner_headless.cpp)
        target_link_libraries(test_runner_headless PRIVATE slangvk)
        target_compile_definitions(test_runner_headless PRIVATE
            SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
        add_test(NAME runner_headless COMMAND test_runner_headless)

        add_executable(test_runner_m2 test_runner_m2.cpp)
        target_link_libraries(test_runner_m2 PRIVATE slangvk)
        target_compile_definitions(test_runner_m2 PRIVATE
            SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
        add_test(NAME runner_m2 COMMAND test_runner_m2)

        add_executable(test_runner_m3 test_runner_m3.cpp)
        target_link_libraries(test_runner_m3 PRIVATE slangvk)
        target_compile_definitions(test_runner_m3 PRIVATE
            SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
        add_test(NAME runner_m3 COMMAND test_runner_m3)

        # M4 links the vendored volk directly: the swapchain end-to-end case
        # drives acquire/present/layout transitions through the loaded loader.
        add_executable(test_runner_m4 test_runner_m4.cpp)
        target_link_libraries(test_runner_m4 PRIVATE slangvk slv-volk)
        target_compile_definitions(test_runner_m4 PRIVATE
            SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
        add_test(NAME runner_m4 COMMAND test_runner_m4)

        # Field regression: the stock libretro shader shape (global MVP UBO +
        # push-constant block read by the fragment stage) must build its pass
        # pipeline — the layout covers set 1/set 3 bindings 0 AND 1.
        add_executable(test_runner_pushblock test_runner_pushblock.cpp)
        target_link_libraries(test_runner_pushblock PRIVATE slangvk)
        target_compile_definitions(test_runner_pushblock PRIVATE
            SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
        add_test(NAME runner_pushblock COMMAND test_runner_pushblock)

        # M4.06 async render path (audit ROB-33): render_async() submits a
        # frame and returns a fence without blocking on the GPU; wait_async()
        # retires it. The test pins the async/sync pixel equivalence, the
        # one-in-flight-per-chain rule and wait_async()'s error contract.
        add_executable(test_runner_async test_runner_async.cpp)
        target_link_libraries(test_runner_async PRIVATE slangvk)
        target_compile_definitions(test_runner_async PRIVATE
            SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
        add_test(NAME runner_async COMMAND test_runner_async)

        # M4.06 device-lost recovery (audit ROB-35): Device::recover() replaces
        # the VkDevice, and the chain notices that its resources belong to a
        # replaced device and rebuilds itself before the next frame. The test
        # drives the same machinery the automatic recovery path uses.
        add_executable(test_runner_recovery test_runner_recovery.cpp)
        target_link_libraries(test_runner_recovery PRIVATE slangvk)
        target_compile_definitions(test_runner_recovery PRIVATE
            SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
        add_test(NAME runner_recovery COMMAND test_runner_recovery)

        add_executable(probe_test probe_test.cpp)
        target_link_libraries(probe_test PRIVATE slangvk)
        target_compile_definitions(probe_test PRIVATE
            SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")

        # C API smoke: the extern "C" surface (slangvk/c.h) drives the same
        # headless path end-to-end — device create, image create/update,
        # preset load + parameter metadata + live override, identity/gain
        # chains, resize, error paths — with byte-exact readback expectations.
        # The test TU consumes ONLY the C header; readback goes through
        # slangvk_device_read_pixels, so this also pins the full C contract.
        add_executable(test_c_api test_c_api.cpp)
        target_link_libraries(test_c_api PRIVATE slangvk)
        target_compile_definitions(test_c_api PRIVATE
            SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
        add_test(NAME c_api COMMAND test_c_api)
endif()
