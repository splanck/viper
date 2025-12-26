//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "rt_output.h"
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

// =============================================================================
// PERFORMANCE OPTIMIZATION: Terminal Raw Mode Caching
// =============================================================================
//
// Problem: Every INKEY$() call was doing tcgetattr + tcsetattr + tcsetattr,
// which are expensive system calls. In a game loop running at 60 FPS, this
// meant 180+ syscalls per second just for keyboard polling.
//
// Solution: Cache the terminal state. When "raw mode" is enabled:
// - Store original termios settings once
// - Set raw mode once
// - Subsequent INKEY$() calls just use select() - no termios changes
// - Restore original settings when raw mode is disabled or program exits
//
// Raw mode is automatically enabled when:
// - Alt screen buffer is activated (typical for games)
// - Explicitly via rt_term_enable_raw_mode()
//
// =============================================================================

#if !defined(_WIN32)
/// @brief Cached original terminal settings (before raw mode).
static struct termios g_orig_termios;

/// @brief Cached raw mode terminal settings.
static struct termios g_raw_termios;

/// @brief Whether raw mode caching is currently active.
static int g_raw_mode_active = 0;

/// @brief Whether we've captured the original terminal settings.
static int g_termios_saved = 0;

/// @brief File descriptor for stdin (cached to avoid repeated fileno calls).
static int g_stdin_fd = -1;

/// @brief Whether atexit handler has been registered.
static int g_atexit_registered = 0;

/// @brief Cleanup handler called on program exit.
/// @details Ensures terminal is restored to original state if raw mode was active.
static void term_atexit_handler(void)
{
    rt_term_disable_raw_mode();
}

/// @brief Initialize terminal state caching.
/// @details Called lazily on first use. Saves original terminal settings.
static void init_term_cache(void)
{
    if (g_stdin_fd < 0)
        g_stdin_fd = fileno(stdin);

    if (!g_termios_saved && g_stdin_fd >= 0 && isatty(g_stdin_fd))
    {
        if (tcgetattr(g_stdin_fd, &g_orig_termios) == 0)
        {
            g_termios_saved = 1;
            // Prepare raw mode settings
            g_raw_termios = g_orig_termios;
            g_raw_termios.c_lflag &= ~(ICANON | ECHO);
            g_raw_termios.c_cc[VMIN] = 0;
            g_raw_termios.c_cc[VTIME] = 0;
        }
    }
}

/// @brief Enable cached raw mode for efficient key polling.
/// @details Switches terminal to raw mode once. Subsequent INKEY$ calls
///          will use select() without needing to change terminal settings.
void rt_term_enable_raw_mode(void)
{
    init_term_cache();
    if (g_raw_mode_active || !g_termios_saved)
        return;

    // Register atexit handler to ensure terminal is restored on exit
    if (!g_atexit_registered)
    {
        atexit(term_atexit_handler);
        g_atexit_registered = 1;
    }

    if (tcsetattr(g_stdin_fd, TCSANOW, &g_raw_termios) == 0)
        g_raw_mode_active = 1;
}

/// @brief Disable raw mode and restore original terminal settings.
/// @details Should be called before program exit or when leaving game mode.
void rt_term_disable_raw_mode(void)
{
    if (!g_raw_mode_active || !g_termios_saved)
        return;

    tcsetattr(g_stdin_fd, TCSANOW, &g_orig_termios);
    g_raw_mode_active = 0;
}

/// @brief Check if raw mode caching is currently active.
int rt_term_is_raw_mode(void)
{
    return g_raw_mode_active;
}

#else // Windows doesn't need raw mode caching - _kbhit is already efficient

void rt_term_enable_raw_mode(void) {}

void rt_term_disable_raw_mode(void) {}

