#!/usr/bin/env bash
# Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
# AI Assisted by Super-Z (GLM-5.3-Flash)
# License: MIT
#
# Self-containment check: every public header and every test translation unit
# must compile on its own, with only the includes it declares.
#
# Why this exists: a translation unit that relies on another header pulling
# in <optional> (or <string>, <vector>, ...) compiles fine in one build
# layout and fails in another. The CI hit exactly that: the pipeline test
# used std::optional without including <optional>, and the failure only
# appeared at step 96 of a 129-step build. This check runs in seconds.
#
# Usage: tools/check_self_contained.sh [build-dir]
#   build-dir defaults to "build" and must be a configured build tree (it
#   supplies the dependency include paths).

set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

BUILD_DIR="${1:-build}"
if [[ ! -d "$BUILD_DIR" ]]; then
        echo "check_self_contained: '$BUILD_DIR' is not a directory" >&2
        echo "  configure a build tree first: cmake -S . -B $BUILD_DIR -G Ninja" >&2
        exit 2
fi

# The dependency include paths, taken from the configured build tree so the
# check matches what the real build sees.
INCLUDES=(
        "-Iinclude"
        "-Isrc"
        "-I${BUILD_DIR}/_deps/vulkan-headers-src/include"
        "-I${BUILD_DIR}/_deps/volk-src"
        "-I${BUILD_DIR}/_deps/vma-src/include"
)
for extra in "${BUILD_DIR}"/_deps/*/include; do
        [[ -d "$extra" ]] && INCLUDES+=("-I$extra")
done

CXX="${CXX:-c++}"
STD="-std=c++23"
DEFS=(-DSLANGVK_ENABLE_VK=1 '-DSLANGVK_TESTS_DATA_DIR="."')

failures=0

check() {
        local label="$1"
        shift
        if "$CXX" "$STD" -fsyntax-only "${DEFS[@]}" "${INCLUDES[@]}" "$@" 2>/tmp/slangvk_selfcontained.$$; then
                echo "  ok   $label"
        else
                echo "  FAIL $label"
                sed 's/^/       /' /tmp/slangvk_selfcontained.$$ | head -12
                failures=$((failures + 1))
        fi
        rm -f /tmp/slangvk_selfcontained.$$
}

echo "Self-containment check (public headers):"
for header in include/slangvk/*.hpp include/slangvk/core/*.hpp; do
        [[ -f "$header" ]] || continue
        check "$header" -x c++ "$header"
done

echo "Self-containment check (public C header):"
if command -v gcc >/dev/null 2>&1; then
        if gcc -std=c11 -fsyntax-only "-I${BUILD_DIR}/_deps/vulkan-headers-src/include" \
                -x c include/slangvk/c.h 2>/tmp/slangvk_selfcontained.$$; then
                echo "  ok   include/slangvk/c.h"
        else
                echo "  FAIL include/slangvk/c.h"
                sed 's/^/       /' /tmp/slangvk_selfcontained.$$ | head -12
                failures=$((failures + 1))
        fi
        rm -f /tmp/slangvk_selfcontained.$$
fi

echo "Self-containment check (test translation units):"
for source in tests/*.cpp; do
        [[ -f "$source" ]] || continue
        check "$source" "$source"
done

if [[ $failures -ne 0 ]]; then
        echo "check_self_contained: $failures failure(s)" >&2
        exit 1
fi
echo "check_self_contained: all translation units are self-contained"
