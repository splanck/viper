#include "tui/term/session.hpp"
#include "tui/term/term_io.hpp"

namespace tui
{

static inline bool env_no_tty()
{
    const char *v = std::getenv("VIPERTUI_NO_TTY");
    return v && *v == '1';
}

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
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE)
    {
        if (GetConsoleMode(hOut, &orig_out_mode_))
        {
            DWORD mode = orig_out_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }
#endif

    term::RealTermIO io;
    io.write("\x1b[?1049h\x1b[?2004h\x1b[?25l"); // alt screen, bracketed paste on, cursor hide
    io.flush();
    active_ = true;
}

TerminalSession::~TerminalSession()
{
    if (!active_)
        return;

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
#endif
}

} // namespace tui
