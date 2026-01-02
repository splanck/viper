#!/bin/bash
#
# ViperOS QEMU Test Runner
#
# A robust test runner for QEMU-based integration tests.
# Boots the OS, captures serial output, and validates expected patterns.
# Exits early when all expected patterns are found (no need to wait for timeout).
#
# Usage:
#   run_qemu_test.sh [options] --kernel <path> --expect <pattern>
#
# Options:
#   --qemu <path>      Path to qemu-system-aarch64 (auto-detected if not set)
#   --kernel <path>    Path to kernel ELF (required)
#   --disk <path>      Path to disk image (optional)
#   --microkernel-devices  Attach dedicated microkernel devices if present
#   --timeout <secs>   Timeout in seconds (default: 30)
#   --expect <regex>   Pattern that must appear (can specify multiple)
#   --forbid <regex>   Pattern that must NOT appear (can specify multiple)
#   --name <name>      Test name for logging (default: qemu_test)
#   --log-dir <dir>    Directory for log files (default: current dir)
#   --memory <size>    Memory size (default: 128M)
#   --verbose          Print serial output in real-time
#
# Exit codes:
#   0 - All expectations met
#   1 - Expectation failed or forbidden pattern found
#   2 - Timeout
#   3 - QEMU error or bad arguments

set -o pipefail

# Defaults
QEMU=""
KERNEL=""
DISK=""
MICROKERNEL_DEVICES=0
TIMEOUT=30
MEMORY="128M"
TEST_NAME="qemu_test"
LOG_DIR="."
VERBOSE=0

# Arrays for patterns
declare -a EXPECT_PATTERNS
declare -a FORBID_PATTERNS

# Colors (disabled if not a terminal)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    NC=''
fi

usage() {
    echo "Usage: $0 [options] --kernel <path> --expect <pattern>"
    echo ""
    echo "Options:"
    echo "  --qemu <path>      Path to qemu-system-aarch64"
    echo "  --kernel <path>    Path to kernel ELF (required)"
    echo "  --disk <path>      Path to disk image"
    echo "  --microkernel-devices  Attach dedicated microkernel devices if present"
    echo "  --timeout <secs>   Timeout in seconds (default: 30)"
    echo "  --expect <regex>   Pattern that must appear (repeatable)"
    echo "  --forbid <regex>   Pattern that must NOT appear (repeatable)"
    echo "  --name <name>      Test name for logging"
    echo "  --log-dir <dir>    Directory for log files"
    echo "  --memory <size>    Memory size (default: 128M)"
    echo "  --verbose          Print serial output in real-time"
    exit 3
}

find_qemu() {
    local candidates=(
        "/opt/homebrew/opt/qemu/bin/qemu-system-aarch64"
        "/usr/local/bin/qemu-system-aarch64"
        "/usr/bin/qemu-system-aarch64"
    )
    for path in "${candidates[@]}"; do
        if [[ -x "$path" ]]; then
            echo "$path"
            return 0
        fi
    done
    # Try PATH
    if command -v qemu-system-aarch64 &>/dev/null; then
        command -v qemu-system-aarch64
        return 0
    fi
    return 1
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --qemu)
            QEMU="$2"
            shift 2
            ;;
        --kernel)
            KERNEL="$2"
            shift 2
            ;;
        --disk)
            DISK="$2"
            shift 2
            ;;
        --microkernel-devices)
            MICROKERNEL_DEVICES=1
            shift
            ;;
        --timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        --expect)
            EXPECT_PATTERNS+=("$2")
            shift 2
            ;;
        --forbid)
            FORBID_PATTERNS+=("$2")
            shift 2
            ;;
        --name)
            TEST_NAME="$2"
            shift 2
            ;;
        --log-dir)
            LOG_DIR="$2"
            shift 2
            ;;
        --memory)
            MEMORY="$2"
            shift 2
            ;;
        --verbose)
            VERBOSE=1
            shift
            ;;
        --help|-h)
            usage
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            ;;
    esac
done

# Validate arguments
if [[ -z "$KERNEL" ]]; then
    echo "Error: --kernel is required" >&2
    usage
fi

if [[ ! -f "$KERNEL" ]]; then
    echo "Error: Kernel not found: $KERNEL" >&2
    exit 3
