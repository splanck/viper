#!/bin/bash
# ViperOS QEMU Smoke Test Script
# Runs automated tests against ViperOS in QEMU
#
# Usage: ./scripts/test-qemu.sh [--skip-build] [--verbose]
#
# Exit codes:
#   0 - All tests passed
#   1 - One or more tests failed
#   2 - Build or setup error

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Options
SKIP_BUILD=false
VERBOSE=false
TIMEOUT=60  # Total test timeout in seconds

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Test results
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Temp files
SERIAL_OUTPUT=""
FIFO_IN=""
QEMU_PID=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [--skip-build] [--verbose] [--timeout N]"
            echo ""
            echo "Options:"
            echo "  --skip-build   Skip building, use existing binaries"
            echo "  --verbose      Show detailed output"
            echo "  --timeout N    Set test timeout (default: 60s)"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 2
            ;;
    esac
done

print_header() {
    echo -e "${CYAN}"
    echo "╔═══════════════════════════════════════════════════════════╗"
    echo "║           ViperOS v0.2.0 Smoke Tests                      ║"
    echo "╚═══════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

log() {
    if [[ "$VERBOSE" == true ]]; then
        echo -e "${BLUE}[LOG]${NC} $1"
    fi
}

log_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
}

pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((TESTS_PASSED++))
    ((TESTS_RUN++))
}

fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    if [[ -n "$2" ]]; then
        echo -e "${RED}       Expected:${NC} $2"
    fi
    if [[ -n "$3" ]]; then
        echo -e "${RED}       Got:${NC} $3"
    fi
    ((TESTS_FAILED++))
    ((TESTS_RUN++))
}

cleanup() {
    log "Cleaning up..."

    # Kill QEMU if running
    if [[ -n "$QEMU_PID" ]] && kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi

    # Remove temp files
    [[ -n "$SERIAL_OUTPUT" && -f "$SERIAL_OUTPUT" ]] && rm -f "$SERIAL_OUTPUT"
    [[ -n "$FIFO_IN" && -p "$FIFO_IN" ]] && rm -f "$FIFO_IN"

    # Remove any stray temp files
    rm -f /tmp/viperos-test-* 2>/dev/null || true
}

trap cleanup EXIT

find_qemu() {
    local paths=(
        "/opt/homebrew/opt/qemu/bin/qemu-system-aarch64"
        "/usr/local/bin/qemu-system-aarch64"
        "/usr/bin/qemu-system-aarch64"
        "qemu-system-aarch64"
    )

    for path in "${paths[@]}"; do
        if command -v "$path" &>/dev/null || [[ -x "$path" ]]; then
            echo "$path"
            return 0
        fi
    done

    return 1
}

