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

OUT_DIR="$BUILD_DIR/tests/linux-toolchain-appimage-validate"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
APPIMAGE="$OUT_DIR/Viper.AppImage"

STAGE_ARGS=(--build-dir "$BUILD_DIR" --skip-build)
if [[ -e "$BUILD_DIR/install_manifest.txt" && ! -w "$BUILD_DIR/install_manifest.txt" ]]; then
    STAGE_DIR="$OUT_DIR/stage"
    mkdir -p "$STAGE_DIR/bin" "$STAGE_DIR/include/viper" "$STAGE_DIR/lib/cmake/Viper" \
        "$STAGE_DIR/share/man/man1" "$STAGE_DIR/share/doc/viper"
    cp "$VIPER_BIN" "$STAGE_DIR/bin/viper"
    chmod 755 "$STAGE_DIR/bin/viper"
    printf '#define VIPER_VERSION_STR "0.0.0"\n' >"$STAGE_DIR/include/viper/version.hpp"
    printf '# AppImage validation config\n' >"$STAGE_DIR/lib/cmake/Viper/ViperConfig.cmake"
    printf '# AppImage validation targets\n' >"$STAGE_DIR/lib/cmake/Viper/ViperTargets.cmake"
    printf 'set(PACKAGE_VERSION "0.0.0")\n' >"$STAGE_DIR/lib/cmake/Viper/ViperConfigVersion.cmake"
    printf '.TH viper 1\n' >"$STAGE_DIR/share/man/man1/viper.1"
    printf 'Viper AppImage validation stage\n' >"$STAGE_DIR/share/doc/viper/README.md"
    find "$BUILD_DIR/src" -type f -name '*.a' -exec cp {} "$STAGE_DIR/lib/" \;
    STAGE_ARGS=(--stage-dir "$STAGE_DIR")
fi

CMD=("$VIPER_BIN" install-package "${STAGE_ARGS[@]}" --target appimage -o "$APPIMAGE")
if [[ -n "$CONFIG" && "${STAGE_ARGS[0]}" == "--build-dir" ]]; then
    CMD+=(--config "$CONFIG")
fi
"${CMD[@]}"
"$VIPER_BIN" install-package --verify-only "$APPIMAGE"
chmod +x "$APPIMAGE"
"$APPIMAGE" --version >/dev/null

echo "Linux installer validation passed"
