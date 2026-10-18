#pragma once

// libslang-vk shader_demo — preset discovery helpers.
//
// CLI paths become "sources": a .slangp file is a single-entry source rooted
// at its parent directory (the preset_info layout: preset next to a shaders/
// folder); a directory is scanned recursively through the core ScanPresets()
// browser, so the same discovery the settings UI uses feeds the demo menu.
//
// Each record carries the shader root that must be installed with
// SetShaderRootDir() before the preset loads — the SEC-01 sandbox resolves
// preset-internal references (shader passes, LUTs, #reference) against it,
// and per-source roots let one menu mix the bundled demos, the test fixture
// trees and a full slang-shaders checkout.

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <slangvk/slangvk.hpp>

namespace demo {

struct PresetRecord {
        std::string root;         ///< Shader root to SetShaderRootDir() before loading.
        std::string relativePath; ///< Path of the .slangp relative to the root.
        std::string displayName;  ///< Prettified name for the menu.

        std::string AbsolutePath() const
        {
                return (std::filesystem::path(root) / std::filesystem::path(relativePath)).generic_string();
        }
};

/**
 * @brief Expands CLI paths (files or directories) into menu records.
 *
 * Directories scan with depth 7: stock slang-shaders keeps presets up to 7
 * levels below the pack root (docs/SPEC.md corpus notes — Mega_Bezel alone
 * is a deep forest), so the default browser cap of 3 would hide most of the
 * pack. Scans are sorted per root, sources stay in CLI order.
 *
 * @return false when a path is neither a file nor a directory (err explains).
 */
inline bool CollectPresets(const std::vector<std::string> &paths, std::vector<PresetRecord> &out, std::string &err)
{
        namespace fs = std::filesystem;
        for (const std::string &rawPath : paths) {
                const fs::path p = fs::absolute(fs::path(rawPath));
                std::error_code ec;
                if (fs::is_regular_file(p, ec)) {
                        if (p.extension() != ".slangp") {
                                err = "not a .slangp preset: " + rawPath;
                                return false;
                        }
                        PresetRecord rec;
                        rec.root = p.parent_path().generic_string();
                        rec.relativePath = p.filename().generic_string();
                        rec.displayName = p.stem().generic_string();
                        out.push_back(std::move(rec));
                        continue;
                }
                if (fs::is_directory(p, ec)) {
                        const std::string root = p.generic_string();
                        for (const slv::preset::PresetEntry &entry : slv::preset::ScanPresets(root, 7)) {
                                PresetRecord rec;
                                rec.root = root;
                                rec.relativePath = entry.relativePath;
                                rec.displayName = entry.displayName;
                                out.push_back(std::move(rec));
                        }
                        continue;
                }
                err = "not a file or directory: " + rawPath;
                return false;
        }
        return true;
}

} // namespace demo
