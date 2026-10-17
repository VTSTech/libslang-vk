#pragma once

// libslang-vk — parse, translate and compile libretro slang shader presets,
// with a Vulkan execution path (M1).
//
// M0 umbrella: the GPU-agnostic core layer (preset data model, .slangp
// parsing, .slang translation, glslang SPIR-V compilation). All core symbols
// live in namespace slv::preset; the public runner API (namespace slv) lands
// with the Vulkan runner in M1 per plan §6.

#include "slangvk/export.h"
#include "slangvk/core/types.hpp"
#include "slangvk/core/preset_parser.hpp"
#include "slangvk/core/slang_translate.hpp"
#include "slangvk/core/slang_compile.hpp"
