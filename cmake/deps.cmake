# Third-party dependencies — all permissively licensed (plan §4).
#
# M0 wires glslang only: it is the one dependency the core layer consumes.
# Vulkan-Headers and VulkanMemoryAllocator are declared here and MakeAvailable
# flips on in M1 when the Vulkan runner starts consuming them — declaring them
# now pins the provenance/licensing intent without paying the fetch cost in a
# build that cannot use them yet.

include(FetchContent)

# glslang — Khronos reference GLSL frontend; compiles translated .slang to
# SPIR-V in-process (the same frontend RetroArch uses for its slang pipeline).
# Same pinned version as the reference host for behavioral parity.
# Link targets: glslang::glslang, glslang::SPIRV,
# glslang::glslang-default-resource-limits (GetDefaultResources()).
set(BUILD_EXTERNAL OFF)
set(BUILD_WERROR OFF)
set(GLSLANG_TESTS OFF)
set(GLSLANG_ENABLE_INSTALL OFF)
set(ENABLE_SPIRV ON)
set(ENABLE_GLSLANG_BINARIES OFF)
set(ENABLE_GLSLANG_JS OFF)
set(ENABLE_HLSL ON)
set(ENABLE_OPT OFF) # skips the SPIRV-Tools submodule dependency
set(ENABLE_SPVREMAPPER OFF)
set(ENABLE_PCH ON)
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(glslang
  URL https://github.com/KhronosGroup/glslang/archive/refs/tags/16.6.0.tar.gz
  URL_HASH SHA256=9c09b901149c729df745057dafa815278aaa101b84d2b6e14f16a42de52f97f2
)
FetchContent_MakeAvailable(glslang)

# M1 (Vulkan runner) will add:
#FetchContent_Declare(Vulkan-Headers
#  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
#  GIT_TAG <pinned in M1>)
#FetchContent_Declare(VulkanMemoryAllocator
#  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
#  GIT_TAG <pinned in M1>)
