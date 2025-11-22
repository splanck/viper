//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_demo_headless.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdlib>
#include <string>

static void set_no_tty_env()
{
#if defined(_WIN32)
    _putenv_s("VIPERTUI_NO_TTY", "1");
#else
    setenv("VIPERTUI_NO_TTY", "1", 1);
#endif
}

int main()
{
    set_no_tty_env();
#if defined(_WIN32)
    const std::string cmd = "..\\apps\\tui_demo.exe";
#else
    const std::string cmd = "../apps/tui_demo";
#endif
    int rc = std::system(cmd.c_str());
    assert(rc == 0);
    return 0;
}