fi

if [[ ${#EXPECT_PATTERNS[@]} -eq 0 ]]; then
    echo "Error: At least one --expect pattern is required" >&2
    usage
fi

# Find QEMU
if [[ -z "$QEMU" ]]; then
    QEMU=$(find_qemu) || {
        echo "Error: qemu-system-aarch64 not found" >&2
        echo "       Install QEMU or specify --qemu path" >&2
        exit 3
    }
fi

if [[ ! -x "$QEMU" ]]; then
    echo "Error: QEMU not executable: $QEMU" >&2
    exit 3
fi

# Build QEMU command
QEMU_OPTS=(
    -machine virt
    -cpu cortex-a72
    -smp 4
    -m "$MEMORY"
    -nographic
    -kernel "$KERNEL"
    -serial mon:stdio
    -device virtio-rng-device
    -no-reboot
)

# Add disk if specified and exists
if [[ -n "$DISK" && -f "$DISK" ]]; then
    QEMU_OPTS+=(
        -drive "file=$DISK,if=none,format=raw,id=disk0"
        -device virtio-blk-device,drive=disk0
    )
fi

# Add network
QEMU_OPTS+=(
    -netdev user,id=net0
    -device virtio-net-device,netdev=net0
)

# Optional dedicated devices for user-space microkernel servers (blkd/fsd/netd).
if [[ $MICROKERNEL_DEVICES -eq 1 ]]; then
    build_dir="$(dirname "$KERNEL")"

    microkernel_img="${build_dir}/microkernel.img"
    if [[ -f "$microkernel_img" ]]; then
        QEMU_OPTS+=(
            -drive "file=$microkernel_img,if=none,format=raw,id=disk1"
            -device virtio-blk-device,drive=disk1
        )
    fi

    # Match the dev provisioning behavior in scripts/build_viper.sh: only add a
    # second NIC when the netd server is present.
    if [[ -f "${build_dir}/netd.sys" ]]; then
        QEMU_OPTS+=(
            -netdev user,id=net1
            -device virtio-net-device,netdev=net1
        )
    fi
fi

# Create log directory
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/${TEST_NAME}.log"
OUTPUT_FILE=$(mktemp)
MATCH_STATUS_FILE=$(mktemp)

# Initialize tracking - which patterns have been matched
declare -a MATCHED
for i in "${!EXPECT_PATTERNS[@]}"; do
    MATCHED[$i]=0
done

# Cleanup on exit
QEMU_PID=""
cleanup() {
    if [[ -n "$QEMU_PID" ]] && kill -0 "$QEMU_PID" 2>/dev/null; then
        kill -TERM "$QEMU_PID" 2>/dev/null
        wait "$QEMU_PID" 2>/dev/null
    fi
    rm -f "$OUTPUT_FILE" "$MATCH_STATUS_FILE"
}
trap cleanup EXIT

# Print test info
if [[ $VERBOSE -eq 1 ]]; then
    echo "[TEST] $TEST_NAME" >&2
    echo "[QEMU] $QEMU ${QEMU_OPTS[*]}" >&2
fi

# Start QEMU in background, redirecting output to file
"$QEMU" "${QEMU_OPTS[@]}" > "$OUTPUT_FILE" 2>&1 &
QEMU_PID=$!

# Monitor output with early exit
RESULT=2  # Default to timeout
STATUS="TIMEOUT"
START_TIME=$(date +%s)
FOUND_FORBIDDEN=""

while true; do
    # Check if QEMU died
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        # QEMU exited - do final pattern check
        break
    fi

    # Check timeout
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    if [[ $ELAPSED -ge $TIMEOUT ]]; then
        break
    fi

    # Read current output
    CURRENT_OUTPUT=$(cat "$OUTPUT_FILE" 2>/dev/null)

    # Check forbidden patterns first
    for pattern in "${FORBID_PATTERNS[@]}"; do
        if echo "$CURRENT_OUTPUT" | grep -qE "$pattern"; then
            FOUND_FORBIDDEN="$pattern"
            RESULT=1
            STATUS="FAIL"
            break 2
        fi
    done

    # Check expected patterns
    ALL_MATCHED=1
    for i in "${!EXPECT_PATTERNS[@]}"; do
        pattern="${EXPECT_PATTERNS[$i]}"
        if [[ ${MATCHED[$i]} -eq 0 ]]; then
            if echo "$CURRENT_OUTPUT" | grep -qE "$pattern"; then
                MATCHED[$i]=1
                if [[ $VERBOSE -eq 1 ]]; then
                    echo "[MATCH] Found: $pattern" >&2
                fi
            else
                ALL_MATCHED=0
            fi
        fi
    done

    # If all expected patterns matched, we're done!
    if [[ $ALL_MATCHED -eq 1 ]]; then
        RESULT=0
        STATUS="PASS"
        break
    fi

    # Small delay to avoid busy-waiting
    sleep 0.1
done

# Kill QEMU if still running
if kill -0 "$QEMU_PID" 2>/dev/null; then
    kill -TERM "$QEMU_PID" 2>/dev/null
    wait "$QEMU_PID" 2>/dev/null
fi

# Read final output
OUTPUT=$(cat "$OUTPUT_FILE")

# If we timed out, do final pattern check (maybe patterns appeared in last moment)
if [[ $RESULT -eq 2 ]]; then
    ALL_MATCHED=1
    for i in "${!EXPECT_PATTERNS[@]}"; do
        pattern="${EXPECT_PATTERNS[$i]}"
        if ! echo "$OUTPUT" | grep -qE "$pattern"; then
            ALL_MATCHED=0
            break
        fi
    done
    if [[ $ALL_MATCHED -eq 1 ]]; then
        RESULT=0
        STATUS="PASS"
    fi
fi

# Collect failed expectations for logging
FAILED_EXPECTS=()
for pattern in "${EXPECT_PATTERNS[@]}"; do
    if ! echo "$OUTPUT" | grep -qE "$pattern"; then
        FAILED_EXPECTS+=("$pattern")
    fi
done

# Collect found forbidden patterns for logging
FOUND_FORBIDDEN_ALL=()
for pattern in "${FORBID_PATTERNS[@]}"; do
    if echo "$OUTPUT" | grep -qE "$pattern"; then
        FOUND_FORBIDDEN_ALL+=("$pattern")
    fi
done

# Write log file
{
    echo "Test: $TEST_NAME"
    echo "Status: $STATUS"
    echo "Kernel: $KERNEL"
    echo "Disk: ${DISK:-(none)}"
    echo "Timeout: ${TIMEOUT}s"
    echo "Expect patterns: ${EXPECT_PATTERNS[*]}"
    echo "Forbid patterns: ${FORBID_PATTERNS[*]}"
    echo ""
    echo "============================================================"
    echo "SERIAL OUTPUT:"
    echo "============================================================"
    echo ""
    echo "$OUTPUT"
    if [[ ${#FAILED_EXPECTS[@]} -gt 0 ]]; then
        echo ""
        echo "============================================================"
        echo "FAILED EXPECTATIONS:"
        for pat in "${FAILED_EXPECTS[@]}"; do
            echo "  - $pat"
        done
    fi
    if [[ ${#FOUND_FORBIDDEN_ALL[@]} -gt 0 ]]; then
        echo ""
        echo "============================================================"
        echo "FORBIDDEN PATTERNS FOUND:"
        for pat in "${FOUND_FORBIDDEN_ALL[@]}"; do
            echo "  - $pat"
        done
    fi
} > "$LOG_FILE"

# Print result
if [[ $RESULT -eq 0 ]]; then
    echo -e "${GREEN}[PASS]${NC} $TEST_NAME"
elif [[ $RESULT -eq 2 ]]; then
    echo -e "${YELLOW}[TIMEOUT]${NC} $TEST_NAME - exceeded ${TIMEOUT}s"
    echo "         Log: $LOG_FILE"
else
    echo -e "${RED}[FAIL]${NC} $TEST_NAME"
    if [[ ${#FAILED_EXPECTS[@]} -gt 0 ]]; then
        echo "       Missing: ${FAILED_EXPECTS[*]}"
    fi
    if [[ ${#FOUND_FORBIDDEN_ALL[@]} -gt 0 ]]; then
        echo "       Forbidden found: ${FOUND_FORBIDDEN_ALL[*]}"
    fi
    echo "       Log: $LOG_FILE"
fi

exit $RESULT
