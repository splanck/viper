#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'USAGE'
Usage: validate-linux-toolchain-installer.sh --build-dir <dir> [--config <cfg>] [--install]

Builds and verifies Linux toolchain installer artifacts using viper install-package.
With --install, also enables privileged dpkg/rpm install smoke tests when run as root.
USAGE
}

BUILD_DIR=""
CONFIG="${VIPER_CONFIG:-}"
RUN_INSTALL=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            BUILD_DIR="$2"
            shift 2
            ;;
        --config)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            CONFIG="$2"
            shift 2
            ;;
        --install)
            RUN_INSTALL=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ -z "$BUILD_DIR" ]]; then
    usage
    exit 2
fi
if [[ "$(uname -s)" != "Linux" ]]; then
    echo "validate-linux-toolchain-installer.sh requires a Linux host" >&2
    exit 2
fi

VIPER_BIN="$BUILD_DIR/src/tools/viper/viper"
if [[ ! -x "$VIPER_BIN" ]]; then
    echo "viper binary not found or not executable: $VIPER_BIN" >&2
    exit 2
fi

CTEST_ARGS=(--test-dir "$BUILD_DIR" --output-on-failure -L installer)
if [[ -n "$CONFIG" ]]; then
    CTEST_ARGS+=(-C "$CONFIG")
fi

if [[ "$RUN_INSTALL" == "1" ]]; then
    export VIPER_RUN_LINUX_INSTALLER_SMOKE=1
fi

ctest "${CTEST_ARGS[@]}"

OUT_DIR="$BUILD_DIR/tests/linux-toolchain-bundle-validate"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
BUNDLE="$OUT_DIR/viper-toolchain.run"

STAGE_ARGS=(--build-dir "$BUILD_DIR" --skip-build)
CMD=("$VIPER_BIN" install-package "${STAGE_ARGS[@]}" --target linux-bundle -o "$BUNDLE")
if [[ -n "$CONFIG" && "${STAGE_ARGS[0]}" == "--build-dir" ]]; then
    CMD+=(--config "$CONFIG")
fi
"${CMD[@]}"
"$VIPER_BIN" install-package --verify-only "$BUNDLE" --require-checksum
chmod +x "$BUNDLE"
BUNDLE_CACHE="$OUT_DIR/cache"
mkdir -p "$BUNDLE_CACHE"
XDG_CACHE_HOME="$BUNDLE_CACHE" VIPER_BUNDLE_QUIET=1 "$BUNDLE" --version >/dev/null
if [[ -z "$(find "$BUNDLE_CACHE/viper" -name .payload.sha256 -type f -print -quit 2>/dev/null)" ]]; then
    echo "Linux bundle did not create an XDG cache payload stamp" >&2
    exit 1
fi

echo "Linux installer validation passed"
