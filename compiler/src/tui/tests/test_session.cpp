//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_session.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/term/session.hpp"
#include <cassert>
#include <cstdlib>

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
    set_no_tty_env();              // ensure CI-safe no-op
    viper::tui::TerminalSession s; // should not throw or exit raw mode paths
    assert(!s.active());           // in CI/headless we expect inactive
    return 0;
}
