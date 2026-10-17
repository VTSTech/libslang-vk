// libslang-vk M0 unit test — .slangp parser (real library code).
//
// Pins the semantics the reference host's audit campaign established:
//   - P1 regression: a bare `scale` key must not reset a previously declared
//     `scale_type` (RetroArch parses preset keys order-independently).
//   - #reference merge: parents merge in order (last non-empty chain wins),
//     textures/parameters accumulate, and the referencing file's own keys
//     apply on top.
//   - ScanPresets: depth-bounded, unfiltered; ListPresetDir is the
//     settings-browser contract that excludes hidden + cache folders.
//   - The preset-pack sandbox (IsPathInsideShaderRoot) rejects escapes.
//   - SetShaderRootDir() retargets resolution (libslang-vk addition).

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "slangvk/core/preset_parser.hpp"
#include "slangvk/core/types.hpp"

using namespace slv::preset;

namespace {

int failures = 0;

void Check(bool condition, const std::string &label)
{
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition)
                ++failures;
}

std::filesystem::path MakeTempRoot(const char *name)
{
        const std::filesystem::path root = std::filesystem::temp_directory_path() / name;
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
        return root;
}

void WriteText(const std::filesystem::path &path, const std::string &content)
{
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << content;
}

std::string NormalizeSlashesForTest(const std::filesystem::path &path)
{
        std::string s = path.string();
        for (char &c : s) {
                if (c == '\\')
                        c = '/';
        }
        return s;
}

void RunScaleOrderCase(const char *label, const char *typeLine, const char *scaleLine)
{
        const std::filesystem::path root = MakeTempRoot("libslang_vk_test_scale_order");
        SetShaderRootDir(root.string());
        WriteText(root / "case.slangp", std::string("shader0=\"pass.slang\"\n") + typeLine + scaleLine);
        WriteText(root / "pass.slang", "#pragma name pass\n");
        Preset preset;
        std::string error;
        const bool ok = ParsePreset((root / "case.slangp").string(), preset, error);
        Check(ok, std::string(label) + " parses");
        if (!ok) {
                std::printf("  error: %s\n", error.c_str());
                return;
        }
        Check(preset.passes.size() == 1, std::string(label) + " has one pass");
        Check(preset.passes[0].scaleX.type == ScaleType::Viewport && preset.passes[0].scaleY.type == ScaleType::Viewport,
            std::string(label) + ": scale_type survives (P1 order-independence)");
        Check(preset.passes[0].scaleX.scale == 2.0F && preset.passes[0].scaleY.scale == 2.0F,
            std::string(label) + ": scale value applied");
        std::filesystem::remove_all(root);
}

} // namespace

