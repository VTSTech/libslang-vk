# libslang-vk examples.
#
# preset_info — the M0 demo CLI: parse a .slangp preset, print its pass
# chain / resources, then translate + compile every pass to SPIR-V. Each
# bundled demo preset doubles as a ctest smoke test so the example path
# stays green on CI.

set(SLANGVK_EXAMPLES_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/presets")

add_executable(preset_info preset_info.cpp)
target_link_libraries(preset_info PRIVATE slangvk)
target_compile_definitions(preset_info PRIVATE
    SLANGVK_EXAMPLES_DATA_DIR="${SLANGVK_EXAMPLES_DATA_DIR}")

foreach(demo demo-bright demo-glow)
	add_test(NAME example_preset_info_${demo}
	    COMMAND preset_info "${SLANGVK_EXAMPLES_DATA_DIR}/${demo}.slangp")
endforeach()
