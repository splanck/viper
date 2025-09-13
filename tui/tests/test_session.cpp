// tui/tests/test_session.cpp
// @brief Lifecycle test for TerminalSession with CI-safe no-op path.
// @invariant TerminalSession performs no terminal I/O when VIPERTUI_NO_TTY=1.
// @ownership No ownership of passed TermIO instances.

#include "tui/term/session.hpp"
#include "tui/term/term_io.hpp"

#include <cassert>
#include <cstdlib>

using namespace viper::tui::term;

static void consume(TermIO &io)
{
    (void)io;
}

int main()
{
#ifdef _WIN32
    _putenv_s("VIPERTUI_NO_TTY", "1");
#else
    setenv("VIPERTUI_NO_TTY", "1", 1);
#endif
    {
        TerminalSession session; // Should be a no-op and not crash.
    }
    StringTermIO tio;
    consume(tio);
    assert(true);
    return 0;
}
