//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC runtime's terminal helpers.  These functions provide
// portable console control for clearing the screen, changing colours, moving the
// cursor, and reading single-key input in blocking and non-blocking modes.  The
// implementation hides platform-specific quirks—such as enabling virtual
// terminal processing on Windows—so higher-level runtime code can rely on a
// single consistent behaviour.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Terminal control and key-input helpers for the BASIC runtime.
/// @details Exposes functions used by BASIC statements like `CLS`, `COLOR`,
///          `LOCATE`, `GETKEY$`, and `INKEY$`.  The helpers only emit ANSI escape
///          sequences when stdout is attached to a terminal and fall back to
///          runtime traps for invalid usage.

#include "rt.hpp"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <conio.h>
#include <io.h>
#include <windows.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

/// @brief Determine whether stdout is attached to a terminal.
/// @details Guards terminal escape emission so batch output (e.g. redirected to
///          a file) remains free of ANSI sequences.
static int stdout_isatty(void)
{
    int fd = fileno(stdout);
    return (fd >= 0) && isatty(fd);
}

#if defined(_WIN32)
/// @brief Enable ANSI escape sequence processing on Windows consoles.
/// @details Lazily toggles the `ENABLE_VIRTUAL_TERMINAL_PROCESSING` flag the
///          first time terminal output is requested so subsequent writes honour
///          colour and cursor positioning sequences.
static void enable_vt(void)
{
    static int once = 0;
    if (once)
        return;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode))
        {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(h, mode);
        }
    }
    once = 1;
}
#endif

/// @brief Emit a raw string to stdout, enabling ANSI support when available.
/// @details Wraps `fputs` and `fflush` so terminal helpers can centralise VT
///          enablement and flushing behaviour.
static void out_str(const char *s)
{
    if (!s)
        return;
#if defined(_WIN32)
    enable_vt();
#endif
    fputs(s, stdout);
    fflush(stdout);
}

/// @brief Emit an SGR escape sequence for the requested foreground/background.
/// @details Converts BASIC colour codes into ANSI escape sequences, supporting
///          normal, bright, and 256-colour modes.  Negative parameters leave the
///          corresponding channel unchanged.
static void sgr_color(int fg, int bg)
{
    if (fg < 0 && bg < 0)
    {
        return;
    }
    char buf[64];
    int n = 0, wrote = 0;

    buf[n++] = '\x1b';
    buf[n++] = '[';

    if (fg >= 0)
    {
        if (fg <= 7)
        {
            n += snprintf(buf + n, sizeof(buf) - n, "%d", 30 + fg);
        }
        else if (fg <= 15)
        {
            n += snprintf(buf + n, sizeof(buf) - n, "1;%d", 30 + (fg - 8));
        }
        else
        {
            n += snprintf(buf + n, sizeof(buf) - n, "38;5;%d", fg);
        }
        wrote = 1;
    }
    if (bg >= 0)
    {
        if (wrote)
            buf[n++] = ';';
        if (bg <= 7)
        {
            n += snprintf(buf + n, sizeof(buf) - n, "%d", 40 + bg);
        }
        else if (bg <= 15)
        {
            n += snprintf(buf + n, sizeof(buf) - n, "%d", 100 + (bg - 8));
        }
        else
        {
            n += snprintf(buf + n, sizeof(buf) - n, "48;5;%d", bg);
        }
    }
    buf[n++] = 'm';
    buf[n] = '\0';
    out_str(buf);
}

/// @brief Clear the terminal display when stdout is interactive.
/// @details Emits the ANSI sequence for clearing the screen and homing the
///          cursor.  No output is produced when stdout is redirected.
void rt_term_cls(void)
{
    if (!stdout_isatty())
        return;
    out_str("\x1b[2J\x1b[H");
}

/// @brief Adjust terminal foreground/background colours using BASIC codes.
/// @details Validates the colour range and forwards to @ref sgr_color when
///          stdout is a terminal.  Negative parameters leave the colour
///          unchanged to mirror BASIC's semantics.
void rt_term_color_i32(int32_t fg, int32_t bg)
{
    if (!stdout_isatty())
        return;
    if (fg < -1 || bg < -1)
        return;
    sgr_color((int)fg, (int)bg);
}

/// @brief Move the cursor to a 1-based row/column pair.
/// @details Clamps coordinates to the minimum BASIC expects and emits an ANSI
///          cursor-position sequence when stdout is interactive.
void rt_term_locate_i32(int32_t row, int32_t col)
{
    if (!stdout_isatty())
        return;
    if (row < 1)
        row = 1;
    if (col < 1)
        col = 1;
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (int)row, (int)col);
    out_str(buf);
}

/// @brief Show or hide the terminal cursor using ANSI DEC Private Mode sequences.
/// @details Emits CSI ?25h to show the cursor or CSI ?25l to hide it.  The
///          helper only outputs escape codes when stdout is a terminal so
///          redirected output remains free of ANSI sequences.
void rt_term_cursor_visible_i32(int32_t show)
{
    if (!stdout_isatty())
        return;
    out_str(show ? "\x1b[?25h" : "\x1b[?25l");
}

/// @brief Toggle alternate screen buffer using ANSI DEC Private Mode sequences.
/// @details Emits CSI ?1049h to enter the alternate screen buffer or CSI ?1049l
///          to exit and restore the original screen.  The helper only outputs
///          escape codes when stdout is a terminal so redirected output remains
///          free of ANSI sequences.
void rt_term_alt_screen_i32(int32_t enable)
{
    if (!stdout_isatty())
        return;
    out_str(enable ? "\x1b[?1049h" : "\x1b[?1049l");
}

