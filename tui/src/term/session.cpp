// tui/src/term/session.cpp
// @brief Implements TerminalSession RAII.
// @invariant Restores terminal state on destruction when active.
// @ownership Owns saved termios state when active.

#include "tui/term/session.hpp"
#include "tui/term/term_io.hpp"

#include <cstdlib>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace viper::tui::term
{
#if defined(__unix__) || defined(__APPLE__)
TerminalSession::TerminalSession()
{
    if (std::getenv("VIPERTUI_NO_TTY") != nullptr)
    {
        return;
    }

    if (tcgetattr(STDIN_FILENO, &orig_) != 0)
    {
        return;
    }

    termios raw = orig_;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    RealTermIO io;
    io.write("\x1b[?1049h\x1b[?2004h\x1b[?25l");
    io.flush();
    active_ = true;
}

TerminalSession::~TerminalSession()
{
    if (!active_)
    {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);

    RealTermIO io;
    io.write("\x1b[?1049l\x1b[?2004l\x1b[?25h");
    io.flush();
}
#else
TerminalSession::TerminalSession() = default;
TerminalSession::~TerminalSession() = default;
#endif
} // namespace viper::tui::term
