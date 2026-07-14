#!/usr/bin/env bash
set -euo pipefail

readonly BUILD_SCRIPT="${1?usage: $0 <build-viper-unix-script>}"
readonly INVALID_GENERATOR="Viper Invalid Generator"
TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/viper-build-lock.XXXXXX")"
# Resolve the physical path the same way the build script canonicalizes
# BUILD_DIR (macOS mktemp returns /var/..., a symlink to /private/var/...).
TMP_ROOT="$(cd "$TMP_ROOT" && pwd -P)"
readonly TMP_ROOT
readonly LOCK_PATH="$TMP_ROOT/.viper-build.lock"

cleanup() {
    rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

run_build_script() {
    env \
        VIPER_BUILD_DIR="$TMP_ROOT" \
        VIPER_CMAKE_GENERATOR="$INVALID_GENERATOR" \
        VIPER_NO_CCACHE=1 \
        VIPER_SKIP_CLEAN=1 \
        VIPER_SKIP_TESTS=1 \
        VIPER_SKIP_LINT=1 \
        VIPER_SKIP_AUDIT=1 \
        VIPER_SKIP_SMOKE=1 \
        VIPER_SKIP_INSTALL=1 \
        "$BUILD_SCRIPT" 2>&1
}

# A configure failure after successful acquisition must release the lock.
set +e
OUTPUT="$(run_build_script)"
STATUS=$?
set -e
if [[ "$STATUS" -eq 0 ]]; then
    echo "expected invalid CMake generator to fail" >&2
    exit 1
fi
if [[ -e "$LOCK_PATH" || -L "$LOCK_PATH" ]]; then
    echo "build lock remained after the build script exited" >&2
    exit 1
fi

# A stale owner is reclaimed, then the newly acquired lock is released on exit.
STALE_PID=999999
while kill -0 "$STALE_PID" 2>/dev/null; do
    STALE_PID=$((STALE_PID + 1))
done
ln -s "$STALE_PID" "$LOCK_PATH"
set +e
OUTPUT="$(run_build_script)"
STATUS=$?
set -e
if [[ "$STATUS" -eq 0 ]]; then
    echo "expected invalid CMake generator to fail after stale-lock recovery" >&2
    exit 1
fi
if [[ "$OUTPUT" == *"another Viper build is already using"* ]]; then
    echo "stale build lock was treated as live" >&2
    exit 1
fi
if [[ -e "$LOCK_PATH" || -L "$LOCK_PATH" ]]; then
    echo "reclaimed build lock remained after the build script exited" >&2
    exit 1
fi

# The test shell is a live owner. The build script must reject the invocation
# without replacing or removing that owner's lock.
ln -s "$$" "$LOCK_PATH"
set +e
OUTPUT="$(run_build_script)"
STATUS=$?
set -e
if [[ "$STATUS" -eq 0 ]]; then
    echo "concurrent build unexpectedly succeeded" >&2
    exit 1
fi
if [[ "$OUTPUT" != *"another Viper build is already using $TMP_ROOT (PID $$)"* ]]; then
    echo "concurrent build did not report the live lock owner" >&2
    printf '%s\n' "$OUTPUT" >&2
    exit 1
fi
if [[ "$(readlink "$LOCK_PATH")" != "$$" ]]; then
    echo "concurrent build replaced the live owner's lock" >&2
    exit 1
fi