/// @brief Emit a bell/beep sound using BEL character or platform-specific API.
/// @details Writes ASCII BEL (0x07) to stdout and flushes. On Windows, when the
///          VIPER_BEEP_WINAPI environment variable is set to "1", additionally
///          calls the Beep() API with 800Hz frequency for 80ms duration. This
///          provides a portable default (BEL) with optional platform-specific
///          enhancement.
void rt_bell(void)
{
    // Always emit BEL for portability
    fputs("\a", stdout);
    fflush(stdout);

#if defined(_WIN32)
    // On Windows, optionally use Beep API for a more audible tone
    const char *env = getenv("VIPER_BEEP_WINAPI");
    if (env && strcmp(env, "1") == 0)
    {
        // 800 Hz for 80 ms - a short, attention-getting beep
        Beep(800, 80);
    }
#endif
}

#if defined(_WIN32)
/// @brief Read a single key from the console, blocking until one is available.
/// @details Uses `_getch` to obtain a byte without echoing it to the console.
static int readkey_blocking(void)
{
    return _getch() & 0xFF;
}

/// @brief Attempt to read a key without blocking, returning success status.
/// @details Peeks using `_kbhit` and captures the byte with `_getch` when
///          available.  Returns 1 when a key was read and stores the byte in
///          @p out.
static int readkey_nonblocking(int *out)
{
    if (_kbhit())
    {
        *out = _getch() & 0xFF;
        return 1;
    }
    return 0;
}
#else
/// @brief Read a single key from the POSIX terminal, blocking until available.
/// @details Temporarily disables canonical mode and echo, reads one byte, and
///          restores the previous terminal attributes regardless of success.
static int readkey_blocking(void)
{
    struct termios orig, raw;
    int fd = fileno(stdin);
    if (tcgetattr(fd, &orig) != 0)
        return 0;
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &raw) != 0)
        return 0;
    unsigned char ch = 0;
    ssize_t n = read(fd, &ch, 1);
    tcsetattr(fd, TCSANOW, &orig);
    return (n == 1) ? (int)ch : 0;
}

/// @brief Poll the POSIX terminal for a key without blocking.
/// @details Places the terminal in non-canonical, non-blocking mode and attempts
///          to read a byte.  When successful the byte is stored in @p out and the
///          function returns 1.
static int readkey_nonblocking(int *out)
{
    struct termios orig, raw;
    int fd = fileno(stdin);
    if (tcgetattr(fd, &orig) != 0)
        return 0;
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &raw) != 0)
        return 0;
    unsigned char ch = 0;
    ssize_t n = read(fd, &ch, 1);
    tcsetattr(fd, TCSANOW, &orig);
    if (n == 1)
    {
        *out = (int)ch;
        return 1;
    }
    return 0;
}
#endif

/// @brief Block for a single keystroke and return it as a BASIC string.
/// @details Delegates to @ref readkey_blocking and wraps the resulting byte via
///          @ref rt_chr so the runtime's string interning and ownership
///          conventions are respected.
rt_string rt_getkey_str(void)
{
    int code = readkey_blocking();
    return rt_chr((int64_t)code);
}

#if defined(_WIN32)
/// @brief Wait for a keystroke with timeout; return "" if timeout expires.
/// @details Uses WaitForSingleObject to poll the console input handle with the
///          specified timeout. When a key arrives within the timeout window it is
///          read via _getch and converted to a runtime string.
rt_string rt_getkey_timeout_i32(int32_t timeout_ms)
{
    if (timeout_ms < 0)
    {
        // Negative timeout means block indefinitely
        int code = readkey_blocking();
        return rt_chr((int64_t)code);
    }

    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    if (hInput == INVALID_HANDLE_VALUE)
        return rt_const_cstr("");

    DWORD result = WaitForSingleObject(hInput, (DWORD)timeout_ms);
    if (result == WAIT_OBJECT_0)
    {
        // Input is available
        if (_kbhit())
        {
            int code = _getch() & 0xFF;
            return rt_chr((int64_t)code);
        }
    }
    // Timeout or error
    return rt_const_cstr("");
}
#else
/// @brief Wait for a keystroke with timeout; return "" if timeout expires.
/// @details Places the terminal in raw mode and uses select() to wait for input
///          with the specified timeout. When a key arrives before the deadline it
///          is read and converted to a runtime string; otherwise the empty string
///          is returned.
rt_string rt_getkey_timeout_i32(int32_t timeout_ms)
{
    if (timeout_ms < 0)
    {
        // Negative timeout means block indefinitely
        int code = readkey_blocking();
        return rt_chr((int64_t)code);
    }

    struct termios orig, raw;
    int fd = fileno(stdin);
    if (tcgetattr(fd, &orig) != 0)
        return rt_const_cstr("");

    // Set raw mode
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &raw) != 0)
        return rt_const_cstr("");

    // Use select to wait with timeout
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);

    unsigned char ch = 0;
    if (ret > 0 && FD_ISSET(fd, &readfds))
    {
        // Data is available
        ssize_t n = read(fd, &ch, 1);
        tcsetattr(fd, TCSANOW, &orig);
        if (n == 1)
            return rt_chr((int64_t)ch);
    }
    else
    {
        // Timeout or error
        tcsetattr(fd, TCSANOW, &orig);
    }

    return rt_const_cstr("");
}
#endif

/// @brief Non-blocking key read that returns "" when no key is pending.
/// @details Uses @ref readkey_nonblocking to poll the console.  When a key is
///          available it is converted using @ref rt_chr; otherwise the canonical
///          empty string from @ref rt_const_cstr is returned.
rt_string rt_inkey_str(void)
{
    int code = 0;
    int ok = readkey_nonblocking(&code);
    if (ok)
        return rt_chr((int64_t)code);
    return rt_const_cstr(""); // use your runtime's empty-string helper
}
