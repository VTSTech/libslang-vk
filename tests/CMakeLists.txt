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

# --- Vulkan runner headless test (M1) ----------------------------------------
# Renders the golden blit round-trip on lavapipe (CI) or a real GPU; skips
# cleanly when no Vulkan device is present.
if(SLANGVK_ENABLE_VK)
	add_executable(test_runner_headless test_runner_headless.cpp)
	target_link_libraries(test_runner_headless PRIVATE slangvk)
	target_compile_definitions(test_runner_headless PRIVATE
	    SLANGVK_TESTS_DATA_DIR="${SLANGVK_TESTS_DATA_DIR}")
	add_test(NAME runner_headless COMMAND test_runner_headless)
endif()
