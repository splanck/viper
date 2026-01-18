//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Establishes and tears down scoped terminal sessions for the Viper TUI.
// The helper transparently switches the terminal into an alternate screen,
// enables raw input, and optionally activates mouse reporting so interactive
// widgets can respond to low-level events.  All configuration is guarded by
// environment variables so automated tests can opt-out without stubbing the
// terminal APIs.  Destruction restores every mutated setting, guaranteeing
// host terminals are left in their original state.
//
// Invariants:
//   * A session is marked inactive when any platform call fails; subsequent
//     clean-up steps are skipped to avoid interacting with uninitialised state.
//   * Alternate screen and mouse reporting are toggled in balanced pairs so the
//     outer application never has to track nested usage.
// Ownership:
//   * TerminalSession instances own no external resources; they merely mutate
//     global process terminal flags and revert them on destruction.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements scoped terminal configuration for the Viper TUI runtime.
/// @details Functions in this unit wrap the platform-specific terminal APIs
///          needed to enable raw mode, alternate screen buffers, and optional
///          mouse support.  The RAII wrapper allows higher layers to opt in to a
///          session without littering code with `#if` blocks or manual cleanup.

#include "tui/term/session.hpp"
#include "tui/term/term_io.hpp"

namespace viper::tui
{

/// @brief Determine whether terminal interaction is globally disabled.
/// @details Reads the `VIPERTUI_NO_TTY` environment variable and returns true
///          when it is explicitly set to `1`.  The opt-out is honoured across
///          all platforms so unit tests and headless environments can run the
///          TUI code paths without touching real terminals.
/// @return True when the caller should avoid configuring the terminal.
static inline bool env_no_tty()
{
    const char *v = std::getenv("VIPERTUI_NO_TTY");
    return v && *v == '1';
}

/// @brief Read a boolean environment flag in a human-friendly way.
/// @details Accepts the common truthy characters (`1`, `y`, `Y`, `t`, `T`) so
///          users can enable optional features—such as mouse support—without
///          remembering exact casing.  Missing variables simply evaluate to
///          false which keeps feature toggles opt-in.
/// @param name Environment variable to query.
/// @return True when the variable exists and encodes a truthy value.
static inline bool env_true(const char *name)
{
    const char *v = std::getenv(name);
    if (!v)
        return false;
    return (*v == '1') || (*v == 'y') || (*v == 'Y') || (*v == 't') || (*v == 'T');
}

/// @brief Establish a scoped terminal session with raw input and alternate screen.
/// @details The constructor short-circuits when the `VIPERTUI_NO_TTY` flag is
///          set or when the active stdin stream is not a terminal.  On POSIX
///          systems it stores the current `termios` configuration, enables raw
///          mode, and ensures reads block for at least one byte.  On Windows it
///          toggles `ENABLE_VIRTUAL_TERMINAL_PROCESSING` so ANSI escape sequences
///          are honoured.  Successful setup pushes the terminal into the
///          alternate screen buffer, hides the cursor, and optionally enables
///          mouse reporting when requested via `VIPERTUI_MOUSE`.
TerminalSession::TerminalSession()
{
    if (env_no_tty())
    {
        active_ = false;
        return;
    }

#if VIPERTUI_POSIX
    if (!isatty(STDIN_FILENO))
    {
        active_ = false;
        return;
    }
    if (tcgetattr(STDIN_FILENO, &orig_) != 0)
    {
        active_ = false;
        return;
    }
    termios raw = orig_;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0)
    {
        active_ = false;
        return;
    }
#endif

#if defined(_WIN32)
    // Configure stdout for virtual terminal processing (ANSI escape sequences)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE)
    {
        if (GetConsoleMode(hOut, &orig_out_mode_))
        {
            DWORD mode = orig_out_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }

    // Configure stdin for raw input with virtual terminal input (ANSI sequences)
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn && hIn != INVALID_HANDLE_VALUE)
    {
        if (GetConsoleMode(hIn, &orig_in_mode_))
        {
            // Enable virtual terminal input to receive ANSI escape sequences
            // Disable line input, echo, and processed input for raw mode
            DWORD mode = orig_in_mode_;
            mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
            mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
            SetConsoleMode(hIn, mode);
        }
    }
#endif

    term::RealTermIO io;
    io.write("\x1b[?1049h\x1b[?2004h\x1b[?25l"); // alt screen, bracketed paste on, cursor hide
    io.flush();

    if (!env_no_tty() && env_true("VIPERTUI_MOUSE"))
    {
        ::viper::tui::term::RealTermIO mouse_io;
        mouse_io.write("\x1b[?1000h\x1b[?1002h\x1b[?1006h"); // enable mouse + SGR
        mouse_io.flush();
    }

    active_ = true;
}

/// @brief Restore the host terminal to its original settings.
/// @details When the session successfully activated, the destructor disables
///          mouse reporting (if previously enabled), leaves the alternate screen
///          buffer, re-enables bracketed paste, and makes the cursor visible.
///          Platform-specific state such as POSIX `termios` or the Windows
///          console mode is reinstated to its captured value.  Inactive sessions
///          skip all operations to avoid altering unrelated terminal state.
TerminalSession::~TerminalSession()
{
    if (!active_)
        return;

    if (!env_no_tty() && env_true("VIPERTUI_MOUSE"))
    {
        ::viper::tui::term::RealTermIO io;
        io.write("\x1b[?1006l\x1b[?1002l\x1b[?1000l"); // disable SGR + motion + mouse
        io.flush();
    }

    term::RealTermIO io;
    io.write("\x1b[?1049l\x1b[?2004l\x1b[?25h"); // leave alt screen, disable paste, show cursor
    io.flush();

#if VIPERTUI_POSIX
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
#endif
#if defined(_WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE)
    {
        SetConsoleMode(hOut, orig_out_mode_);
    }
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn && hIn != INVALID_HANDLE_VALUE)
    {
        SetConsoleMode(hIn, orig_in_mode_);
    }
#endif
}

/// @brief Indicate whether the terminal session modified the terminal state.
/// @details Returns true only when the constructor succeeded in applying all
///          configuration steps.  Callers can use this to decide whether to
///          emit escape sequences or to fall back to non-interactive output.
/// @return True when the session successfully initialized and remains active.
bool TerminalSession::active() const
{
    return active_;
}

} // namespace viper::tui
