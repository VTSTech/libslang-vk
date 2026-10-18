// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "slangvk/export.h"

namespace slv {
namespace preset {

/** @brief std140 block member types we support in slang UBO/push blocks. */
enum class UniformType : uint8_t {
        Float,
        Vec2,
        Vec3,
        Vec4,
        Int,
        IVec2,
        IVec3,
        IVec4,
        Uint,
        UVec2,
        UVec3,
        UVec4,
        Bool,
        Mat4,
};

struct UniformMember {
        std::string name;
        UniformType type = UniformType::Float;
        int arraySize = 0;   // 0 = scalar member; > 0 = array with this element count.
        uint32_t offset = 0; // std140 byte offset inside the block.
        uint32_t size = 0;   // Total std140 byte size (arrays included).
};

/** @brief One rewritten uniform block (the original UBO or push-constant block). */
struct UniformBlock {
        std::string instanceName;           // Instance identifier as used by the shader ("" if none).
        uint32_t binding = 0;               // Binding assigned after the rewrite (dense, 0..1).
        std::vector<UniformMember> members; // std140 reflected layout.
};

/** @brief A texture the shader samples, with its assigned dense binding. */
struct TextureRef {
        std::string name;     // "Source", "Original", "OriginalHistory1", alias or LUT name.
        uint32_t binding = 0; // Dense binding in set 2 (fragment).
};

struct ParamInfo {
        std::string name;
        std::string description;
        float defaultValue = 0.0F;
        float minimum = 0.0F;
        float maximum = 1.0F;
        float step = 0.001F;
};

struct StageTranslation {
        bool present = false;
        std::string glsl; // Final GLSL source for this stage.
        std::vector<UniformBlock> uniformBlocks;
        std::vector<TextureRef> textures;
        bool usesMvp = false;
        bool usesFrameCount = false;
        bool usesFrameDirection = false;
        bool usesFrameTime = false;
        bool usesPassIndex = false;
        // Field diagnostics: raw sampling-style usage counts of the stage GLSL.
        int texelFetchCount = 0;
        int textureGatherCount = 0;
};

struct SlangTranslation {
        StageTranslation vertex;
        StageTranslation fragment;
        std::vector<ParamInfo> parameters; // #pragma parameter metadata.
        std::string pragmaName;            // #pragma name
        std::string pragmaFormat;          // #pragma format
        // Field diagnostics: content fingerprints (raw file, include-resolved merge).
        int sourceSize = 0;
        unsigned sourceHash = 0;
        int mergedSize = 0;
        unsigned mergedHash = 0;
        std::string error;
        std::vector<std::string> warnings; // Non-fatal notes: deviations tolerated at translate time.

        bool ok() const { return error.empty() && (vertex.present || fragment.present); }
};

/**
 * @brief Translates one .slang source file into per-stage GLSL ready for
 * glslang Vulkan compilation, with descriptors rewritten to the libslang-vk
 * descriptor conventions (inherited from the reference host: vertex UBOs set
 * 1, fragment textures set 2, fragment UBOs set 3) and the push-constant
 * block re-homed as a std140 UBO. The Vulkan runner consumes exactly
 * these set/binding assignments.
 *
 * @param shaderPath Absolute path of the .slang file (include base + diagnostics).
 * @param knownTextureNames Names the shaders may reference besides the builtins:
 *   preset lookup texture names and pass aliases. Matched case-insensitively:
 *   entries may be spelled in any case and mixed-case preset aliases (e.g.
 *   "alias1 = Filtered") resolve without callers pre-lowering them.
 * @param passIndex Index of this pass in the chain (for PassOutput# causality).
 * @param passCount Number of passes in the chain.
 */
SLANGVK_API bool TranslateSlang(const std::string &shaderPath, const std::unordered_set<std::string> &knownTextureNames,
    int passIndex, int passCount, SlangTranslation &out,
    const std::vector<ParamInfo> &presetParameters = {});

/**
 * @brief Collects every `#pragma name` reachable from one pass shader
 * (top-level file plus its #include graph, with the same include-once
 * semantics TranslateSlang applies).
 *
 * Hosts feed the result into the knownTextureNames set they pass to
 * TranslateSlang: besides preset aliases, a pass is also named by its
 * shader's `#pragma name`, and later passes may tap that pass's output
 * through the name — plain (this frame) or with a "Feedback" suffix (the
 * pass's previous-frame output; SPEC 5.3, e.g. crt-yah's phosphor pass
 * taps its own "PhosphorPassFeedback" for persistence). The Vulkan chain
 * resolves such bindings at render time; this scan is what lets the
 * translator admit the declarations instead of failing with "unknown
 * texture reference".
 *
 * @param shaderPath Absolute path of the pass's .slang file.
 * @param out Set to insert collected names into (raw declared spellings;
 *   TranslateSlang accepts any case).
 * @return false when the source cannot be read or its include graph fails
 *   the include policy. Soft-fail: callers should ignore it and let the
 *   real translation surface the load error for that pass.
 */
SLANGVK_API bool CollectSlangPragmaNames(const std::string &shaderPath, std::unordered_set<std::string> &out);

} // namespace preset
} // namespace slv
