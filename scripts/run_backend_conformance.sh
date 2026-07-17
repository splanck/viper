#!/usr/bin/env bash
#===----------------------------------------------------------------------===//
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//
#
# File: scripts/run_backend_conformance.sh
# Purpose: Render the fixed conformance scene per 3D backend and diff images
#          against the software-rasterizer golden reference.
# Key invariants:
#   - The software backend is the golden reference for standard-Z rendering.
#   - Self-check mode renders software twice and requires byte-identical
#     output (rasterizer determinism), needing no display or GPU.
#   - GPU backends are compared with tolerance for AA/precision variance.
# Ownership/Lifetime:
#   - Writes only /tmp/zanna_conformance_*.png scratch images.
# Links: src/tests/graphics_conformance/conformance_scene.zia,
#        src/tests/graphics_conformance/conformance_compare.zia
#
#===----------------------------------------------------------------------===//

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_backend_conformance.sh [options]
  --zia PATH        zia interpreter (default: build/src/tools/zia/zia)
  --backends "..."  space-separated backends to test against software
                    (default: metal on macOS, opengl on Linux, d3d11 on Windows)
  --self-check      render software twice and require byte-identical output
  --postfx          also run the PostFX-enabled scene variant
EOF
}

ZIA="build/src/tools/zia/zia"
BACKENDS=""
SELF_CHECK=0
POSTFX=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --zia) ZIA="$2"; shift 2 ;;
        --backends) BACKENDS="$2"; shift 2 ;;
        --self-check) SELF_CHECK=1; shift ;;
        --postfx) POSTFX=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown option $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! -x "${ZIA}" ]]; then
    echo "ERROR: zia interpreter not found at ${ZIA} (build first or pass --zia)." >&2
    exit 2
fi

if [[ -z "${BACKENDS}" ]]; then
    case "$(uname -s)" in
        Darwin) BACKENDS="metal" ;;
        Linux) BACKENDS="opengl" ;;
        *) BACKENDS="d3d11" ;;
    esac
fi

SCENE="src/tests/graphics_conformance/conformance_scene.zia"
COMPARE="src/tests/graphics_conformance/conformance_compare.zia"

render() {
    # render <backend> <out.png> [postfx01]
    local backend="$1" out="$2" postfx="${3:-0}"
    echo "== render backend=${backend} postfx=${postfx} -> ${out}"
    ZANNA_3D_BACKEND="${backend}" \
    ZANNA_CONFORMANCE_OUT="${out}" \
    ZANNA_CONFORMANCE_POSTFX="${postfx}" \
        "${ZIA}" "${SCENE}" | grep -E "CONFORMANCE|RESULT" || true
}

compare_images() {
    # compare_images <a.png> <b.png> <exact01>
    local a="$1" b="$2" exact="$3"
    ZANNA_CONFORMANCE_A="${a}" \
    ZANNA_CONFORMANCE_B="${b}" \
    ZANNA_CONFORMANCE_EXACT="${exact}" \
        "${ZIA}" "${COMPARE}"
}

fail=0

if [[ "${SELF_CHECK}" -eq 1 ]]; then
    # Display-free determinism gate: the software rasterizer must reproduce the
    # scene exactly across runs. Runs standalone (used by the smoke test).
    render software /tmp/zanna_conformance_sw_a.png 0
    render software /tmp/zanna_conformance_sw_b.png 0
    if cmp -s /tmp/zanna_conformance_sw_a.png /tmp/zanna_conformance_sw_b.png; then
        echo "SELF-CHECK: software renders are byte-identical"
    else
        echo "SELF-CHECK: software PNG bytes differ; checking pixels" >&2
    fi
    if ! compare_images /tmp/zanna_conformance_sw_a.png \
                        /tmp/zanna_conformance_sw_b.png 1 | tee /dev/stderr |
            grep -q "RESULT: ok"; then
        echo "FAIL: software rasterizer is not deterministic" >&2
        echo "RESULT: fail"
        exit 1
    fi
    echo "RESULT: ok"
    exit 0
fi

variants="0"
[[ "${POSTFX}" -eq 1 ]] && variants="0 1"
for postfx in ${variants}; do
    suffix=""
    [[ "${postfx}" == "1" ]] && suffix="_postfx"
    golden="/tmp/zanna_conformance_software${suffix}.png"
    render software "${golden}" "${postfx}"
    for backend in ${BACKENDS}; do
        [[ "${backend}" == "software" ]] && continue
        candidate="/tmp/zanna_conformance_${backend}${suffix}.png"
        render "${backend}" "${candidate}" "${postfx}"
        if [[ ! -f "${candidate}" ]]; then
            echo "FAIL: backend ${backend} produced no image" >&2
            fail=1
            continue
        fi
        if ! compare_images "${golden}" "${candidate}" 0 | tee /dev/stderr |
                grep -q "RESULT: ok"; then
            echo "FAIL: backend ${backend}${suffix} diverges from software golden" >&2
            fail=1
        fi
    done
done

if [[ "${fail}" -ne 0 ]]; then
    echo "RESULT: fail"
    exit 1
fi
echo "RESULT: ok"
