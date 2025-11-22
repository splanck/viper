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
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
#include <pty.h>
#elif defined(__APPLE__)
#include <util.h>
#else
#error "openpty is required for RTTermColorTests"
#endif

namespace
{

std::string capture_sgr(int fg, int bg)
{
    int master = -1;
    int slave = -1;
    int rc = openpty(&master, &slave, nullptr, nullptr, nullptr);
    assert(rc == 0);

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

    close(slave);

    std::string result;
    char buf[64];
    ssize_t n = 0;
    while ((n = read(master, buf, sizeof(buf))) > 0)
    {
        result.append(buf, static_cast<size_t>(n));
        if (n < static_cast<ssize_t>(sizeof(buf)))
        {
            break;
        }
    }

    close(master);

    int status = 0;
    waitpid(pid, &status, 0);

    return result;
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
