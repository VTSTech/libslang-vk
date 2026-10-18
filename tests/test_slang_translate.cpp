// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk M0 unit test — .slang translation (real library code).
//
// Exercises the largest core TU end-to-end on a minimal slang source:
// stage splitting, #pragma parameter metadata capture, builtin-texture
// binding (Source → fragment set 2, dense bindings) and parameter UBO
// reflection.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

#include "slangvk/core/slang_translate.hpp"

using namespace slv::preset;

namespace {

int failures = 0;

void Check(bool condition, const std::string &label)
{
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition)
                ++failures;
}

constexpr const char *kTrivialSlang =
    "#pragma name trivial\n"
    "#pragma parameter TEST_AMT \"Test amount\" 0.5 0.0 1.0 0.01\n"
    "#pragma stage vertex\n"
    "uniform float TEST_AMT;\n"
    "layout(location = 0) in vec2 VertexCoord;\n"
    "layout(location = 0) out vec2 vTexCoord;\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = vec4(VertexCoord, 0.0, 1.0);\n"
    "    vTexCoord = VertexCoord;\n"
    "}\n"
    "#pragma stage fragment\n"
    "uniform float TEST_AMT;\n"
    "uniform sampler2D Source;\n"
    "in vec2 vTexCoord;\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "void main(void)\n"
    "{\n"
    "    fragColor = texture(Source, vTexCoord) * TEST_AMT;\n"
    "}\n";

// Field regression (crt-beans-s-video pass 3): a pass samples an EARLIER
// pass's output through its preset alias, and the alias is spelled with
// mixed case ("alias1 = Filtered"). Whether the caller passes the name in
// preset case (preset_info) or pre-lowered (the runtime host), the rewrite
// must bind it instead of hard-failing with "unknown texture reference".
constexpr const char *kAliasTapSlang =
    "#pragma name alias_tap\n"
    "#pragma stage vertex\n"
    "layout(location = 0) in vec2 VertexCoord;\n"
    "layout(location = 0) out vec2 vTexCoord;\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = vec4(VertexCoord, 0.0, 1.0);\n"
    "    vTexCoord = VertexCoord;\n"
    "}\n"
    "#pragma stage fragment\n"
    "uniform sampler2D Filtered;\n"
    "in vec2 vTexCoord;\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "void main(void)\n"
    "{\n"
    "    fragColor = texture(Filtered, vTexCoord);\n"
    "}\n";

// Field regression (crt-yah phosphor persistence): a pass names itself via
// `#pragma name` (the preset carries no aliasN) and taps its own
// previous-frame output through the Feedback-suffixed form. The pragma name
// must resolve like an alias: CollectSlangPragmaNames feeds the universe,
// the rewrite strips the suffix for the membership check while keeping the
// full name on the emitted binding, and the chain binds the previous-frame
// texture at render time.
constexpr const char *kFeedbackTapSlang =
    "#pragma name PhosphorPass\n"
    "#pragma stage vertex\n"
    "layout(location = 0) in vec2 VertexCoord;\n"
    "layout(location = 0) out vec2 vTexCoord;\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = vec4(VertexCoord, 0.0, 1.0);\n"
    "    vTexCoord = VertexCoord;\n"
    "}\n"
    "#pragma stage fragment\n"
    "uniform sampler2D PhosphorPassFeedback;\n"
    "in vec2 vTexCoord;\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "void main(void)\n"
    "{\n"
    "    fragColor = texture(PhosphorPassFeedback, vTexCoord);\n"
    "}\n";

