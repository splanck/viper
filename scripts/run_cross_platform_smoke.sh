#!/usr/bin/env bash
# Run a short host-capability smoke slice after a successful build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"

usage() {
    echo "usage: $0 [--build-dir <dir>]"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --build-dir=*)
            BUILD_DIR="${1#--build-dir=}"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

CAP_FILE="$BUILD_DIR/generated/viper/platform/Capabilities.hpp"
if [[ ! -f "$CAP_FILE" ]]; then
    echo "error: capability header not found: $CAP_FILE" >&2
    exit 1
fi

cap_value() {
    local macro="$1"
    local value
    value="$(awk -v macro="$macro" '$1 == "#define" && $2 == macro { print $3; exit }' "$CAP_FILE")"
    if [[ -z "$value" ]]; then
        echo 0
    else
        echo "$value"
    fi
}

HOST_WINDOWS="$(cap_value VIPER_HOST_WINDOWS)"
HOST_MACOS="$(cap_value VIPER_HOST_MACOS)"
HOST_LINUX="$(cap_value VIPER_HOST_LINUX)"
HAS_GRAPHICS="$(cap_value VIPER_BUILD_HAS_GRAPHICS)"
HAS_AUDIO="$(cap_value VIPER_BUILD_HAS_AUDIO)"
HAS_GUI="$(cap_value VIPER_BUILD_HAS_GUI)"
NATIVE_LINK_X64="$(cap_value VIPER_BUILD_NATIVE_LINK_X86_64)"
NATIVE_LINK_A64="$(cap_value VIPER_BUILD_NATIVE_LINK_AARCH64)"

HAS_DISPLAY=0
if [[ "${VIPER_SMOKE_FORCE_DISPLAY:-0}" == "1" ]]; then
    HAS_DISPLAY=1
elif [[ $HOST_LINUX -eq 1 ]]; then
    if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
        HAS_DISPLAY=1
    fi
elif [[ $HOST_MACOS -eq 1 || $HOST_WINDOWS -eq 1 ]]; then
    HAS_DISPLAY=1
fi

echo "=========================================="
echo " Viper Host Smoke Slice"
echo "=========================================="
if [[ $HOST_WINDOWS -eq 1 ]]; then
    echo " Host:                 Windows"
elif [[ $HOST_MACOS -eq 1 ]]; then
    echo " Host:                 macOS"
elif [[ $HOST_LINUX -eq 1 ]]; then
    echo " Host:                 Linux"
else
    echo " Host:                 Unknown"
fi
echo " Graphics:             $HAS_GRAPHICS"
echo " Audio:                $HAS_AUDIO"
echo " GUI:                  $HAS_GUI"
echo " Display available:    $HAS_DISPLAY"
echo " Native link x86_64:   $NATIVE_LINK_X64"
echo " Native link AArch64:  $NATIVE_LINK_A64"
echo "=========================================="

run_named_tests() {
    local regex="$1"
    local listing
    if [[ -z "$regex" ]]; then
        return
    fi
    listing="$(ctest --test-dir "$BUILD_DIR" -N -R "$regex" 2>&1 || true)"
    if ! printf '%s\n' "$listing" | grep -q "Test #"; then
        return
    fi
    ctest --test-dir "$BUILD_DIR" --output-on-failure -R "$regex"
}

core_regex='^(smoke_term_basic|smoke_basic_oop|zia_smoke_paint|zia_smoke_vipersql|zia_smoke_chess)$'
run_named_tests "$core_regex"

disabled_surface_regex='^(test_rt_canvas_unavailable|test_rt_graphics_surface_link|test_rt_audio_unavailable|test_rt_audio_surface_link)$'
run_named_tests "$disabled_surface_regex"

planner_regex='^(test_linker_platform_import_planners|test_linker_runtime_import_audit|test_linker_elf_exe_writer)$'
run_named_tests "$planner_regex"

if [[ $HOST_MACOS -eq 1 && $NATIVE_LINK_A64 -eq 1 ]]; then
    native_link_regex='^(native_smoke_3dbowling_build_arm64|native_smoke_xenoscape_start_arm64|native_smoke_xenoscape_action_names_arm64)$'
    run_named_tests "$native_link_regex"
fi

if [[ $HAS_GRAPHICS -eq 1 && $HAS_DISPLAY -eq 1 ]]; then
    display_regex='^(zia_smoke_viperide|zia_smoke_3dbowling|zia_smoke_3dscene|zia_smoke_3dbaseball|zia_smoke_xenoscape)$'
    run_named_tests "$display_regex"
else
    echo "Skipping display-bound smoke tests on this host"
fi