build_viperos() {
    if [[ "$SKIP_BUILD" == true ]]; then
        log "Skipping build (--skip-build)"
        return 0
    fi

    echo -e "${BLUE}[BUILD]${NC} Building ViperOS..."

    cd "$PROJECT_DIR"

    # Run cmake if needed
    if [[ ! -f "$BUILD_DIR/Makefile" ]]; then
        cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 || {
            echo -e "${RED}[ERROR]${NC} CMake configuration failed"
            return 1
        }
    fi

    # Build
    cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) >/dev/null 2>&1 || {
        echo -e "${RED}[ERROR]${NC} Build failed"
        return 1
    }

    # Build tools and create disk image
    if [[ -f "$PROJECT_DIR/tools/mkfs.viperfs.cpp" ]]; then
        # Compile mkfs.viperfs if needed
        if [[ ! -x "$PROJECT_DIR/tools/mkfs.viperfs" ]] || \
           [[ "$PROJECT_DIR/tools/mkfs.viperfs.cpp" -nt "$PROJECT_DIR/tools/mkfs.viperfs" ]]; then
            c++ -std=c++17 -O2 -o "$PROJECT_DIR/tools/mkfs.viperfs" \
                "$PROJECT_DIR/tools/mkfs.viperfs.cpp" 2>/dev/null || {
                echo -e "${RED}[ERROR]${NC} Failed to compile mkfs.viperfs"
                return 1
            }
        fi

        # Create disk image with proper directory structure
        # Layout: vinit.sys at root, everything else in /c/
        local mkfs_args=("$BUILD_DIR/disk.img" 8)
        mkfs_args+=("--add" "$BUILD_DIR/vinit.sys:vinit.sys")
        mkfs_args+=("--mkdir" "c")
        mkfs_args+=("--mkdir" "t")
        mkfs_args+=("--mkdir" "s")

        # Add user programs to c/ directory
        for prg in hello.prg fsd_smoke.prg netd_smoke.prg tls_smoke.prg edit.prg \
                   sftp.prg ssh.prg ping.prg fsinfo.prg netstat.prg sysinfo.prg \
                   devices.prg mathtest.prg faulttest_null.prg faulttest_illegal.prg; do
            if [[ -f "$BUILD_DIR/$prg" ]]; then
                mkfs_args+=("--add" "$BUILD_DIR/$prg:c/$prg")
            fi
        done

        # Add microkernel server binaries to c/ directory
        for server in blkd.sys netd.sys fsd.sys consoled.sys inputd.sys; do
            if [[ -f "$BUILD_DIR/$server" ]]; then
                mkfs_args+=("--add" "$BUILD_DIR/$server:c/$server")
            fi
        done

        "$PROJECT_DIR/tools/mkfs.viperfs" "${mkfs_args[@]}" >/dev/null 2>&1 || {
            echo -e "${RED}[ERROR]${NC} Failed to create disk image"
            return 1
        }

        # Create dedicated disk for user-space microkernel servers
        if [[ -f "$BUILD_DIR/blkd.sys" || -f "$BUILD_DIR/fsd.sys" ]]; then
            cp -f "$BUILD_DIR/disk.img" "$BUILD_DIR/microkernel.img"
        fi
    fi

    echo -e "${GREEN}[BUILD]${NC} Build complete"
    return 0
}

start_qemu() {
    local qemu
    qemu=$(find_qemu) || {
        echo -e "${RED}[ERROR]${NC} QEMU not found"
        return 1
    }

    log "Using QEMU: $qemu"

    # Verify required files exist
    if [[ ! -f "$BUILD_DIR/kernel.sys" ]]; then
        echo -e "${RED}[ERROR]${NC} kernel.sys not found"
        return 1
    fi

    if [[ ! -f "$BUILD_DIR/disk.img" ]]; then
        echo -e "${RED}[ERROR]${NC} disk.img not found"
        return 1
    fi

    # Create temp files
    SERIAL_OUTPUT=$(mktemp /tmp/viperos-test-output.XXXXXX)
    FIFO_IN=$(mktemp -u /tmp/viperos-test-fifo.XXXXXX)
    mkfifo "$FIFO_IN"

    log "Serial output: $SERIAL_OUTPUT"
    log "Input FIFO: $FIFO_IN"

    # Start QEMU in background
    # Use -nographic for pure serial I/O
    local qemu_opts=(
        -machine virt
        -cpu cortex-a72
        -m 128M
        -kernel "$BUILD_DIR/kernel.sys"
        -drive "file=$BUILD_DIR/disk.img,if=none,format=raw,id=disk0"
        -device virtio-blk-device,drive=disk0
        -device virtio-rng-device
        -netdev user,id=net0
        -device virtio-net-device,netdev=net0
        -nographic
        -no-reboot
    )

    # Add dedicated microkernel devices if available
    if [[ -f "$BUILD_DIR/microkernel.img" ]]; then
        qemu_opts+=(
            -drive "file=$BUILD_DIR/microkernel.img,if=none,format=raw,id=disk1"
            -device virtio-blk-device,drive=disk1
        )
    fi
    if [[ -f "$BUILD_DIR/netd.sys" ]]; then
        qemu_opts+=(
            -netdev user,id=net1
            -device virtio-net-device,netdev=net1
        )
    fi

    "$qemu" "${qemu_opts[@]}" < "$FIFO_IN" > "$SERIAL_OUTPUT" 2>&1 &

    QEMU_PID=$!
    log "QEMU started with PID: $QEMU_PID"

    # Keep FIFO open for writing
    exec 3>"$FIFO_IN"

    return 0
}

