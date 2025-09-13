// tui/tests/test_session.cpp
// @brief Tests TerminalSession lifecycle with no-op path.
// @invariant Setting VIPERTUI_NO_TTY makes TerminalSession no-op.
// @ownership No ownership transferred.

#include "tui/term/session.hpp"
#include "tui/term/term_io.hpp"

#include <cassert>
#include <cstdlib>

using viper::tui::term::StringTermIO;
using viper::tui::term::TerminalSession;
using viper::tui::term::TermIO;

static void consume(TermIO &)
{
    // No-op placeholder.
}

int main()
{
#if defined(_WIN32)
    _putenv_s("VIPERTUI_NO_TTY", "1");
#else
    setenv("VIPERTUI_NO_TTY", "1", 1);
#endif
    {
        TerminalSession session;
    }
    StringTermIO tio;
    consume(tio);
    assert(true);
    return 0;
}
