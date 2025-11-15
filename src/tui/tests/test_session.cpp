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
