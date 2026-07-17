//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTermColorTests.cpp
// Purpose: Verify rt_term_color_i32 emits correct SGR codes for bright backgrounds.
// Key invariants:
//   - Background values 8-15 map to ANSI 100-107 without using 48;5.
//   - PTY capture uses only portable POSIX termios flags.
// Ownership/Lifetime:
//   - Each capture owns and closes its PTY file descriptors.
//   - The forked child exits after emitting one SGR sequence.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "tests/common/PlatformSkip.h"

#include <cassert>
#include <cstdio>

#ifdef _WIN32
// This test requires PTY support which is not available on Windows
int main() {
    ZANNA_PLATFORM_SKIP("PTY not available on Windows");
}
#else
// POSIX-only implementation
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__linux__)
#include <pty.h>
#elif defined(__APPLE__)
#include <util.h>
#endif

namespace {

void make_raw_termios(struct termios &tio) {
    tio.c_iflag &=
        static_cast<tcflag_t>(~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON));
    tio.c_oflag &= static_cast<tcflag_t>(~OPOST);
    tio.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL | ICANON | ISIG | IEXTEN));
    tio.c_cflag &= static_cast<tcflag_t>(~(CSIZE | PARENB));
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
}

std::string capture_sgr_once(int fg, int bg) {
    int master = -1;
    int slave = -1;
    int rc = openpty(&master, &slave, nullptr, nullptr, nullptr);
    assert(rc == 0);

    // Set PTY to raw mode - no buffering, no echo, no processing
    struct termios tio;
    tcgetattr(slave, &tio);
    make_raw_termios(tio);
    tcsetattr(slave, TCSANOW, &tio);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        close(master);
        dup2(slave, STDOUT_FILENO);
        close(slave);
        rt_term_color_i32(fg, bg);
        _exit(0);
    }

    // Parent: close slave so read() gets EOF when child exits
    close(slave);

    // Read from master until EOF (child exit closes slave -> master gets EOF)
    // Use blocking read - this is reliable because we closed the slave in parent
    std::string result;
    char buf[64];

    while (true) {
        ssize_t n = read(master, buf, sizeof(buf));
        if (n <= 0)
            break; // EOF or error
        result.append(buf, static_cast<size_t>(n));
    }

    // Reap child
    int status = 0;
    waitpid(pid, &status, 0);

    close(master);
    return result;
}

// PTY operations can be flaky on macOS - retry up to 3 times
std::string capture_sgr(int fg, int bg) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::string result = capture_sgr_once(fg, bg);
        // For empty expected output (fg=-1, bg=-1), empty is correct
        // For non-empty expected output, retry if we got nothing
        if (fg == -1 && bg == -1)
            return result; // Empty is expected
        if (!result.empty())
            return result;
        // Got empty when we expected data - retry
    }
    return ""; // All retries failed
}

// Capture the SGR bytes emitted by the i64 public wrapper (rt_term_color),
// which clamps its parameters before narrowing (VDOC-221).
std::string capture_sgr_i64_once(int64_t fg, int64_t bg) {
    int master = -1;
    int slave = -1;
    int rc = openpty(&master, &slave, nullptr, nullptr, nullptr);
    assert(rc == 0);
    struct termios tio;
    tcgetattr(slave, &tio);
    make_raw_termios(tio);
    tcsetattr(slave, TCSANOW, &tio);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(master);
        dup2(slave, STDOUT_FILENO);
        close(slave);
        rt_term_color(fg, bg); // i64 public wrapper (clamps)
        _exit(0);
    }
    close(slave);
    std::string result;
    char buf[64];
    while (true) {
        ssize_t n = read(master, buf, sizeof(buf));
        if (n <= 0)
            break;
        result.append(buf, static_cast<size_t>(n));
    }
    int status = 0;
    waitpid(pid, &status, 0);
    close(master);
    return result;
}

std::string capture_sgr_i64(int64_t fg, int64_t bg) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::string r = capture_sgr_i64_once(fg, bg);
        if (!r.empty())
            return r;
    }
    return "";
}

} // namespace

int main() {
    // Probe PTY availability; skip test gracefully when unavailable (e.g., sandboxed macOS).
#if defined(__linux__) || defined(__APPLE__)
    {
        int m = -1, s = -1;
        if (openpty(&m, &s, nullptr, nullptr, nullptr) != 0) {
            ZANNA_PLATFORM_SKIP("openpty unavailable in this environment");
        }
        close(m);
        close(s);
    }
#endif

    std::string no_change = capture_sgr(-1, -1);
    assert(no_change.empty());

    for (int bg = 8; bg <= 15; ++bg) {
        std::string sgr = capture_sgr(-1, bg);
        std::string expected = "\x1b[" + std::to_string(100 + (bg - 8)) + "m";
        assert(sgr == expected);
        assert(sgr.find("48;5") == std::string::npos);
    }

    std::string combined = capture_sgr(8, 8);
    assert(combined == "\x1b[1;30;100m");

    // VDOC-221: a foreground value of 2^31 truncates to INT32_MIN (negative)
    // under the old i32 path, which `rt_term_color_i32` rejects (`fg < -1`) and
    // emits nothing. The i64 wrapper clamps it to INT32_MAX (positive) first, so
    // a valid SGR is still emitted (non-empty) instead of being silently dropped
    // by a sign flip. The equivalent small value 8 emits a real code.
    std::string clamped = capture_sgr_i64(2147483648LL, -1);
    assert(!clamped.empty());
    // The wrapper matches the i32 path for in-range values.
    std::string small = capture_sgr_i64(8, 8);
    assert(small == "\x1b[1;30;100m");

    return 0;
}
#endif // !_WIN32