int rt_term_is_raw_mode(void)
{
    return 0;
}

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
/// @details Writes to the output buffer and conditionally flushes based on
///          batch mode. When batch mode is active (via rt_output_begin_batch),
///          output accumulates until rt_output_end_batch is called, dramatically
///          reducing system calls during screen rendering.
///
/// Performance note: Before this change, each terminal operation caused an
/// immediate fflush(), resulting in thousands of system calls per frame.
/// With output buffering, a typical 60x20 game screen update goes from
/// ~6000 syscalls to ~1 syscall (at batch end).
static void out_str(const char *s)
{
    if (!s)
        return;
#if defined(_WIN32)
    enable_vt();
#endif
    rt_output_str(s);
    rt_output_flush_if_not_batch();
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
///
/// PERFORMANCE: Automatically enables/disables raw mode caching when entering/
///              exiting alt screen. Games typically use alt screen, so this
///              provides automatic optimization for game loops.
void rt_term_alt_screen_i32(int32_t enable)
{
    if (!stdout_isatty())
        return;
    if (enable)
    {
        out_str("\x1b[?1049h");
        // Auto-enable raw mode for better INKEY$ performance in games
        rt_term_enable_raw_mode();
        // Also auto-enable batch mode for screen rendering
        rt_output_begin_batch();
    }
    else
    {
        // End batch mode before exiting alt screen
        rt_output_end_batch();
        // Restore original terminal settings
        rt_term_disable_raw_mode();
        out_str("\x1b[?1049l");
    }
}

/// @brief Emit a bell/beep sound using BEL character or platform-specific API.
/// @details Writes ASCII BEL (0x07) to stdout and flushes. On Windows, when the
///          VIPER_BEEP_WINAPI environment variable is set to "1", additionally
///          calls the Beep() API with 800Hz frequency for 80ms duration. This
///          provides a portable default (BEL) with optional platform-specific
///          enhancement.
void rt_bell(void)
{
    // Always emit BEL for portability - bell should always flush immediately
    // to ensure the user hears it at the expected moment
    rt_output_str("\a");
    rt_output_flush();

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
/// @details When raw mode caching is active, uses only select() for maximum
///          performance. Otherwise falls back to the traditional approach of
///          temporarily setting raw mode for each call.
///
/// PERFORMANCE: With raw mode caching, this function does:
///   - 1 select() syscall (unavoidable for non-blocking check)
///   - 0-1 read() syscall (only if data available)
/// Without caching, each call did:
///   - 1 tcgetattr() syscall
///   - 1 tcsetattr() syscall (set raw)
///   - 1 select() or read() syscall
///   - 1 tcsetattr() syscall (restore)
/// That's 3x fewer syscalls in the hot path!
static int readkey_nonblocking(int *out)
{
    int fd = g_stdin_fd >= 0 ? g_stdin_fd : fileno(stdin);

    // Check if stdin is a TTY or a pipe/file
    if (!isatty(fd))
    {
        // For pipes/files: use select() to check for data without blocking
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if (ret > 0 && FD_ISSET(fd, &readfds))
        {
            unsigned char ch = 0;
            ssize_t n = read(fd, &ch, 1);
            if (n == 1)
            {
                *out = (int)ch;
                return 1;
            }
        }
        return 0;
    }

    // FAST PATH: If raw mode is already active, just use select() + read()
    // This eliminates the tcgetattr/tcsetattr overhead entirely!
    if (g_raw_mode_active)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if (ret > 0 && FD_ISSET(fd, &readfds))
        {
            unsigned char ch = 0;
            ssize_t n = read(fd, &ch, 1);
            if (n == 1)
            {
                *out = (int)ch;
                return 1;
            }
        }
        return 0;
    }

    // SLOW PATH: Traditional approach - set raw mode temporarily
    // This is only used when raw mode caching hasn't been enabled
    struct termios orig, raw;
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
///          conventions are respected. Flushes output first to ensure any
///          pending screen updates are visible before blocking.
rt_string rt_getkey_str(void)
{
    // Flush output before blocking for input so user sees current state
    rt_output_flush();
    int code = readkey_blocking();
    return rt_chr((int64_t)code);
}

#if defined(_WIN32)
/// @brief Wait for a keystroke with timeout; return "" if timeout expires.
/// @details Uses WaitForSingleObject to poll the console input handle with the
///          specified timeout. When a key arrives within the timeout window it is
///          read via _getch and converted to a runtime string. Flushes output
///          first to ensure any pending screen updates are visible.
rt_string rt_getkey_timeout_i32(int32_t timeout_ms)
{
    // Flush output before waiting for input so user sees current state
    rt_output_flush();

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
///          is returned. Flushes output first to ensure pending updates are visible.
rt_string rt_getkey_timeout_i32(int32_t timeout_ms)
{
    // Flush output before waiting for input so user sees current state
    rt_output_flush();

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
///          empty string from @ref rt_const_cstr is returned. Flushes output
///          first to ensure the screen is up-to-date when polling.
rt_string rt_inkey_str(void)
{
    // Flush output so user sees current state when we check for input
    rt_output_flush();
    int code = 0;
    int ok = readkey_nonblocking(&code);
    if (ok)
        return rt_chr((int64_t)code);
    return rt_const_cstr(""); // use your runtime's empty-string helper
}

#if defined(_WIN32)
/// @brief Check if a key is available in the input buffer without reading it.
/// @details Returns non-zero if a key is pending, zero otherwise.
int32_t rt_keypressed(void)
{
    return _kbhit() ? 1 : 0;
}
#else
/// @brief Check if a key is available in the input buffer without reading it.
/// @details Uses select() with zero timeout to poll the terminal. Returns non-zero
///          if a key is pending, zero otherwise. When stdin is a pipe or file,
///          directly uses select() without modifying terminal settings.
///
/// PERFORMANCE: When raw mode caching is active, this only does a single
///              select() syscall instead of tcgetattr + tcsetattr + select + tcsetattr.
int32_t rt_keypressed(void)
{
    int fd = g_stdin_fd >= 0 ? g_stdin_fd : fileno(stdin);

    // For pipes/files: just use select directly
    if (!isatty(fd))
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
        return (ret > 0 && FD_ISSET(fd, &readfds)) ? 1 : 0;
    }

    // FAST PATH: If raw mode is already active, just use select()
    if (g_raw_mode_active)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
        return (ret > 0 && FD_ISSET(fd, &readfds)) ? 1 : 0;
    }

    // SLOW PATH: For TTY when raw mode not cached - set raw mode temporarily
    struct termios orig, raw;
    if (tcgetattr(fd, &orig) != 0)
        return 0;
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &raw) != 0)
        return 0;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
    tcsetattr(fd, TCSANOW, &orig);

    return (ret > 0 && FD_ISSET(fd, &readfds)) ? 1 : 0;
}
#endif

