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

        // --- `histories` key (miss list #4): parse + clamp pinned; the ring
        // itself derives depth from the max referenced OriginalHistoryN (SPEC
        // key table: the key is informational, extra declared slots are not
        // allocated) ---
        {
                const std::filesystem::path root = MakeTempRoot("libslang_vk_test_histories");
                SetShaderRootDir(root.string());
                WriteText(root / "p.slang", "#pragma name p\n");
                struct Case {
                        const char *value;
                        int want;
                        const char *label;
                };
                const Case cases[] = {
                        { "4", 4, "histories parses" },
                        { "0", 1, "histories clamps low to 1" },
                        { "99", 8, "histories clamps high to 8" },
                        { "-3", 1, "histories negative clamps to 1" },
                        { "\"2\"", 2, "histories quoted value parses" },
                };
                for (const Case &c : cases) {
                        WriteText(root / "h.slangp", std::string("shader0=\"p.slang\"\n") + "histories = " + c.value + "\n");
                        Preset preset;
                        std::string error;
                        if (ParsePreset((root / "h.slangp").string(), preset, error))
                                Check(preset.histories == c.want, std::string(c.label));
                        else
                                Check(false, std::string(c.label) + " (parse failed: " + error + ")");
                }
                // #reference adoption: a child that keeps the default 1 picks up
                // the parent's depth; an explicit child value wins.
                WriteText(root / "base.slangp", "shader0=\"p.slang\"\nhistories = \"3\"\n");
                WriteText(root / "child_default.slangp", "#reference \"base.slangp\"\nshader0=\"p.slang\"\n");
                WriteText(root / "child_explicit.slangp",
                    "#reference \"base.slangp\"\nshader0=\"p.slang\"\nhistories = \"5\"\n");
                Preset preset;
                std::string error;
                Check(ParsePreset((root / "child_default.slangp").string(), preset, error)
                        && preset.histories == 3,
                    "#reference: child default adopts parent histories");
                Check(ParsePreset((root / "child_explicit.slangp").string(), preset, error)
                        && preset.histories == 5,
                    "#reference: explicit child histories wins");
                std::filesystem::remove_all(root);
        }

        // --- `max_fbo_width` / `max_fbo_height` keys (miss list #3): parse,
        // clamp, and #reference adoption. The runner-side clamp is exercised
        // by the runner_headless max_fbo case. ---
        {
                const std::filesystem::path root = MakeTempRoot("libslang_vk_test_maxfbo");
                SetShaderRootDir(root.string());
                WriteText(root / "p.slang", "#pragma name p\n");
                WriteText(root / "clamped.slangp",
                    "shader0=\"p.slang\"\n"
                    "max_fbo_width=\"64\"\n"
                    "max_fbo_height=\"48\"\n");
                Preset preset;
                std::string error;
                Check(ParsePreset((root / "clamped.slangp").string(), preset, error), "max_fbo preset parses");
                Check(preset.maxFboWidth == 64 && preset.maxFboHeight == 48, "max_fbo keys parse into clamps");
                WriteText(root / "unset.slangp", "shader0=\"p.slang\"\n");
                Check(ParsePreset((root / "unset.slangp").string(), preset, error)
                        && preset.maxFboWidth == 0 && preset.maxFboHeight == 0,
                    "max_fbo default (unset) is 0/0 (no clamp)");
                WriteText(root / "hostile.slangp",
                    "shader0=\"p.slang\"\nmax_fbo_width=\"99999999\"\nmax_fbo_height=\"-5\"\n");
                Check(ParsePreset((root / "hostile.slangp").string(), preset, error)
                        && preset.maxFboWidth == 16384 && preset.maxFboHeight == 0,
                    "max_fbo hostile values clamp to 16384 / 0");
                // #reference: child without keys adopts the parent's clamps.
                WriteText(root / "base.slangp",
                    "shader0=\"p.slang\"\nmax_fbo_width=\"128\"\nmax_fbo_height=\"96\"\n");
                WriteText(root / "child.slangp", "#reference \"base.slangp\"\nshader0=\"p.slang\"\n");
                Check(ParsePreset((root / "child.slangp").string(), preset, error)
                        && preset.maxFboWidth == 128 && preset.maxFboHeight == 96,
                    "#reference: child adopts parent max_fbo clamps");
                std::filesystem::remove_all(root);
        }

        // --- Pass-count cap (ROB-02 boundary, raised 2026-10-18 for the
        // crt-adamant family: field load failed at the old cap of 32 with
        // "preset pass index 93 exceeds the limit of 32"). Positive: 40
        // sparse, out-of-order shaderN keys must parse (the lazy resize
        // path). Negative: the exact new cap must still reject, and an
        // in-int-range absurd index must keep failing (ROB-02). Shader files
        // need not exist — parse stores paths only. ---
        {
                const std::filesystem::path root = MakeTempRoot("libslang_vk_test_passcap");
                SetShaderRootDir(root.string());
                // Out of order: shader39 first, then the rest — exercises the
                // sparse resize instead of an appends-only path.
                std::string sparse = "shader39=\"p.slang\"\n";
                for (int i = 38; i >= 0; --i)
                        sparse += "shader" + std::to_string(i) + "=\"p.slang\"\n";
                WriteText(root / "sparse.slangp", sparse);
                Preset preset;
                std::string error;
                const bool ok = ParsePreset((root / "sparse.slangp").string(), preset, error);
                Check(ok, "40 sparse out-of-order passes parse (cap raised to 128)");
                if (!ok) {
                        std::printf("  error: %s\n", error.c_str());
                } else {
                        Check(preset.passes.size() == 40, "sparse keys materialize 40 passes");
                        Check(preset.passes[0].shaderPath == "p.slang"
                                && preset.passes[39].shaderPath == "p.slang",
                            "sparse resize stores pass paths");
                }
                // Boundary: index == cap rejects with the existing message shape.
                WriteText(root / "atcap.slangp", "shader0=\"p.slang\"\nshader128=\"p.slang\"\n");
                error.clear();
                Check(!ParsePreset((root / "atcap.slangp").string(), preset, error),
                    "shader128 (= cap) rejected");
                Check(error.find("exceeds the limit of 128") != std::string::npos,
                    "cap error keeps the documented message shape");
                // ROB-02 regression: in-int-range absurd index must not allocate.
                WriteText(root / "absurd.slangp", "shader0=\"p.slang\"\nshader2147483647=\"p.slang\"\n");
                error.clear();
                Check(!ParsePreset((root / "absurd.slangp").string(), preset, error),
                    "shader2147483647 rejected (ROB-02 intact)");
                Check(error.find("preset pass index 2147483647 exceeds the limit") != std::string::npos,
                    "ROB-02 error names the absurd index");
                std::filesystem::remove_all(root);
        }

        std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
        return failures == 0 ? 0 : 1;
}
