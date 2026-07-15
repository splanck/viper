#!/usr/bin/env bash
#===----------------------------------------------------------------------===//
#
# Part of the Viper project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//
#
# File: scripts/check_runtime_completeness.sh
# Purpose: Validate cross-row references in the modular runtime definition set.
# Key invariants:
#   - rtgen is the only parser for runtime definition manifests and fragments.
#   - Every class constructor, property accessor, and method target resolves.
# Ownership/Lifetime:
#   - Reads repository sources and an existing rtgen build artifact.
#   - Creates no persistent files.
# Links: src/tools/rtgen/rtgen.cpp, src/il/runtime/runtime.def
#
#===----------------------------------------------------------------------===//

set -euo pipefail

readonly DEF="src/il/runtime/runtime.def"
RTGEN="${VIPER_RTGEN:-build/src/rtgen}"

if [[ ! -f "${DEF}" ]]; then
    echo "ERROR: ${DEF} not found. Run from the project root." >&2
    exit 2
fi

if [[ ! -x "${RTGEN}" ]]; then
    config="${VIPER_BUILD_TYPE:-Debug}"
    candidate="build/src/${config}/rtgen.exe"
    if [[ -x "${candidate}" ]]; then
        RTGEN="${candidate}"
    else
        echo "ERROR: rtgen is not built. Run the platform build script first." >&2
        exit 2
    fi
fi

"${RTGEN}" --validate "${DEF}"
echo "OK: Runtime definition references are complete."

#===----------------------------------------------------------------------===//
# 3D stub-parity audit.
#
# The generated VM handler table takes the address of every RT_FUNC c-symbol
# unconditionally, so each 3D entry point must be defined by a source that
# still compiles when VIPER_ENABLE_GRAPHICS is OFF: an always-on file listed
# in RT_GRAPHICS_DISABLED_SOURCES / RT_AUDIO_SOURCES (plus one level of .inc
# includes) or a trap stub in src/runtime/graphics/common/*stubs*.c.
#===----------------------------------------------------------------------===//

readonly RT_CMAKE="src/runtime/CMakeLists.txt"
stub_tmp="$(mktemp -d)"
trap 'rm -rf "${stub_tmp}"' EXIT

awk '/RT_FUNC\(/ {
        line = $0
        sub(/.*RT_FUNC\(/, "", line)
        n = split(line, parts, ",")
        if (n >= 2) {
            s = parts[2]
            gsub(/[ \t]/, "", s)
            if (s ~ /^rt_/)
                print s
        }
    }' src/il/runtime/defs/graphics3d/*.def src/il/runtime/defs/game3d/*.def |
    sort -u > "${stub_tmp}/def_syms"

awk '/^set\(RT_GRAPHICS_DISABLED_SOURCES|^set\(RT_AUDIO_SOURCES/ { grab = 1; next }
     grab && /^\)/ { grab = 0 }
     grab {
        s = $1
        gsub(/[ \t]/, "", s)
        if (s ~ /\.(c|m|cpp)$/)
            print "src/runtime/" s
     }' "${RT_CMAKE}" > "${stub_tmp}/link_files"

cp "${stub_tmp}/link_files" "${stub_tmp}/corpus_files"
while IFS= read -r src_file; do
    src_dir="$(dirname "${src_file}")"
    grep -h '#include "' "${src_file}" 2>/dev/null |
        sed -n 's/.*#include "\([^"]*\.inc\)".*/\1/p' |
        while IFS= read -r inc_file; do
            [[ -f "${src_dir}/${inc_file}" ]] && echo "${src_dir}/${inc_file}"
        done
done < "${stub_tmp}/link_files" >> "${stub_tmp}/corpus_files"
sort -u "${stub_tmp}/corpus_files" -o "${stub_tmp}/corpus_files"

xargs grep -oh 'rt_[a-z0-9_]*' < "${stub_tmp}/corpus_files" 2>/dev/null |
    sort -u > "${stub_tmp}/corpus_syms"

if ! comm -23 "${stub_tmp}/def_syms" "${stub_tmp}/corpus_syms" > "${stub_tmp}/missing"; then
    echo "ERROR: stub-parity comparison failed." >&2
    exit 2
fi

if [[ -s "${stub_tmp}/missing" ]]; then
    echo "ERROR: 3D runtime entry points missing from graphics-disabled builds:" >&2
    sed 's/^/  /' "${stub_tmp}/missing" >&2
    echo "Add a trap stub in src/runtime/graphics/common/*stubs*.c or list the" >&2
    echo "defining source in RT_GRAPHICS_DISABLED_SOURCES (${RT_CMAKE})." >&2
    exit 1
fi

def_total="$(wc -l < "${stub_tmp}/def_syms" | tr -d ' ')"
echo "OK: 3D stub parity holds for ${def_total} registered entry points."
