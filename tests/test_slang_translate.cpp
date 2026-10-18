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

        for (const std::string &warning : translation.warnings)
                std::printf("  warning: %s\n", warning.c_str());
        std::filesystem::remove_all(root);

        std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
        return failures == 0 ? 0 : 1;
}