wait_for_pattern() {
    local pattern="$1"
    local timeout="${2:-30}"
    local start_time=$(date +%s)

    log "Waiting for pattern: '$pattern' (timeout: ${timeout}s)"

    while true; do
        if grep -q "$pattern" "$SERIAL_OUTPUT" 2>/dev/null; then
            log "Found pattern: '$pattern'"
            return 0
        fi

        local current_time=$(date +%s)
        if (( current_time - start_time >= timeout )); then
            log "Timeout waiting for pattern: '$pattern'"
            return 1
        fi

        # Check if QEMU is still running
        if ! kill -0 "$QEMU_PID" 2>/dev/null; then
            log "QEMU exited unexpectedly"
            return 1
        fi

        sleep 0.2
    done
}

send_command() {
    local cmd="$1"
    local delay="${2:-0.5}"

    log "Sending command: '$cmd'"

    # Send command character by character with small delay
    # This helps with QEMU input buffering
    echo "$cmd" >&3
    sleep "$delay"
}

get_output_after() {
    local marker="$1"
    local lines="${2:-50}"

    # Get output after the marker
    grep -A "$lines" "$marker" "$SERIAL_OUTPUT" 2>/dev/null | tail -n +"2"
}

# ============================================================================
# Test Functions
# ============================================================================

test_shell_starts() {
    log_test "Shell starts and shows prompt"

    if wait_for_pattern "SYS:>" 30; then
        pass "Shell started successfully"
        return 0
    else
        fail "Shell did not start" "SYS:> prompt" "$(tail -20 "$SERIAL_OUTPUT" 2>/dev/null)"
        return 1
    fi
}

test_assign_command() {
    log_test "ASSIGN command lists SYS: and D0:"

    send_command "Assign"
    sleep 1

    local output
    output=$(cat "$SERIAL_OUTPUT")

    local has_sys=false
    local has_d0=false

    if echo "$output" | grep -q "SYS:"; then
        has_sys=true
    fi

    if echo "$output" | grep -q "D0:"; then
        has_d0=true
    fi

    if [[ "$has_sys" == true && "$has_d0" == true ]]; then
        pass "ASSIGN shows SYS: and D0:"
        return 0
    else
        local got=""
        [[ "$has_sys" == true ]] && got+="SYS: found "
        [[ "$has_d0" == true ]] && got+="D0: found"
        [[ -z "$got" ]] && got="neither SYS: nor D0: found"
        fail "ASSIGN missing assigns" "SYS: and D0:" "$got"
        return 1
    fi
}

test_dir_root() {
    log_test "DIR / shows vinit.sys"

    send_command "Dir /"
    sleep 1

    local output
    output=$(cat "$SERIAL_OUTPUT")

    if echo "$output" | grep -qi "vinit"; then
        pass "DIR / shows vinit.sys"
        return 0
    else
        fail "DIR / missing vinit.sys" "vinit.sys in listing" "not found"
        return 1
    fi
}

test_makedir() {
    log_test "MAKEDIR creates directory and DIR shows it"

    # Create a test directory
    send_command "MakeDir /testdir"
    sleep 1

    local output
    output=$(cat "$SERIAL_OUTPUT")

    # Check if directory was created
    if echo "$output" | grep -qi "Created directory"; then
        log "Directory creation confirmed"
    fi

    # List directory to verify
    send_command "Dir /"
    sleep 1

    output=$(cat "$SERIAL_OUTPUT")

    if echo "$output" | grep -qi "testdir"; then
        pass "MAKEDIR creates directory visible in DIR"
        return 0
    else
        fail "MAKEDIR directory not visible" "testdir in DIR listing" "not found"
        return 1
    fi
}

test_version_command() {
    log_test "VERSION command shows v0.2.0"

    send_command "Version"
    sleep 1

    local output
    output=$(cat "$SERIAL_OUTPUT")

    if echo "$output" | grep -q "0.2.0"; then
        pass "VERSION shows v0.2.0"
        return 0
    else
        fail "VERSION missing version" "0.2.0" "$(echo "$output" | grep -i version | tail -1)"
        return 1
    fi
}

