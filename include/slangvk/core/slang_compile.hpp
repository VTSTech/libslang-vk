// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "slangvk/export.h"
#include "slangvk/core/slang_translate.hpp"

namespace slv {
namespace preset {

struct PassSpirv {
	std::vector<uint8_t> vertex;   // SPIR-V bytecode for the vertex stage.
	std::vector<uint8_t> fragment; // SPIR-V bytecode for the fragment stage.
};

/**
 * @brief Compiles one translated .slang pass to SPIR-V (Vulkan 1.0 target,
 * GLSL 450) using the in-process glslang frontend.
 *
 * libslang-vk divergence from the reference host (see PROVENANCE.md): the
 * fork layered precompiled sidecar files and a content-addressed disk cache
 * in front of this call; v1 compiles with glslang on every Preset::load by
 * decision (plan §2, non-goal: on-disk SPIR-V cache). Nothing here touches
 * the filesystem.
 *
 * @param translation Translated stage sources and metadata.
 * @param out Receives the SPIR-V bytecode.
 * @param error Receives a human-readable failure reason.
 */
SLANGVK_API bool CompilePass(const SlangTranslation &translation, PassSpirv &out, std::string &error);

/**
 * @brief The content key of a pass's compiled output.
 *
 * A stable, hex-encoded digest that identifies the exact SPIR-V
 * `CompilePass` would produce for this pass. It covers the GLSL handed to
 * glslang (which already includes the include resolution, the preset's
 * declared texture names and the library's translation semantics) plus the
 * compiler's version and target environment.
 *
 * Hashing the compiler's *input* is what makes the key correct by
 * construction: the key changes exactly when the compiled output would
 * change, so a host that caches by this key can never serve a stale
 * shader. The key is stable across processes and machines.
 *
 * This is the "cache enabler": the library owns the key (only it knows
 * what determines the output); the host owns the bytes.
 */
SLANGVK_API auto ComputePassCacheKey(const SlangTranslation &translation) -> std::string;

} // namespace preset
} // namespace slv
