//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTInputNumericFailTests.cpp
// Purpose: Ensure INPUT-style numeric parsing traps when trailing junk appears.
// Key invariants: rt_to_double rejects non-whitespace suffixes and reports the INPUT trap.
// Ownership/Lifetime: Uses runtime helpers directly.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include "tests/common/PosixCompat.h"
#include "tests/common/WaitCompat.hpp"
#include <cassert>
#include <string>

namespace
{
void expect_input_failure(const char *literal)
{
    int pipes[2];
    assert(pipe(pipes) == 0);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        close(pipes[0]);
        dup2(pipes[1], 2);
        rt_to_double(rt_const_cstr(literal));
        _exit(0);
    }

    close(pipes[1]);
    char buffer[256];
    ssize_t n = read(pipes[0], buffer, sizeof(buffer) - 1);
    if (n < 0)
        n = 0;
    buffer[n] = '\0';
    close(pipes[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    std::string stderrOutput(buffer);
    bool trapped = stderrOutput.find("INPUT: expected numeric value") != std::string::npos;
    assert(trapped);
}
} // namespace

int main()
{
    expect_input_failure("12abc");
    expect_input_failure("7.5foo");
    return 0;
}
