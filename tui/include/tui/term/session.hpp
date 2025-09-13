// tui/include/tui/term/session.hpp
// @brief RAII wrapper for terminal session state (raw mode, alt screen).
// @invariant Restores terminal state on destruction; no-op if VIPERTUI_NO_TTY=1 or non-POSIX.
// @ownership Does not own the terminal; manipulates stdio state only when active.
#pragma once

#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#endif

namespace viper::tui::term
{
/// @brief Manages a terminal session by entering raw mode and enabling UI features.
/// @details On construction, the session switches the terminal to raw mode,
///          enables the alternate screen buffer, enables bracketed paste, and hides the cursor.
///          On destruction, the original terminal settings are restored.
///          If the environment variable `VIPERTUI_NO_TTY` is set to `1`,
///          or on non-POSIX platforms, the session performs no operations.
/// @invariant Terminal state is restored when the object is destroyed.
/// @ownership Does not own the underlying terminal.
class TerminalSession
{
  public:
    /// @brief Construct a terminal session.
    /// @invariant No-op when `VIPERTUI_NO_TTY=1` or unsupported platform.
    TerminalSession() noexcept;

    /// @brief Restore terminal state.
    ~TerminalSession();

    TerminalSession(const TerminalSession &) = delete;
    TerminalSession &operator=(const TerminalSession &) = delete;

  private:
#if defined(__unix__) || defined(__APPLE__)
    bool active_{false};
    struct termios old_termios_{};
#endif
};

} // namespace viper::tui::term
