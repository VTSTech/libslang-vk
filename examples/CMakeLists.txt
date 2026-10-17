# libslang-vk examples.
#
# preset_info — the M0 demo CLI: parse a .slangp preset, print its pass
# chain / resources, then translate + compile every pass to SPIR-V. Each
# bundled demo preset doubles as a ctest smoke test so the example path
# stays green on CI.

set(SLANGVK_EXAMPLES_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/presets")
set(SLANGVK_TESTS_DATA_DIR "${CMAKE_SOURCE_DIR}/tests/data")

add_executable(preset_info preset_info.cpp)
target_link_libraries(preset_info PRIVATE slangvk)
target_compile_definitions(preset_info PRIVATE
    SLANGVK_EXAMPLES_DATA_DIR="${SLANGVK_EXAMPLES_DATA_DIR}")

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
endif()
