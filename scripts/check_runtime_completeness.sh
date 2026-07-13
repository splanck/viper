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
