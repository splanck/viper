// tui/src/term/session.cpp
// @brief Implements TerminalSession for managing terminal modes.
// @invariant Restores prior terminal state on destruction when active.
// @ownership Does not own the underlying terminal.

#include "tui/term/session.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#endif

namespace viper::tui::term
{
#if defined(__unix__) || defined(__APPLE__)
TerminalSession::TerminalSession() noexcept
{
    const char *no_tty = std::getenv("VIPERTUI_NO_TTY");
    if (no_tty && no_tty[0] == '1')
    {
        active_ = false;
        return;
    }

    if (tcgetattr(STDIN_FILENO, &old_termios_) != 0)
    {
        active_ = false;
        return;
    }

    struct termios raw = old_termios_;
    cfmakeraw(&raw);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
    {
        active_ = false;
        return;
    }

    std::fputs("\x1b[?1049h\x1b[?2004h\x1b[?25l", stdout);
    std::fflush(stdout);
    active_ = true;
}

TerminalSession::~TerminalSession()
{
    if (!active_)
    {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios_);
    std::fputs("\x1b[?1049l\x1b[?2004l\x1b[?25h", stdout);
    std::fflush(stdout);
}
#else
TerminalSession::TerminalSession() noexcept = default;
TerminalSession::~TerminalSession() = default;
#endif
} // namespace viper::tui::term