// Field regression (Mega Bezel NO-REFLECT variants): samplers declared and/or
// sampled ONLY inside conditional blocks the GLSL preprocessor discards for
// this variant ("#ifdef IS_NO_REFLECT_PRESET" wraps the ReflectionPass
// declaration in combine-passes.inc, "#ifndef IS_NO_REFLECT_PRESET" wraps its
// sampling). The raw text contains sampling calls, but the compile-time truth
// — glslang preprocessing the exact text with no host defines — has no live
// use, so the declarations must drop with a warning instead of hard-failing
// the load. Covers three shapes: dead declaration + dead sampling, live
// declaration + dead sampling, and a dead #if-arithmetic gate.
constexpr const char *kDeadCodeSamplerSlang =
    "#pragma stage vertex\n"
    "layout(location = 0) in vec2 VertexCoord;\n"
    "layout(location = 0) out vec2 vTexCoord;\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = vec4(VertexCoord, 0.0, 1.0);\n"
    "    vTexCoord = VertexCoord;\n"
    "}\n"
    "#pragma stage fragment\n"
    "#define FEATURE_OFF 0\n"
    "#ifdef FEATURE_UNDEFINED\n"
    "uniform sampler2D DeadPass;\n"
    "#endif\n"
    "#if FEATURE_OFF\n"
    "uniform sampler2D OffPass;\n"
    "#endif\n"
    "uniform sampler2D AlsoDeadPass;\n"
    "in vec2 vTexCoord;\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "void main(void)\n"
    "{\n"
    "    fragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "#ifdef FEATURE_UNDEFINED\n"
    "    fragColor = texture(DeadPass, vTexCoord);\n"
    "    fragColor = texture(AlsoDeadPass, vTexCoord);\n"
    "#endif\n"
    "#if FEATURE_OFF\n"
    "    fragColor = texture(OffPass, vTexCoord);\n"
    "#endif\n"
    "}\n";

// Companion to kDeadCodeSamplerSlang: the same conditional shape with a LIVE
// gate. The sample survives preprocessing, so the missing chain dependency is
// real and the load must keep hard-failing.
constexpr const char *kDeadGateLiveSamplerSlang =
    "#pragma stage vertex\n"
    "layout(location = 0) in vec2 VertexCoord;\n"
    "layout(location = 0) out vec2 vTexCoord;\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = vec4(VertexCoord, 0.0, 1.0);\n"
    "    vTexCoord = VertexCoord;\n"
    "}\n"
    "#pragma stage fragment\n"
    "#define FEATURE_ON 1\n"
    "#if FEATURE_ON\n"
    "uniform sampler2D LiveGatePass;\n"
    "#endif\n"
    "in vec2 vTexCoord;\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "void main(void)\n"
    "{\n"
    "#if FEATURE_ON\n"
    "    fragColor = texture(LiveGatePass, vTexCoord);\n"
    "#else\n"
    "    fragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "#endif\n"
    "}\n";

// Session 9 field regression (upstream corpus test/format.slangp): integer
// samplers. The upstream preset round-trips through an R32_UINT FBO — pass 0
// packs RGBA into a uint (uint FragColor), pass 1 reads it back via
// usampler2D + texelFetch + unpackUnorm4x8, with the sampler carrying an
// EXPLICIT source-level layout. RewriteSamplers used to skip integer samplers
// entirely, so that explicit set/binding reached glslang while the chain's
// canonical pipeline layout provides an empty set 0: llvmpipe segfaulted at
// draw and the AMD Windows driver returned VK_ERROR_UNKNOWN at pipeline
// creation (the fork field log's "test/format fails to load"). The rewrite
// must re-home integer samplers into the texture set and preserve their type.
constexpr const char *kUintSamplerSlang =
    "#pragma stage vertex\n"
    "layout(location = 0) in vec2 VertexCoord;\n"
    "layout(location = 0) out vec2 vTexCoord;\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = vec4(VertexCoord, 0.0, 1.0);\n"
    "    vTexCoord = VertexCoord;\n"
    "}\n"
    "#pragma stage fragment\n"
    "layout(location = 0) in vec2 vTexCoord;\n"
    "layout(location = 0) out uint fragColor;\n"
    "layout(set = 0, binding = 1) uniform usampler2D Source;\n"
    "void main(void)\n"
    "{\n"
    "    fragColor = packUnorm4x8(texture(Source, vTexCoord));\n"
    "}\n";

