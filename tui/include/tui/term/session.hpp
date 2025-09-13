// tui/include/tui/term/session.hpp
// @brief RAII for terminal mode management.
// @invariant Restores terminal state on destruction when active.
// @ownership Owns saved termios state when active.
#pragma once

#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#endif

namespace viper::tui::term
{
class TerminalSession
{
  public:
    TerminalSession();
    ~TerminalSession();

    TerminalSession(const TerminalSession &) = delete;
    TerminalSession &operator=(const TerminalSession &) = delete;

  private:
#if defined(__unix__) || defined(__APPLE__)
    termios orig_{};
    bool active_{false};
#endif
};
} // namespace viper::tui::term
