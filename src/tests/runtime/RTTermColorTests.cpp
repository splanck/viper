//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTermColorTests.cpp
// Purpose: Verify rt_term_color_i32 emits correct SGR codes for bright backgrounds.
// Key invariants: Background values 8-15 map to ANSI 100-107 without using 48;5.
// Ownership/Lifetime: Runtime library tests.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"

#include <cassert>
#include <cstdio>

#ifdef _WIN32
// This test requires PTY support which is not available on Windows
int main()
{
    printf("Test skipped: PTY not available on Windows\n");
    return 0;
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

namespace
{

std::string capture_sgr_once(int fg, int bg)
{
    int master = -1;
    int slave = -1;
    int rc = openpty(&master, &slave, nullptr, nullptr, nullptr);
    assert(rc == 0);

    // Set PTY to raw mode - no buffering, no echo, no processing
    struct termios tio;
    tcgetattr(slave, &tio);
    cfmakeraw(&tio);
    tcsetattr(slave, TCSANOW, &tio);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0)
    {
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

    while (true)
    {
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
std::string capture_sgr(int fg, int bg)
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
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

} // namespace

int main()
{
    // Probe PTY availability; skip test gracefully when unavailable (e.g., sandboxed macOS).
#if defined(__linux__) || defined(__APPLE__)
    {
        int m = -1, s = -1;
        if (openpty(&m, &s, nullptr, nullptr, nullptr) != 0)
        {
            std::fprintf(stderr,
                         "Skipping RTTermColorTests: openpty unavailable in this environment\n");
            return 0; // mark as skipped/passed in constrained environments
        }
        close(m);
        close(s);
    }
#endif

    std::string no_change = capture_sgr(-1, -1);
    assert(no_change.empty());

    for (int bg = 8; bg <= 15; ++bg)
    {
        std::string sgr = capture_sgr(-1, bg);
        std::string expected = "\x1b[" + std::to_string(100 + (bg - 8)) + "m";
        assert(sgr == expected);
        assert(sgr.find("48;5") == std::string::npos);
    }

    std::string combined = capture_sgr(8, 8);
    assert(combined == "\x1b[1;30;100m");

    return 0;
}
#endif // !_WIN32