constexpr const char *kIsamplerSlang =
    "#pragma stage vertex\n"
    "layout(location = 0) in vec2 VertexCoord;\n"
    "layout(location = 0) out vec2 vTexCoord;\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = vec4(VertexCoord, 0.0, 1.0);\n"
    "    vTexCoord = VertexCoord;\n"
    "}\n"
    "#pragma stage fragment\n"
    "layout(location = 0) in vec2 vTexCoord;\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "layout(set = 0, binding = 3) uniform isampler2D Tint;\n"
    "void main(void)\n"
    "{\n"
    "    fragColor = vec4(float(texelFetch(Tint, ivec2(vTexCoord * vec2(64.0)), 0).r));\n"
    "}\n";

// Integer-sampler orphan parity: a never-sampled usampler2D with an unknown
// name must drop with a warning exactly like the float path (the scan index
// feeds the same dead/orphan resolution).
constexpr const char *kDeadUintSamplerSlang =
    "#pragma stage vertex\n"
    "layout(location = 0) in vec2 VertexCoord;\n"
    "layout(location = 0) out vec2 vTexCoord;\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = vec4(VertexCoord, 0.0, 1.0);\n"
    "    vTexCoord = VertexCoord;\n"
    "}\n"
    "#pragma stage fragment\n"
    "uniform usampler2D DeadUintPass;\n"
    "layout(location = 0) in vec2 vTexCoord;\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "void main(void)\n"
    "{\n"
    "    fragColor = vec4(0.25, 0.5, 0.75, 1.0);\n"
    "}\n";

} // namespace