int main()
{
        // --- P1 regression: both key orders must converge on the same result.
        // "Bare scale" in the audit finding means the axis-less `scaleN` key as
        // opposed to scale_x/scale_y — an unindexed key never targets a pass.
        RunScaleOrderCase("type-then-scale", "scale_type0=\"viewport\"\n", "scale0=\"2.0\"\n");
        RunScaleOrderCase("scale-then-type", "scale0=\"2.0\"\n", "scale_type0=\"viewport\"\n");

        // --- #reference merge ---
        {
                const std::filesystem::path root = MakeTempRoot("libslang_vk_test_reference");
                SetShaderRootDir(root.string());
                WriteText(root / "base.slangp",
                    "shader0=\"base.slang\"\n"
                    "filter_linear0=\"true\"\n"
                    "scale_type0=\"viewport\"\n");
                WriteText(root / "child.slangp",
                    "#reference \"base.slangp\"\n"
                    "shader0=\"child.slang\"\n");
                WriteText(root / "base.slang", "#pragma name base\n");
                WriteText(root / "child.slang", "#pragma name child\n");
                Preset preset;
                std::string error;
                const bool ok = ParsePreset((root / "child.slangp").string(), preset, error);
                Check(ok, "#reference child parses");
                if (!ok) {
                        std::printf("  error: %s\n", error.c_str());
                        return failures > 0 ? 1 : 0;
                }
                Check(preset.passes.size() == 1, "#reference merged chain has one pass");
                Check(preset.passes[0].shaderPath == "child.slang",
                    "#reference: referencing file's own keys apply on top");
                Check(preset.passes[0].filter == FilterMode::Linear, "#reference: inherited filter_linear0");
                Check(preset.passes[0].scaleX.type == ScaleType::Viewport, "#reference: inherited scale_type0");
                std::filesystem::remove_all(root);
        }

        // --- Texture declaration order (M3): the materialized list keeps the
        // declared sequence so User# positional access matches spec 3.5
        // (User0 = first declared, NOT the alphabetically sorted first) ---
        {
                const std::filesystem::path root = MakeTempRoot("libslang_vk_test_tex_order");
                SetShaderRootDir(root.string());
                WriteText(root / "order.slangp",
                    "shader0=\"p.slang\"\n"
                    "textures = \"zeta;alpha\"\n"
                    "zeta = \"z.png\"\n"
                    "alpha = \"a.png\"\n");
                WriteText(root / "p.slang", "#pragma name p\n");
                Preset preset;
                std::string error;
                const bool ok = ParsePreset((root / "order.slangp").string(), preset, error);
                Check(ok, "texture list parses");
                if (!ok) {
                        std::printf("  error: %s\n", error.c_str());
                } else {
                        Check(preset.textures.size() == 2, "declared list materializes both textures");
                        if (preset.textures.size() == 2) {
                                Check(preset.textures[0].name == "zeta",
                                    "declaration order: zeta first (User0 binds it)");
                                Check(preset.textures[1].name == "alpha", "declaration order: alpha second (User1)");
                        }
                }
                std::filesystem::remove_all(root);
        }

        // --- Recursive scan: subfolders listed, hidden + cache skipped ---
        {
                const std::filesystem::path root = MakeTempRoot("libslang_vk_test_scan");
                WriteText(root / "a.slangp", "shader0=\"p.slang\"\n");
                WriteText(root / "sub" / "b.slangp", "shader0=\"p.slang\"\n");
                WriteText(root / "sub" / "deep" / "c.slangp", "shader0=\"p.slang\"\n");
                WriteText(root / "sub" / "deep" / "deeper" / "d.slangp", "shader0=\"p.slang\"\n");
                WriteText(root / ".hidden" / "e.slangp", "shader0=\"p.slang\"\n");
                WriteText(root / "cache" / "f.slangp", "shader0=\"p.slang\"\n");
                const std::vector<PresetEntry> entries = ScanPresets(root.string());
                bool foundB = false;
                for (const PresetEntry &entry : entries) {
                        if (entry.relativePath == "sub/b.slangp")
                                foundB = true;
                        // Path separators must be normalized to forward slashes.
                        Check(entry.relativePath.find('\\') == std::string::npos,
                            "scan: forward-slash relative paths");
                }
                Check(entries.size() == 6,
                    "scan: depth-bounded, unfiltered (hidden + cache included by ScanPresets)");
                Check(foundB, "scan: sub/b.slangp present");

                // ListPresetDir: the settings-browser contract - hidden dirs
                // and the cache folder are not listed.
                const PresetDirListing listing = ListPresetDir(root.string(), "");
                Check(listing.presets.size() == 1 && listing.presets[0].relativePath == "a.slangp",
                    "ListPresetDir: root presets only");
                Check(listing.subdirs.size() == 1 && listing.subdirs[0] == "sub",
                    "ListPresetDir: hidden + cache subdirs excluded");
                const PresetDirListing deepListing = ListPresetDir(root.string(), "sub");
                Check(deepListing.presets.size() == 1 && deepListing.presets[0].relativePath == "sub/b.slangp",
                    "ListPresetDir: nested preset carries root-relative path");
                std::filesystem::remove_all(root);
        }

        // --- Preset-pack sandbox ---
        {
                const std::filesystem::path root = MakeTempRoot("libslang_vk_test_sandbox");
                SetShaderRootDir(root.string());
                const std::filesystem::path inside = root / "pack";
                std::filesystem::create_directories(inside);
                Check(IsPathInsideShaderRoot(inside), "sandbox: root subtree accepted");
                Check(IsPathInsideShaderRoot(root / "pack" / "shaders-ng"), "sandbox: nonexistent tail tolerated");
                Check(!IsPathInsideShaderRoot(root.parent_path() / "elsewhere"), "sandbox: sibling rejected");
                // Same parent, similar prefix: ".../shaders-ng" must not pass ".../shaders".
                const std::filesystem::path tricky = root / "pack-extra";
                std::filesystem::create_directories(tricky);
                // Explicit .string(): on MSVC path::string_type is std::wstring,
                // so the implicit conversion GCC accepts does not exist.
                SetShaderRootDir((root / "pack").string());
                Check(!IsPathInsideShaderRoot(tricky), "sandbox: prefix lookalike rejected");
                SetShaderRootDir(root.string());
                std::filesystem::remove_all(root);
        }

        // --- SetShaderRootDir retargets ResolvePresetPath ---
        {
                const std::filesystem::path root = MakeTempRoot("libslang_vk_test_root");
                WriteText(root / "x" / "y.slangp", "shader0=\"p.slang\"\n");
                SetShaderRootDir(root.string());
                Check(ResolvePresetPath("x/y.slangp") == NormalizeSlashesForTest(root / "x" / "y.slangp"),
                    "SetShaderRootDir: resolution follows the new root");
                Check(ResolvePresetPath("missing.slangp").empty(), "SetShaderRootDir: missing file resolves empty");
                std::filesystem::remove_all(root);
        }

        std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
        return failures == 0 ? 0 : 1;
}
