// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// slang_compile.cpp — glslang frontend glue.
//
// Ported from the reference host (VTSTech/DevilutionX,
// Source/engine/render/gpu/preset/slang_compile.cpp @ origin/master 96af9b5fc).
// Divergences from the original (see PROVENANCE.md):
//   - USE_GPU_SCALER / USE_SDL1 build guards removed (the library always
//     builds this translation unit).
//   - The precompiled-sidecar fast path and the content-addressed disk cache
//     (picosha2, cache version salt, atomic temp-file writer) are removed by
//     design: v1 compiles with glslang on every load (plan §2 non-goal).
//   - fork logging replaced by the internal log callback shim (compat.hpp).

#include "slangvk/core/slang_compile.hpp"

#include <cstdint>
#include <cstring>
#include <mutex>

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/build_info.h>

#include "compat.hpp"
#include "sha256.hpp"

namespace slv {
namespace preset {

/** @brief Serializes glslang frontend entry points across translation units
 * (decision D-3, freeze review F-1): TShader parse/preprocess/link are not
 * thread-safe — the lifecycle singleton below guards Initialize/FinalizeProcess
 * only. Defined here with external linkage; slang_translate.cpp forward-declares
 * it for its preprocessing oracle (same pattern as EnsureGlslangRuntime). */
std::mutex &GlslangFrontendMutex()
{
        static std::mutex mutex;
        return mutex;
}

namespace {

/** @brief Process-wide glslang lifecycle guard. */
class GlslangRuntime {
public:
        static GlslangRuntime &Instance()
        {
                static GlslangRuntime runtime;
                return runtime;
        }

        GlslangRuntime()
        {
                glslang::InitializeProcess();
        }
        ~GlslangRuntime()
        {
                glslang::FinalizeProcess();
        }
        GlslangRuntime(const GlslangRuntime &) = delete;
        GlslangRuntime &operator=(const GlslangRuntime &) = delete;
};

// The glslang version as a string, for the content key.
#define SLV_STRINGIFY_(x) #x
#define SLV_STRINGIFY(x) SLV_STRINGIFY_(x)
#define SLV_GLSLANG_VERSION_STRING SLV_STRINGIFY(GLSLANG_VERSION_MAJOR) "." \
        SLV_STRINGIFY(GLSLANG_VERSION_MINOR) "." SLV_STRINGIFY(GLSLANG_VERSION_PATCH)

// The compile environment, in one place: the content key below must change
// whenever this does, so both read the same constants.
constexpr int kGlslVersion = 450;
constexpr int kGlslVersionClient = 100;
constexpr auto kTargetClient = glslang::EShTargetVulkan_1_0;
constexpr auto kTargetSpv = glslang::EShTargetSpv_1_0;

bool CompileStageToSpv(EShLanguage stage, const std::string &source, std::vector<uint8_t> &out, std::string &error)
{
        GlslangRuntime::Instance();
        std::scoped_lock glslangLock(GlslangFrontendMutex());

        glslang::TShader shader(stage);
        const char *sourcePtr = source.c_str();
        shader.setStrings(&sourcePtr, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, kGlslVersionClient);
        shader.setEnvClient(glslang::EShClientVulkan, kTargetClient);
        shader.setEnvTarget(glslang::EShTargetSpv, kTargetSpv);
        const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(GetDefaultResources(), kGlslVersion, false, messages)) {
                error = StrCat("glslang parse failed (", stage == EShLangVertex ? "vertex" : "fragment", "): ",
                    shader.getInfoLog());
                return false;
        }
        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(EShMsgDefault)) {
                error = StrCat("glslang link failed: ", program.getInfoLog());
                return false;
        }
        std::vector<unsigned int> spirv;
        glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
        out.resize(spirv.size() * sizeof(uint32_t));
        std::memcpy(out.data(), spirv.data(), out.size());
        return true;
}

} // namespace

void EnsureGlslangRuntime()
{
        // Shared process-wide glslang init guard: CompileStageToSpv and the
        // translate-time preprocessing oracle in slang_translate.cpp both run
        // TShader frontends and hit this same singleton.
        GlslangRuntime::Instance();
}

auto ComputePassCacheKey(const SlangTranslation &translation) -> std::string
{
        // The material is everything that determines the compiled bytes:
        // the GLSL handed to glslang (which already folds in the include
        // resolution, the preset's declared texture names and the library's
        // translation semantics) and the compiler's version and environment.
        // Hashing the compiler's input is what makes the key correct by
        // construction — it changes exactly when the output would.
        std::string material;
        material.reserve(translation.vertex.glsl.size() + translation.fragment.glsl.size() + 128);
        material += "libslang-vk-spirv-cache-1";
        material += '\0';
        material += "glslang " SLV_GLSLANG_VERSION_STRING;
        material += '\0';
        material += "target ";
        material += std::to_string(static_cast<int>(kTargetClient));
        material += ' ';
        material += std::to_string(static_cast<int>(kTargetSpv));
        material += ' ';
        material += std::to_string(kGlslVersion);
        material += '\0';
        material += translation.vertex.glsl;
        material += '\0';
        material += translation.fragment.glsl;
        return core::Sha256Hex(material);
}

bool CompilePass(const SlangTranslation &translation, PassSpirv &out, std::string &error)
{
        out.vertex.clear();
        out.fragment.clear();

        if (!translation.vertex.present || !translation.fragment.present) {
                error = "slang: pass is missing a required stage";
                return false;
        }

        if (!CompileStageToSpv(EShLangVertex, translation.vertex.glsl, out.vertex, error))
                return false;
        if (!CompileStageToSpv(EShLangFragment, translation.fragment.glsl, out.fragment, error))
                return false;
        return true;
}

} // namespace preset
} // namespace slv