int main()
{
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "libslang_vk_test_translate";
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
        const std::filesystem::path shader = root / "trivial.slang";
        {
                std::ofstream stream(shader, std::ios::binary | std::ios::trunc);
                stream << kTrivialSlang;
        }
        const std::filesystem::path aliasShader = root / "alias_tap.slang";
        {
                std::ofstream stream(aliasShader, std::ios::binary | std::ios::trunc);
                stream << kAliasTapSlang;
        }
        const std::filesystem::path feedbackShader = root / "feedback_tap.slang";
        {
                std::ofstream stream(feedbackShader, std::ios::binary | std::ios::trunc);
                stream << kFeedbackTapSlang;
        }

        SlangTranslation translation;
        const bool ok = TranslateSlang(shader.string(), /*knownTextureNames=*/{}, /*passIndex=*/0, /*passCount=*/1,
            translation);
        Check(ok, "trivial slang translates");
        if (!ok) {
                std::printf("  error: %s\n", translation.error.c_str());
                return 1;
        }
        Check(translation.vertex.present && translation.fragment.present, "both stages present");
        Check(translation.pragmaName == "trivial", "#pragma name captured");
        Check(translation.parameters.size() == 1 && translation.parameters[0].name == "TEST_AMT",
            "#pragma parameter captured");
        const bool hasSource = [&] {
                for (const TextureRef &ref : translation.fragment.textures)
                        if (ref.name == "Source")
                                return true;
                return false;
        }();
        Check(hasSource, "fragment: Source texture bound");
        Check(translation.fragment.glsl.find("set = 2") != std::string::npos,
            "fragment: sampler rewritten into the texture set (set 2)");

        // Alias-name case handling: the name set may hold only the preset
        // spelling, only a lower-cased spelling, or nothing at all (then the
        // sampled unknown name must still hard-fail).
        {
                SlangTranslation aliasTap;
                const bool presetCase = TranslateSlang(aliasShader.string(), {"Filtered"}, 1, 2, aliasTap);
                Check(presetCase, "alias tap: mixed-case name set translates");
                bool bound = false;
                for (const TextureRef &ref : aliasTap.fragment.textures)
                        if (ref.name == "Filtered")
                                bound = true;
                Check(presetCase && bound, "alias tap: Filtered bound in the texture set");

                SlangTranslation lowerTap;
                const bool lowerCase = TranslateSlang(aliasShader.string(), {"filtered"}, 1, 2, lowerTap);
                Check(lowerCase, "alias tap: lower-cased name set translates");

                SlangTranslation unknownTap;
                const bool unknownFails = !TranslateSlang(aliasShader.string(), {}, 1, 2, unknownTap);
                Check(unknownFails
                        && unknownTap.error.find("unknown texture reference") != std::string::npos,
                    "alias tap: unsupplied sampled name still hard-fails");
        }

        // Pragma-name universe + Feedback-suffix tap (crt-yah): the host seeds
        // the name set from CollectSlangPragmaNames exactly like Preset::load;
        // the rewrite must admit "PhosphorPassFeedback" against a universe
        // that only holds the pragma spelling, in any case.
        {
                std::unordered_set<std::string> collected;
                const bool scanned = CollectSlangPragmaNames(feedbackShader.string(), collected);
                Check(scanned && collected.count("PhosphorPass") == 1,
                    "pragma tap: CollectSlangPragmaNames finds the pass name");

                SlangTranslation feedbackTap;
                const bool seeded = TranslateSlang(feedbackShader.string(), collected, 1, 2, feedbackTap);
                bool bound = false;
                for (const TextureRef &ref : feedbackTap.fragment.textures)
                        if (ref.name == "PhosphorPassFeedback")
                                bound = true;
                Check(seeded && bound && feedbackTap.fragment.glsl.find("set = 2") != std::string::npos,
                    "pragma tap: PhosphorPassFeedback bound in the texture set");

                SlangTranslation lowerTap;
                const bool lowerCase = TranslateSlang(feedbackShader.string(), {"phosphorpass"}, 1, 2, lowerTap);
                Check(lowerCase, "pragma tap: lower-cased pragma spelling resolves the Feedback tap");

                SlangTranslation unknownTap;
                const bool unknownFails = !TranslateSlang(feedbackShader.string(), {}, 1, 2, unknownTap);
                Check(unknownFails
                        && unknownTap.error.find("unknown texture reference") != std::string::npos,
                    "pragma tap: unsupplied pragma name still hard-fails");
        }

        // Dead-conditional sampling (Mega Bezel NO-REFLECT): samplers declared
        // and/or sampled only inside conditional blocks the GLSL preprocessor
        // discards for this variant. The raw text contains sampling calls, but
        // the compile-time truth (glslang preprocesses the exact text with no
        // host defines) has no live use — the declarations must drop with a
        // warning instead of hard-failing the load.
        {
                const std::filesystem::path deadShader = root / "dead_code_sampler.slang";
                {
                        std::ofstream stream(deadShader, std::ios::binary | std::ios::trunc);
                        stream << kDeadCodeSamplerSlang;
                }
                SlangTranslation dead;
                const bool deadOk = TranslateSlang(deadShader.string(), /*knownTextureNames=*/{}, 1, 2, dead);
                Check(deadOk, "dead code: shader with only dead-sampled unknown samplers translates");
                if (!deadOk) {
                        std::printf("  error: %s\n", dead.error.c_str());
                } else {
                        Check(dead.fragment.textures.empty(),
                            "dead code: no bindings emitted for discarded samplers");
                        // The dropped DECLARATIONS must be gone. Dead sampling
                        // calls may remain in the output (they still sit in
                        // the raw text's dead conditional regions; the real
                        // compile preprocesses them away).
                        Check(dead.fragment.glsl.find("sampler2D DeadPass") == std::string::npos
                                && dead.fragment.glsl.find("sampler2D AlsoDeadPass") == std::string::npos
                                && dead.fragment.glsl.find("sampler2D OffPass") == std::string::npos,
                            "dead code: discarded sampler declarations absent from the GLSL");
                        size_t deadDrops = 0;
                        for (const std::string &warning : dead.warnings)
                                if (warning.find("dead conditional code") != std::string::npos)
                                        ++deadDrops;
                        Check(deadDrops >= 3, "dead code: all three drops reported with warnings");
                }

                // The same conditional shape with a LIVE gate stays a hard
                // failure: the sample survives preprocessing, so the missing
                // chain dependency is real.
                const std::filesystem::path gateShader = root / "dead_gate_live_sampler.slang";
                {
                        std::ofstream stream(gateShader, std::ios::binary | std::ios::trunc);
                        stream << kDeadGateLiveSamplerSlang;
                }
                SlangTranslation gated;
                const bool gateFails = !TranslateSlang(gateShader.string(), /*knownTextureNames=*/{}, 1, 2, gated);
                Check(gateFails
                        && gated.error.find("unknown texture reference 'LiveGatePass'") != std::string::npos,
                    "dead code: live-conditional unknown sampler still hard-fails");
        }

        // Integer samplers (Session 9 field regression): the rewrite must
        // re-home usampler2D/isampler2D declarations into the texture set
        // (set 2) with the integer type preserved, removing any source-level
        // set/binding so the SPIR-V matches the chain's canonical layout.
        {
                const std::filesystem::path uintShader = root / "uint_roundtrip.slang";
                {
                        std::ofstream stream(uintShader, std::ios::binary | std::ios::trunc);
                        stream << kUintSamplerSlang;
                }
                SlangTranslation uintPass;
                const bool uintOk = TranslateSlang(uintShader.string(), /*knownTextureNames=*/{}, 1, 2, uintPass);
                Check(uintOk, "uint sampler: usampler2D round-trip pass translates");
                if (!uintOk) {
                        std::printf("  error: %s\n", uintPass.error.c_str());
                } else {
                        bool bound = false;
                        for (const TextureRef &ref : uintPass.fragment.textures)
                                if (ref.name == "Source" && ref.binding == 0)
                                        bound = true;
                        Check(bound, "uint sampler: Source bound at texture-set binding 0");
                        Check(uintPass.fragment.glsl.find(
                                  "layout(set = 2, binding = 0) uniform usampler2D Source;")
                                  != std::string::npos,
                            "uint sampler: usampler2D re-homed with type preserved");
                        Check(uintPass.fragment.glsl.find("set = 0, binding = 1") == std::string::npos,
                            "uint sampler: source-level set/binding removed");
                }

                const std::filesystem::path intShader = root / "isampler_tap.slang";
                {
                        std::ofstream stream(intShader, std::ios::binary | std::ios::trunc);
                        stream << kIsamplerSlang;
                }
                SlangTranslation intPass;
                const bool intOk = TranslateSlang(intShader.string(), /*knownTextureNames=*/{"tint"}, 1, 2, intPass);
                Check(intOk, "int sampler: isampler2D tap translates against the supplied name");
                if (!intOk) {
                        std::printf("  error: %s\n", intPass.error.c_str());
                } else {
                        bool bound = false;
                        for (const TextureRef &ref : intPass.fragment.textures)
                                if (ref.name == "Tint" && ref.binding == 0)
                                        bound = true;
                        Check(bound, "int sampler: Tint bound at texture-set binding 0");
                        Check(intPass.fragment.glsl.find(
                                  "layout(set = 2, binding = 0) uniform isampler2D Tint;")
                                  != std::string::npos,
                            "int sampler: isampler2D re-homed with type preserved");
                }

                const std::filesystem::path deadUintShader = root / "dead_uint_sampler.slang";
                {
                        std::ofstream stream(deadUintShader, std::ios::binary | std::ios::trunc);
                        stream << kDeadUintSamplerSlang;
                }
                SlangTranslation deadUint;
                const bool deadUintOk = TranslateSlang(deadUintShader.string(), /*knownTextureNames=*/{}, 1, 2, deadUint);
                Check(deadUintOk, "uint sampler: never-sampled unknown usampler2D orphan translates");
                if (!deadUintOk) {
                        std::printf("  error: %s\n", deadUint.error.c_str());
                } else {
                        Check(deadUint.fragment.textures.empty(),
                            "uint sampler: orphan usampler2D emits no binding");
                        bool orphanDrop = false;
                        for (const std::string &warning : deadUint.warnings)
                                if (warning.find("DeadUintPass") != std::string::npos
                                        && warning.find("never sampled") != std::string::npos)
                                        orphanDrop = true;
                        Check(orphanDrop, "uint sampler: orphan usampler2D dropped with a warning");
                        Check(deadUint.fragment.glsl.find("usampler2D DeadUintPass") == std::string::npos,
                            "uint sampler: orphan usampler2D declaration absent from the GLSL");
                }
        }

        for (const std::string &warning : translation.warnings)
                std::printf("  warning: %s\n", warning.c_str());
        std::filesystem::remove_all(root);

        std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
        return failures == 0 ? 0 : 1;
}
