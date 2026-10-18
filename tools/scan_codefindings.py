#!/usr/bin/env python3
# Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
# AI Assisted by Super-Z (GLM-5.3-Flash)
# License: MIT

"""Provenance gate: scan source for RetroArch code fingerprints (plan section 4, risk 1).

libslang-vk is clean-room against RetroArch's GPL-3 slang implementation. The
only permitted references are the libretro slang documentation, docs/SPEC.md
(itself written from docs + corpus conventions), and black-box behavior
comparison. This gate enforces the "no RetroArch code, ever" policy
mechanically: it greps the code-bearing trees for fingerprints of
RetroArch-origin code (identifiers, file references, license headers) and
fails the run when one appears.

Docs trees (README.md, docs/, PROVENANCE.md) are exempt: naming RetroArch as
a behavioral reference is the documented, allowed use. `vendor/` is scanned —
it must stay free of RetroArch code too.

Waivers: append the marker `provenance-ok:` (with a reason) anywhere on the
offending line. The gate prints waived lines in report mode but does not fail
on them. Waivers exist for false positives (e.g. a comment explaining a
divergence by name); anything more than a couple of lines is a policy
conversation, not a waiver.

Usage:
    scan_codefindings.py [--root DIR] [--report] [--allow TOKEN]...
    exit 0 = clean, 1 = findings, 2 = usage error
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Scanned trees: everything code-bearing. Docs (README, docs/, PROVENANCE.md)
# are the sanctioned place to discuss RetroArch behavior and are exempt.
SCAN_DIRS = ("src", "include", "tests", "examples", "tools", "cmake", "vendor", ".github")
SCAN_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl",
    ".cmake", ".yml", ".yaml", ".py", ".sh", ".txt", ".slang", ".slangp", ".inc",
}
SCAN_FILES = ("CMakeLists.txt",)

# Fingerprints of RetroArch-origin code. Each entry: (compiled regex, rationale).
# The list targets the slang stack and its immediate dependencies; extend it
# whenever a new RetroArch subsystem becomes relevant. Generic tokens
# (`retroarch`, `RetroArch`) are deliberately NOT findings by themselves —
# docs-style comments naming the reference are legal — but RetroArch
# identifier conventions (rarch_ prefixes, video_shader_* APIs, settings_t) # provenance-ok: the fingerprint table must name the identifiers it scans for
# in code-bearing files are.
PATTERNS: list[tuple[re.Pattern[str], str]] = [
    # RetroArch slang parser / shader pipeline internals.
    (re.compile(r"\bvideo_shader_parse\b"), "RetroArch slang parser entry point"),
    (re.compile(r"\bvideo_shader_resolve_current\b"), "RetroArch shader resolution pass"),
    (re.compile(r"\bvideo_shader_read_file\b"), "RetroArch preset load path"),
    (re.compile(r"\bvideo_shader_wrap_mode_to_enum\b"), "RetroArch wrap-mode mapping"),
    (re.compile(r"\bvideo_shader_scale_type\b"), "RetroArch scale-type mapping"),
    (re.compile(r"\bstruct video_shader(_pass|_lut)?\b"), "RetroArch shader data model"),
    (re.compile(r"\bvideo_shader_ctx(_info|_filter|_dir)?\b"), "RetroArch shader context structs"),
    (re.compile(r"\bvideo_driver_(set_shader|filter_)?\w*"), "RetroArch video driver surface"),
    # RetroArch-wide identifier conventions.
    (re.compile(r"\brarch_[a-z0-9_]+"), "RetroArch rarch_ identifier convention"),
    (re.compile(r"\bRARCH_[A-Z0-9_]+"), "RetroArch RARCH_ macro convention"),
    (re.compile(r"\bsettings_t\b"), "RetroArch global settings struct"),
    (re.compile(r"\bconfig_get_ptr\b"), "RetroArch config accessor"),
    (re.compile(r"\bgfx_display_[a-z0-9_]+"), "RetroArch menu/gfx display surface"),
    (re.compile(r"\bmenu_driver_[a-z0-9_]+"), "RetroArch menu driver surface"),
    (re.compile(r"\binput_driver_[a-z0-9_]+"), "RetroArch input driver surface"),
    (re.compile(r"\bgfx/video_shader_parse\.c\b"), "RetroArch source file reference in code"),  # provenance-ok: fingerprint table
    # License contamination markers.
    (re.compile(r"GNU General Public License"), "GPL license header"),  # provenance-ok: fingerprint table
    (re.compile(r"\bGPLv?[23]\b"), "GPL version tag"),
    (re.compile(r"as published by the Free Software Foundation"), "GPL boilerplate sentence"),  # provenance-ok: fingerprint table
    # Copy-paste detector: RetroArch's parse loop leans on its string_list
    # helpers; that pairing does not occur in this codebase's stdlib code.
    (re.compile(r"\bstring_list_(find|free|append)\b"), "RetroArch string_list helper"),
]

WAIVER_MARKER = "provenance-ok:"


def scan_file(path: Path, allow: list[str]) -> tuple[list[str], list[str]]:
    """Return (findings, waived) as formatted report lines for one file."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return [f"unreadable: {exc}"], []
    findings: list[str] = []
    waived: list[str] = []
    for lineno, line in enumerate(text.splitlines(), start=1):
        if WAIVER_MARKER in line:
            waived.append(f"{path}:{lineno}: waived: {line.strip()}")
            continue
        if any(token in line for token in allow):
            continue
        for pattern, rationale in PATTERNS:
            if pattern.search(line):
                findings.append(
                    f"{path}:{lineno}: {rationale}: {line.strip()[:160]}"
                )
                break  # one report per line is enough
    return findings, waived


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".", help="repository root (default: cwd)")
    parser.add_argument("--report", action="store_true", help="also list waived lines")
    parser.add_argument("--allow", action="append", default=[],
                        help="substring waiver: skip lines containing this token")
    args = parser.parse_args(argv)

    root = Path(args.root).resolve()
    if not root.is_dir():
        print(f"error: root is not a directory: {root}", file=sys.stderr)
        return 2

    findings: list[str] = []
    waived: list[str] = []
    scanned = 0
    for sub in SCAN_DIRS:
        base = root / sub
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.is_symlink():
                continue
            if path.name in SCAN_FILES or path.suffix.lower() in SCAN_SUFFIXES:
                scanned += 1
                f, w = scan_file(path, args.allow)
                findings.extend(f)
                waived.extend(w)

    for line in waived:
        if args.report:
            print(line)
    for line in findings:
        print(f"FINDING {line}")
    print(f"scan_codefindings: {scanned} files scanned, "
          f"{len(findings)} finding(s), {len(waived)} waived")

    if findings:
        print(
            "\nRetroArch-origin fingerprints found in code-bearing files.\n"
            "Policy (docs/PLAN.md section 4): no RetroArch code, ever — clean-room\n"
            "against the libretro docs, docs/SPEC.md and black-box behavior only.\n"
            "Remove the code or re-express it from the spec; a false positive may\n"
            f"carry an inline `{WAIVER_MARKER} <reason>` waiver.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