test_help_command() {
    log_test "HELP command shows available commands"

    send_command "Help"
    sleep 1

    local output
    output=$(cat "$SERIAL_OUTPUT")

    # Check for some expected commands in help output
    local has_dir=false
    local has_assign=false
    local has_fetch=false

    echo "$output" | grep -qi "Dir" && has_dir=true
    echo "$output" | grep -qi "Assign" && has_assign=true
    echo "$output" | grep -qi "Fetch" && has_fetch=true

    if [[ "$has_dir" == true && "$has_assign" == true && "$has_fetch" == true ]]; then
        pass "HELP shows expected commands"
        return 0
    else
        fail "HELP missing commands" "Dir, Assign, Fetch" "incomplete help output"
        return 1
    fi
}

test_echo_command() {
    log_test "ECHO command prints text"

    local test_string="TEST_MARKER_12345"
    send_command "Echo $test_string"
    sleep 1

    local output
    output=$(cat "$SERIAL_OUTPUT")

    if echo "$output" | grep -q "$test_string"; then
        pass "ECHO prints text correctly"
        return 0
    else
        fail "ECHO output not found" "$test_string" "not found in output"
        return 1
    fi
}

test_uptime_command() {
    log_test "UPTIME command shows uptime"

    send_command "Uptime"
    sleep 1

    local output
    output=$(cat "$SERIAL_OUTPUT")

    # Uptime should show some time indication
    if echo "$output" | grep -qiE "(second|minute|hour|uptime|[0-9]+s)"; then
        pass "UPTIME shows time information"
        return 0
    else
        fail "UPTIME output unexpected" "time information" "$(echo "$output" | tail -5)"
        return 1
    fi
}

test_avail_command() {
    log_test "AVAIL command shows memory info"

    send_command "Avail"
    sleep 1

    local output
    output=$(cat "$SERIAL_OUTPUT")

    # Should show memory statistics
    if echo "$output" | grep -qiE "(memory|free|used|pages|[0-9]+ (KB|MB))"; then
        pass "AVAIL shows memory information"
        return 0
    else
        fail "AVAIL output unexpected" "memory statistics" "$(echo "$output" | tail -5)"
        return 1
    fi
}

test_https_fetch() {
    log_test "HTTPS Fetch works (example.com)"

    # Note: This test requires network access through QEMU user mode networking
    # The fetch may fail due to DNS or network issues, which is acceptable in CI

    send_command "Fetch https://example.com"
    sleep 10  # Give time for DNS resolution and TLS handshake

    local output
    output=$(cat "$SERIAL_OUTPUT")

    # Check for various success indicators
    if echo "$output" | grep -qi "TLS handshake complete"; then
        pass "HTTPS TLS handshake succeeded"
        return 0
    elif echo "$output" | grep -qi "Received.*bytes"; then
        pass "HTTPS fetch received data"
        return 0
    elif echo "$output" | grep -qi "<!doctype html\|<html"; then
        pass "HTTPS fetch returned HTML"
        return 0
    elif echo "$output" | grep -qi "DNS.*failed\|resolve.*failed"; then
        # DNS failure is acceptable in some CI environments
        echo -e "${YELLOW}[SKIP]${NC} HTTPS test skipped (DNS unavailable)"
        return 0
    else
        fail "HTTPS fetch failed" "TLS handshake or HTML response" "$(echo "$output" | grep -iE "(fetch|tls|error|fail)" | tail -5)"
        return 1
    fi
}

test_path_command() {
    log_test "PATH command resolves assigns"

    send_command "Path SYS:vinit.sys"
    sleep 1

    local output
    output=$(cat "$SERIAL_OUTPUT")

    if echo "$output" | grep -qiE "(vinit|inode|resolve)"; then
        pass "PATH resolves assign paths"
        return 0
    else
        fail "PATH command failed" "path resolution info" "$(echo "$output" | tail -5)"
        return 1
    fi
}

