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

        for (const std::string &warning : translation.warnings)
                std::printf("  warning: %s\n", warning.c_str());
        std::filesystem::remove_all(root);

        std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
        return failures == 0 ? 0 : 1;
}