// =============================================================================
// Output Batch Mode Control Functions
// =============================================================================

/// @brief Begin batch mode for output operations.
/// @details While in batch mode, terminal control sequences (COLOR, LOCATE,
///          etc.) do not trigger individual flushes. This dramatically improves
///          rendering performance for games and animations.
///
/// Usage in BASIC:
///   _SCREENBATCH ON   ' or _BEGINBATCH
///   ' ... multiple LOCATE, COLOR, PRINT operations ...
///   _SCREENBATCH OFF  ' or _ENDBATCH - flushes all at once
///
/// Performance: Reduces syscalls from ~6000/frame to ~1/frame for typical games.
void rt_term_begin_batch(void)
{
    rt_output_begin_batch();
}

/// @brief End batch mode and flush accumulated output.
/// @details Decrements the batch mode reference count. When it reaches zero,
///          all accumulated output is flushed to the terminal in a single
///          system call, eliminating screen flashing.
void rt_term_end_batch(void)
{
    rt_output_end_batch();
}

/// @brief Explicitly flush terminal output.
/// @details Forces all buffered output to be written immediately. Useful when
///          you need to ensure output is visible without ending batch mode.
void rt_term_flush(void)
{
    rt_output_flush();
}

// =============================================================================
// Pascal-Compatible Wrappers (i64 arguments)
// =============================================================================

/// @brief Move cursor to position (for Pascal which uses i64 integers).
void rt_term_locate(int64_t row, int64_t col)
{
    rt_term_locate_i32((int32_t)row, (int32_t)col);
}

/// @brief Set terminal colors (for Pascal which uses i64 integers).
void rt_term_color(int64_t fg, int64_t bg)
{
    rt_term_color_i32((int32_t)fg, (int32_t)bg);
}

/// @brief Set foreground text color only.
void rt_term_textcolor(int64_t fg)
{
    rt_term_color_i32((int32_t)fg, -1);
}

/// @brief Set background color only.
void rt_term_textbg(int64_t bg)
{
    rt_term_color_i32(-1, (int32_t)bg);
}

/// @brief Hide cursor.
void rt_term_hide_cursor(void)
{
    rt_term_cursor_visible_i32(0);
}

/// @brief Show cursor.
void rt_term_show_cursor(void)
{
    rt_term_cursor_visible_i32(1);
}

/// @brief Set cursor visibility (i64 wrapper for ViperLang).
void rt_term_cursor_visible(int64_t show)
{
    rt_term_cursor_visible_i32((int32_t)show);
}

/// @brief Set alt screen mode (i64 wrapper for ViperLang).
void rt_term_alt_screen(int64_t enable)
{
    rt_term_alt_screen_i32((int32_t)enable);
}

/// @brief Sleep for specified milliseconds (i64 wrapper).
void rt_sleep_ms_i64(int64_t ms)
{
    rt_sleep_ms((int32_t)ms);
}

/// @brief Check if a key is available (i64 wrapper for Pascal).
int64_t rt_keypressed_i64(void)
{
    return (int64_t)rt_keypressed();
}

/// @brief Get key with timeout (i64 wrapper for ViperLang).
rt_string rt_getkey_timeout(int64_t timeout_ms)
{
    return rt_getkey_timeout_i32((int32_t)timeout_ms);
}