test_run_command() {
    log_test "RUN command spawns hello.prg (malloc test)"

    send_command "Run /hello.prg"
    sleep 5  # Give time for malloc tests to complete

    local output
    output=$(cat "$SERIAL_OUTPUT")

    # Check for malloc test output
    if echo "$output" | grep -q "All tests PASSED"; then
        pass "RUN spawned hello.prg - malloc tests PASSED"
        return 0
    elif echo "$output" | grep -q "\[malloc_test\]"; then
        # Check if tests are running
        if echo "$output" | grep -qi "FAILED"; then
            fail "RUN: malloc test FAILED" "All tests PASSED" "$(echo "$output" | grep -i FAILED | tail -1)"
            return 1
        else
            # Tests running, wait a bit more
            sleep 3
            output=$(cat "$SERIAL_OUTPUT")
            if echo "$output" | grep -q "All tests PASSED"; then
                pass "RUN spawned hello.prg - malloc tests PASSED"
                return 0
            else
                fail "RUN: malloc test incomplete" "All tests PASSED" "tests running but not completed"
                return 1
            fi
        fi
    elif echo "$output" | grep -q "Started process"; then
        # Legacy hello output or waiting for output
        if echo "$output" | grep -q "Hello from spawned process"; then
            pass "RUN spawned hello.prg successfully (legacy output)"
            return 0
        fi
        fail "RUN started process but output not found" "malloc test output" "process started but no test output"
        return 1
    elif echo "$output" | grep -qi "failed to spawn"; then
        fail "RUN failed to spawn hello.prg" "successful spawn" "spawn failed"
        return 1
    else
        fail "RUN command unexpected result" "malloc_test output" "$(echo "$output" | tail -10)"
        return 1
    fi
}

# ============================================================================
# Main Test Runner
# ============================================================================

run_tests() {
    echo ""
    echo -e "${BLUE}[INFO]${NC} Running tests..."
    echo ""

    # Wait for shell to start
    if ! test_shell_starts; then
        echo -e "${RED}[ERROR]${NC} Shell failed to start, cannot run tests"
        if [[ "$VERBOSE" == true ]]; then
            echo ""
            echo "=== Serial Output ==="
            cat "$SERIAL_OUTPUT"
            echo "===================="
        fi
        return 1
    fi

    # Run individual tests
    test_version_command
    test_help_command
    test_echo_command
    test_assign_command
    test_dir_root
    test_makedir
    test_uptime_command
    test_avail_command
    test_path_command
    test_run_command
    test_https_fetch

    # Exit shell gracefully
    send_command "Exit"
    sleep 1
}

print_summary() {
    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo -e "  Tests Run:    ${TESTS_RUN}"
    echo -e "  ${GREEN}Passed:${NC}       ${TESTS_PASSED}"
    echo -e "  ${RED}Failed:${NC}       ${TESTS_FAILED}"
    echo "════════════════════════════════════════════════════════════"

    if [[ $TESTS_FAILED -eq 0 ]]; then
        echo ""
        echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
        echo -e "${GREEN}║                    ALL TESTS PASSED                       ║${NC}"
        echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
        echo ""
        return 0
    else
        echo ""
        echo -e "${RED}╔═══════════════════════════════════════════════════════════╗${NC}"
        echo -e "${RED}║                  SOME TESTS FAILED                        ║${NC}"
        echo -e "${RED}╚═══════════════════════════════════════════════════════════╝${NC}"
        echo ""
        return 1
    fi
}

main() {
    print_header

    # Build if needed
    if ! build_viperos; then
        echo -e "${RED}[ERROR]${NC} Build failed"
        exit 2
    fi

    echo ""
    echo -e "${BLUE}[INFO]${NC} Starting QEMU..."

    # Start QEMU
    if ! start_qemu; then
        echo -e "${RED}[ERROR]${NC} Failed to start QEMU"
        exit 2
    fi

    # Run tests
    run_tests

    # Print summary
    print_summary
    local summary_result=$?

    # Show verbose output on failure
    if [[ $TESTS_FAILED -gt 0 && "$VERBOSE" != true ]]; then
        echo "Run with --verbose to see full serial output"
    fi

    if [[ "$VERBOSE" == true && $TESTS_FAILED -gt 0 ]]; then
        echo ""
        echo "=== Full Serial Output ==="
        cat "$SERIAL_OUTPUT"
        echo "=========================="
    fi

    # Exit with appropriate code
    if [[ $TESTS_FAILED -gt 0 ]]; then
        exit 1
    fi

    exit 0
}

main "$@"
