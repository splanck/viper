/**
 * @file vinit.cpp
 * @brief ViperOS init process (`vinit`) with a minimal built-in shell.
 *
 * @details
 * `vinit` is intended to be the first user-space process started by the kernel.
 * In the current bring-up environment it provides:
 * - A basic interactive command loop (shell) for debugging and demos.
 * - Simple utilities to exercise syscalls (filesystem, memory info, task list,
 *   networking, and TLS).
 *
 * The code is intentionally freestanding and self-contained: it does not rely
 * on a hosted C/C++ runtime or libc. Instead it uses the thin syscall wrappers
 * in `user/syscall.hpp` and provides minimal helpers for strings and memory
 * movement where needed.
 *
 * Design notes:
 * - The shell is "Amiga-inspired" (commands like `Dir`, `List`, `Assign`).
 * - Line editing is implemented directly using ANSI escape sequences and a
 *   small in-memory history ring.
 * - Error handling is best-effort and focuses on providing useful feedback
 *   during OS bring-up rather than strict POSIX behavior.
 */

#include "../syscall.hpp"

/** @name Small string helpers
 *  @brief Minimal string primitives for a freestanding environment.
 *  @{
 */

/**
 * @brief Compute the length of a NUL-terminated string.
 *
 * @details
 * This is a minimal replacement for `strlen(3)` and performs a linear scan
 * until the first `\\0` byte is found.
 *
 * @param s Pointer to a NUL-terminated string.
 * @return Number of bytes before the terminating NUL.
 */
usize strlen(const char *s)
{
    usize len = 0;
    while (s[len])
        len++;
    return len;
}

/**
 * @brief Compare two strings for exact equality.
 *
 * @details
 * Performs a byte-wise comparison and returns true only if both strings have
 * identical content and terminate at the same time.
 *
 * @param a First NUL-terminated string.
 * @param b Second NUL-terminated string.
 * @return True if both strings are identical.
 */
bool streq(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a != *b)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

/**
 * @brief Test whether a string starts with a given prefix (case-sensitive).
 *
 * @details
 * Returns true if `prefix` is empty or if every character of `prefix` matches
 * the corresponding character in `s`.
 *
 * @param s NUL-terminated input string.
 * @param prefix NUL-terminated prefix to match.
 * @return True if `s` begins with `prefix`.
 */
bool strstart(const char *s, const char *prefix)
{
    while (*prefix)
    {
        if (*s != *prefix)
            return false;
        s++;
        prefix++;
    }
    return true;
}

/** @} */

/** @name Console output helpers
 *  @brief Thin wrappers around the syscall console/debug APIs.
 *  @{
 */

/**
 * @brief Write a NUL-terminated string to the debug console.
 *
 * @details
 * This uses the kernel debug print syscall. The string is written exactly as
 * provided; callers are responsible for including any desired newline
 * characters.
 *
 * This is named `print_str` to avoid conflict with libc's `print_str()` which
 * adds a newline per POSIX.
 *
 * @param s NUL-terminated string to print.
 */
// Forward declarations for paging (defined below)
static bool g_paging = false;
static bool g_page_quit = false;
static int g_page_line = 0;
constexpr int SCREEN_HEIGHT = 24;
void paged_print(const char *s);

void print_str(const char *s)
{
    if (g_paging)
    {
        paged_print(s);
    }
    else
    {
        sys::print(s);
    }
}

/**
 * @brief Write a single character to the console.
 *
 * @details
 * This uses the kernel character output syscall. It is used heavily by the
 * line editor to emit ANSI escape sequences and backspaces.
 *
 * This is named `print_char` to avoid conflict with libc's `print_char()`.
 *
 * @param c Character to print.
 */
void print_char(char c)
{
    sys::putchar(c);
}

/** @} */

/** @name Paging support
 *  @brief Output pagination for long command output.
 *  @{
 */

/**
 * @brief Wait for user keypress during paging.
 *
 * @details
 * Displays a prompt and waits for user input:
 * - Space or Enter: continue to next page
 * - Q or q: quit paging, suppress remaining output
 * - Any other key: show one more line
 *
 * @return true to continue, false to quit
 */
bool page_wait()
{
    sys::print("\x1b[7m-- More (Space=page, Enter=line, Q=quit) --\x1b[0m");

    // Wait for a character (blocking getchar)
    int c = sys::getchar();

    // Clear the prompt
    sys::print("\r\x1b[K");

    if (c == 'q' || c == 'Q')
    {
        g_page_quit = true;
        return false;
    }
    else if (c == ' ')
    {
        // Next page
        g_page_line = 0;
        return true;
    }
    else if (c == '\r' || c == '\n')
    {
        // One more line
        g_page_line = SCREEN_HEIGHT - 1;
        return true;
    }
    else
    {
        // Any other key: next page
        g_page_line = 0;
        return true;
    }
}

/**
 * @brief Print a string with paging support.
 *
 * @param s NUL-terminated string to print.
 */
void paged_print(const char *s)
{
    if (!g_paging || g_page_quit)
    {
        if (!g_page_quit)
            sys::print(s);
        return;
    }

    while (*s)
    {
        if (g_page_quit)
            return;

        sys::putchar(*s);

        if (*s == '\n')
        {
            g_page_line++;
            if (g_page_line >= SCREEN_HEIGHT - 1)
            {
                if (!page_wait())
                    return;
            }
        }
        s++;
    }
}

/**
 * @brief Print a character with paging support.
 *
 * @param c Character to print.
 */
void paged_putc(char c)
{
    if (!g_paging || g_page_quit)
    {
        if (!g_page_quit)
            sys::putchar(c);
        return;
    }

    sys::putchar(c);

    if (c == '\n')
    {
        g_page_line++;
        if (g_page_line >= SCREEN_HEIGHT - 1)
        {
            page_wait();
        }
    }
}

/**
 * @brief Start paging mode.
 */
void paging_start()
{
    g_paging = true;
    g_page_line = 0;
    g_page_quit = false;
}

/**
 * @brief Stop paging mode.
 */
void paging_stop()
{
    g_paging = false;
    g_page_line = 0;
    g_page_quit = false;
}

/** @} */

/**
 * @brief Print a signed integer in decimal.
 *
 * @details
 * Converts the number to ASCII in a small stack buffer and prints it using
 * @ref puts. No trailing newline is added.
 *
 * @param n Number to print.
 */
void put_num(i64 n)
{
    char buf[32];
    char *p = buf + 31;
    *p = '\0';

    bool neg = false;
    if (n < 0)
    {
        neg = true;
        n = -n;
    }

    do
    {
        *--p = '0' + (n % 10);
        n /= 10;
    } while (n > 0);

    if (neg)
        *--p = '-';

    print_str(p);
}

/**
 * @brief Print a 32-bit integer in hexadecimal with `0x` prefix.
 *
 * @details
 * This helper is primarily used for displaying handle values and rights masks
 * while debugging the capability system. Output uses lowercase digits.
 *
 * @param n Value to print.
 */
void put_hex(u32 n)
{
    print_str("0x");
    char buf[16];
    char *p = buf + 15;
    *p = '\0';

    do
    {
        int digit = n & 0xF;
        *--p = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        n >>= 4;
    } while (n > 0);

    print_str(p);
}

/** @} */

/** @name Minimal memory routines
 *  @brief Small freestanding replacements for common libc primitives.
 *  @{
 */

/**
 * @brief Copy bytes from `src` to `dst` (non-overlapping).
 *
 * @details
 * This is a simple byte loop intended for small buffers. If the source and
 * destination ranges overlap, behavior is the same as a naive byte copy and is
 * not guaranteed to match `memmove(3)` semantics.
 *
 * @param dst Destination pointer.
 * @param src Source pointer.
 * @param n Number of bytes to copy.
 * @return `dst`.
 */
void *memcpy(void *dst, const void *src, usize n)
{
    char *d = static_cast<char *>(dst);
    const char *s = static_cast<const char *>(src);
    while (n--)
        *d++ = *s++;
    return dst;
}

/**
 * @brief Copy bytes from `src` to `dst`, handling overlap safely.
 *
 * @details
 * If the ranges overlap and `dst` is above `src`, the copy proceeds backwards
 * to avoid clobbering bytes that have not yet been copied.
 *
 * @param dst Destination pointer.
 * @param src Source pointer.
 * @param n Number of bytes to move.
 * @return `dst`.
 */
void *memmove(void *dst, const void *src, usize n)
{
    char *d = static_cast<char *>(dst);
    const char *s = static_cast<const char *>(src);
    if (d < s)
    {
        while (n--)
            *d++ = *s++;
    }
    else if (d > s)
    {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

/** @} */

/**
 * @brief Redraw the portion of the input line from `pos` to the end.
 *
 * @details
 * The line editor maintains an input buffer plus a cursor index. When the user
 * inserts or deletes characters in the middle of the line, the terminal needs
 * to be updated to reflect the new tail of the buffer.
 *
 * This helper:
 * 1. Prints the characters from `pos` to `len`.
 * 2. Prints a trailing space to erase a leftover character when shrinking.
 * 3. Emits backspaces to return the cursor to the logical `pos`.
 *
 * @param buf Input buffer.
 * @param len Current buffer length.
 * @param pos Cursor position within the buffer.
 */
static void redraw_line_from(const char *buf, usize len, usize pos)
{
    // Print characters from pos to end
    for (usize i = pos; i < len; i++)
    {
        print_char(buf[i]);
    }
    // Clear one extra char in case we deleted
    print_char(' ');
    // Move cursor back to current position
    for (usize i = len + 1; i > pos; i--)
    {
        print_char('\b');
    }
}

/**
 * @brief Move the terminal cursor left by `n` columns.
 *
 * @details
 * Emits the ANSI escape sequence `ESC[D` `n` times. This is used by the line
 * editor to reposition the cursor for insert/delete operations.
 *
 * @param n Number of columns to move left.
 */
static void cursor_left(usize n)
{
    while (n--)
    {
        print_str("\033[D");
    }
}

/**
 * @brief Move the terminal cursor right by `n` columns.
 *
 * @details
 * Emits the ANSI escape sequence `ESC[C` `n` times.
 *
 * @param n Number of columns to move right.
 */
static void cursor_right(usize n)
{
    while (n--)
    {
        print_str("\033[C");
    }
}

/** @name Command history
 *  @brief Simple in-memory history ring used by the line editor.
 *  @{
 */

/** @brief Maximum number of commands stored in history. */
constexpr usize HISTORY_SIZE = 16;
/** @brief Maximum length of each stored history line, including the terminating NUL. */
constexpr usize HISTORY_LINE_LEN = 256;
/** @brief Circular history buffer storing the last @ref HISTORY_SIZE lines. */
static char history[HISTORY_SIZE][HISTORY_LINE_LEN];
/** @brief Total number of commands ever recorded (monotonic counter). */
static usize history_count = 0;
/** @brief Current index used when navigating history with up/down arrows. */
static usize history_index = 0;

/**
 * @brief Append a line to the history ring.
 *
 * @details
 * - Empty lines are ignored.
 * - If the new line matches the most recent entry, it is not duplicated.
 * - When the ring is full, the oldest entry is overwritten.
 *
 * @param line NUL-terminated command line to store.
 */
static void history_add(const char *line)
{
    if (strlen(line) == 0)
        return;

    // Don't add duplicates of the last command
    if (history_count > 0)
    {
        usize last = (history_count - 1) % HISTORY_SIZE;
        if (streq(history[last], line))
            return;
    }

    // Copy to history buffer
    usize idx = history_count % HISTORY_SIZE;
    usize i = 0;
    for (; line[i] && i < HISTORY_LINE_LEN - 1; i++)
    {
        history[idx][i] = line[i];
    }
    history[idx][i] = '\0';
    history_count++;
}

/**
 * @brief Get a history entry by absolute index.
 *
 * @details
 * `history_count` is a monotonic counter. This function treats `index` as an
 * absolute position in that count (0-based) and maps it into the circular
 * storage. Entries older than the ring capacity are considered unavailable.
 *
 * @param index Absolute command index (0-based).
 * @return Pointer to the stored line, or `nullptr` if the entry is not available.
 */
static const char *history_get(usize index)
{
    if (index >= history_count)
        return nullptr;
    // Calculate actual index in circular buffer
    usize first = (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
    if (index < first)
        return nullptr;
    return history[index % HISTORY_SIZE];
}

/**
 * @brief Replace the current editable line buffer with new content.
 *
 * @details
 * This is used for history navigation and tab completion. It performs both the
 * terminal update (clearing/redrawing the line) and the buffer update:
 * - Moves the cursor back to the beginning of the current line.
 * - Overwrites the displayed characters with spaces.
 * - Copies `newline` into `buf` and prints it.
 * - Updates `len` and `pos` to the end of the new line.
 *
 * @param buf Editable line buffer to update.
 * @param len In/out pointer to current line length.
 * @param pos In/out pointer to current cursor position.
 * @param newline NUL-terminated replacement line content.
 */
static void replace_line(char *buf, usize *len, usize *pos, const char *newline)
{
    // Move cursor to start
    cursor_left(*pos);
    // Clear the line
    for (usize i = 0; i < *len; i++)
        print_char(' ');
    cursor_left(*len);
    // Copy new line
    *len = 0;
    *pos = 0;
    for (usize i = 0; newline[i] && i < 255; i++)
    {
        buf[i] = newline[i];
        print_char(newline[i]);
        (*len)++;
        (*pos)++;
    }
    buf[*len] = '\0';
}

/** @} */

/** @name Shell return codes
 *  @brief Amiga-style numeric return codes used for user feedback.
 *  @{
 */

/**
 * @brief Return codes used by shell commands.
 *
 * @details
 * These values mirror the classic AmigaDOS convention where non-zero return
 * codes are grouped by severity.
 */
enum ReturnCode
{
    RC_OK = 0,     /**< Success. */
    RC_WARN = 5,   /**< Non-fatal issue; command produced a warning. */
    RC_ERROR = 10, /**< Command failed due to an error. */
    RC_FAIL = 20,  /**< Severe failure; the requested operation could not proceed. */
};

/** @brief Return code from the last executed command. */
static int last_rc = RC_OK;
/** @brief Optional human-readable explanation for the last error. */
static const char *last_error = nullptr;
/** @} */

/**
 * @brief Current directory string shown in the shell prompt.
 *
 * @details
 * This is updated by the CD command via getcwd() syscall.
 */
static char current_dir[256] = "/";

/**
 * @brief Refresh the current_dir from the kernel's CWD.
 *
 * @details
 * Calls getcwd() to get the actual current working directory from the kernel.
 */
static void refresh_current_dir()
{
    if (sys::getcwd(current_dir, sizeof(current_dir)) < 0)
    {
        current_dir[0] = '/';
        current_dir[1] = '\0';
    }
}

/**
 * @brief Built-in command names used for tab completion.
 *
 * @details
 * Tab completion in the line editor is intentionally simple: it completes only
 * the first word (the command name) and does not attempt to complete paths.
 */
static const char *commands[] = {"Assign",  "Avail", "Caps", "chdir",    "Cls",     "Copy",
                                 "cwd",     "Date",  "Delete","Dir",     "Echo",    "EndShell",
                                 "Fetch",   "Help",  "History","Info",   "List",    "MakeDir",
                                 "Path",    "Rename","Run",  "Status",   "Time",    "Type",
                                 "Uptime",  "Version","Why"};
static const usize num_commands = sizeof(commands) / sizeof(commands[0]);

/**
 * @brief Compute the length of the common prefix of two strings.
 *
 * @details
 * Used by tab completion to extend the input buffer to the longest shared
 * prefix among multiple matches.
 *
 * @param a First NUL-terminated string.
 * @param b Second NUL-terminated string.
 * @return Number of leading characters that are equal in both strings.
 */
static usize common_prefix(const char *a, const char *b)
{
    usize i = 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return i;
}

/**
 * @brief Read an input line from the console with basic line editing.
 *
 * @details
 * Provides a small interactive editor suitable for a serial console:
 * - Printable characters are inserted at the current cursor position.
 * - Backspace deletes the character before the cursor.
 * - Left/Right arrows move the cursor.
 * - Home/End jump to start/end of the line.
 * - Delete removes the character at the cursor.
 * - Up/Down arrows navigate the in-memory command history.
 * - Tab performs simple command-name completion based on @ref commands.
 * - Ctrl+C cancels input and returns an empty line.
 * - Ctrl+U clears the whole line; Ctrl+K clears from cursor to end.
 *
 * The function stops when the user presses Enter or the buffer is full. The
 * resulting buffer is always NUL-terminated.
 *
 * @param buf Destination buffer to receive the line.
 * @param maxlen Size of `buf` in bytes (including space for the NUL terminator).
 * @return Number of characters stored in `buf` (excluding the terminating NUL).
 */
usize readline(char *buf, usize maxlen)
{
    usize len = 0; // Total length of input
    usize pos = 0; // Cursor position

    // Save current line for history navigation
    static char saved_line[256];
    saved_line[0] = '\0';
    history_index = history_count; // Start at end (current input)

    while (len < maxlen - 1)
    {
        char c = sys::getchar();

        // Handle escape sequences
        if (c == '\033')
        {
            char c2 = sys::getchar();
            if (c2 == '[')
            {
                char c3 = sys::getchar();
                switch (c3)
                {
                    case 'A': // Up arrow - previous history
                        if (history_index > 0)
                        {
                            // Save current line if we're starting navigation
                            if (history_index == history_count && len > 0)
                            {
                                for (usize i = 0; i <= len; i++)
                                    saved_line[i] = buf[i];
                            }
                            history_index--;
                            usize first =
                                (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
                            if (history_index >= first)
                            {
                                const char *hist = history_get(history_index);
                                if (hist)
                                    replace_line(buf, &len, &pos, hist);
                            }
                        }
                        break;
                    case 'B': // Down arrow - next history
                        if (history_index < history_count)
                        {
                            history_index++;
                            if (history_index == history_count)
                            {
                                // Restore saved line
                                replace_line(buf, &len, &pos, saved_line);
                            }
                            else
                            {
                                const char *hist = history_get(history_index);
                                if (hist)
                                    replace_line(buf, &len, &pos, hist);
                            }
                        }
                        break;
                    case 'C': // Right arrow
                        if (pos < len)
                        {
                            cursor_right(1);
                            pos++;
                        }
                        break;
                    case 'D': // Left arrow
                        if (pos > 0)
                        {
                            cursor_left(1);
                            pos--;
                        }
                        break;
                    case 'H': // Home
                        cursor_left(pos);
                        pos = 0;
                        break;
                    case 'F': // End
                        cursor_right(len - pos);
                        pos = len;
                        break;
                    case '3':               // Delete key (followed by ~)
                        c = sys::getchar(); // consume '~'
                        if (pos < len)
                        {
                            memmove(buf + pos, buf + pos + 1, len - pos);
                            len--;
                            redraw_line_from(buf, len, pos);
                        }
                        break;
                    case '5':           // Page Up (ignore)
                    case '6':           // Page Down (ignore)
                        sys::getchar(); // consume '~'
                        break;
                }
                continue;
            }
            // Unknown escape sequence, ignore
            continue;
        }

        if (c == '\r' || c == '\n')
        {
            print_char('\r');
            print_char('\n');
            break;
        }

        if (c == 127 || c == '\b')
        { // Backspace
            if (pos > 0)
            {
                pos--;
                memmove(buf + pos, buf + pos + 1, len - pos);
                len--;
                print_char('\b');
                redraw_line_from(buf, len, pos);
            }
            continue;
        }

        if (c == 3)
        { // Ctrl+C
            print_str("^C\n");
            len = 0;
            pos = 0;
            break;
        }

        if (c == 1)
        { // Ctrl+A (Home)
            cursor_left(pos);
            pos = 0;
            continue;
        }

        if (c == 5)
        { // Ctrl+E (End)
            cursor_right(len - pos);
            pos = len;
            continue;
        }

        if (c == 21)
        { // Ctrl+U (kill line)
            cursor_left(pos);
            for (usize i = 0; i < len; i++)
                print_char(' ');
            cursor_left(len);
            len = 0;
            pos = 0;
            continue;
        }

        if (c == 11)
        { // Ctrl+K (kill to end)
            for (usize i = pos; i < len; i++)
                print_char(' ');
            cursor_left(len - pos);
            len = pos;
            continue;
        }

        if (c == '\t')
        { // Tab completion
            buf[len] = '\0';
            // Only complete if at start of line (command completion)
            // Find matching commands
            const char *first_match = nullptr;
            usize match_count = 0;
            usize prefix_len = 0;

            for (usize i = 0; i < num_commands; i++)
            {
                if (strstart(commands[i], buf))
                {
                    if (match_count == 0)
                    {
                        first_match = commands[i];
                        prefix_len = strlen(commands[i]);
                    }
                    else
                    {
                        // Find common prefix with previous matches
                        prefix_len = common_prefix(first_match, commands[i]);
                        if (prefix_len < len)
                            prefix_len = len;
                    }
                    match_count++;
                }
            }

            if (match_count == 1)
            {
                // Single match - complete it
                replace_line(buf, &len, &pos, first_match);
            }
            else if (match_count > 1)
            {
                // Multiple matches - complete common prefix and show options
                if (prefix_len > len)
                {
                    // Extend to common prefix
                    for (usize i = len; i < prefix_len; i++)
                    {
                        buf[i] = first_match[i];
                        print_char(first_match[i]);
                    }
                    len = prefix_len;
                    pos = len;
                    buf[len] = '\0';
                }
                else
                {
                    // Show all matches
                    print_str("\n");
                    for (usize i = 0; i < num_commands; i++)
                    {
                        if (strstart(commands[i], buf))
                        {
                            print_str(commands[i]);
                            print_str("  ");
                        }
                    }
                    print_str("\n");
                    print_str(current_dir);
                    print_str("> ");
                    for (usize i = 0; i < len; i++)
                        print_char(buf[i]);
                    pos = len;
                }
            }
            continue;
        }

        if (c >= 32 && c < 127)
        { // Printable
            if (len >= maxlen - 1)
                continue;
            // Insert at cursor position
            memmove(buf + pos + 1, buf + pos, len - pos);
            buf[pos] = c;
            len++;
            // Print char and rest of line
            print_char(c);
            pos++;
            if (pos < len)
            {
                redraw_line_from(buf, len, pos);
            }
        }
    }

    buf[len] = '\0';
    return len;
}

/**
 * @brief Compare two strings for equality (ASCII case-insensitive).
 *
 * @details
 * This helper performs a simple ASCII-only case fold for characters A–Z by
 * converting them to a–z. It does not perform locale-aware or Unicode-aware
 * case mapping.
 *
 * @param a First NUL-terminated string.
 * @param b Second NUL-terminated string.
 * @return True if the strings are equal when compared case-insensitively.
 */
bool strcaseeq(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

/**
 * @brief Test whether `s` starts with `prefix` (ASCII case-insensitive).
 *
 * @details
 * Like @ref strcaseeq, this is ASCII-only and intended for matching shell
 * commands regardless of capitalization.
 *
 * @param s NUL-terminated input string.
 * @param prefix NUL-terminated prefix string.
 * @return True if `s` begins with `prefix` (case-insensitive).
 */
bool strcasestart(const char *s, const char *prefix)
{
    while (*prefix)
    {
        char cs = (*s >= 'A' && *s <= 'Z') ? (*s + 32) : *s;
        char cp = (*prefix >= 'A' && *prefix <= 'Z') ? (*prefix + 32) : *prefix;
        if (cs != cp)
            return false;
        s++;
        prefix++;
    }
    return true;
}

/** @name Built-in shell commands
 *  @brief Command handlers invoked by the main shell loop.
 *
 *  @details
 *  Each `cmd_*` function implements one shell command. Commands typically
 *  update @ref last_rc and @ref last_error to support the `Why` command.
 *  @{
 */

/**
 * @brief Print the built-in help text.
 *
 * @details
 * Displays available commands, expected arguments, and a short reminder of the
 * line editing keys supported by @ref readline.
 */
void cmd_help()
{
    print_str("\nViperOS Shell Commands:\n\n");
    print_str("  chdir [path]   - Change directory (default: /)\n");
    print_str("  cwd            - Print current working directory\n");
    print_str("  Dir [path]     - Brief directory listing\n");
    print_str("  List [path]    - Detailed directory listing\n");
    print_str("  Type <file>    - Display file contents\n");
    print_str("  Copy           - Copy files\n");
    print_str("  Delete         - Delete files/directories\n");
    print_str("  MakeDir        - Create directory\n");
    print_str("  Rename         - Rename files\n");
    print_str("  Cls            - Clear screen\n");
    print_str("  Echo [text]    - Print text\n");
    print_str("  Fetch <host>   - Fetch webpage (HTTP/HTTPS)\n");
    print_str("  Version        - Show system version\n");
    print_str("  Uptime         - Show system uptime\n");
    print_str("  Avail          - Show memory availability\n");
    print_str("  Status         - Show running tasks\n");
    print_str("  Run <path>     - Execute program\n");
    print_str("  Read <cmd>     - Run command with paged output\n");
    print_str("  Caps [handle]  - Show capabilities (derive/revoke test)\n");
    print_str("  Date           - Show current date\n");
    print_str("  Time           - Show current time\n");
    print_str("  Assign         - Manage logical devices\n");
    print_str("  Path           - Manage command path\n");
    print_str("  History        - Show command history\n");
    print_str("  Why            - Explain last error\n");
    print_str("  Help           - Show this help\n");
    print_str("  EndShell       - Exit shell\n");
    print_str("\nReturn Codes: OK=0, WARN=5, ERROR=10, FAIL=20\n");
    print_str("\nLine Editing:\n");
    print_str("  Left/Right     - Move cursor\n");
    print_str("  Up/Down        - History navigation\n");
    print_str("  Home/End       - Jump to start/end\n");
    print_str("  Tab            - Command completion\n");
    print_str("  Ctrl+U         - Clear line\n");
    print_str("  Ctrl+K         - Kill to end\n");
    print_str("\n");
}

/**
 * @brief Print the current command history buffer.
 *
 * @details
 * Iterates over the history ring and prints commands in chronological order
 * (oldest available to newest). This is primarily useful for debugging the
 * line editor and for user convenience.
 */
void cmd_history()
{
    usize first = (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
    for (usize i = first; i < history_count; i++)
    {
        print_str("  ");
        put_num(static_cast<i64>(i + 1));
        print_str("  ");
        print_str(history_get(i));
        print_str("\n");
    }
}

/**
 * @brief Clear the screen using ANSI escape sequences.
 *
 * @details
 * Sends `ESC[2J` (clear screen) and `ESC[H` (home cursor) to the console.
 * This assumes the console understands a basic ANSI/VT100 subset.
 */
void cmd_cls()
{
    // ANSI escape sequence to clear screen
    print_str("\033[2J\033[H");
    last_rc = RC_OK;
}

/**
 * @brief Echo the provided arguments back to the console.
 *
 * @details
 * If `args` is null, prints only a newline. This is a minimal diagnostic
 * command useful for testing console output and line parsing.
 *
 * @param args Pointer to the argument substring (may be null).
 */
void cmd_echo(const char *args)
{
    if (args)
    {
        print_str(args);
    }
    print_str("\n");
    last_rc = RC_OK;
}

/**
 * @brief Print the ViperOS version banner.
 *
 * @details
 * This command prints a static version string and platform identifier. It does
 * not currently query the kernel for build information; it is updated manually
 * as part of bring-up.
 */
void cmd_version()
{
    print_str("ViperOS 0.2.0 (December 2025)\n");
    print_str("Platform: AArch64\n");
    last_rc = RC_OK;
}

/**
 * @brief Display the system uptime in human-readable form.
 *
 * @details
 * Calls the `SYS_UPTIME` syscall (via @ref sys::uptime) to get a monotonic tick
 * count, interprets it as milliseconds, and prints a days/hours/minutes/seconds
 * breakdown. If the kernel changes the unit of `SYS_UPTIME`, this output would
 * need to be updated accordingly.
 */
void cmd_uptime()
{
    u64 ms = sys::uptime();
    u64 secs = ms / 1000;
    u64 mins = secs / 60;
    u64 hours = mins / 60;
    u64 days = hours / 24;

    print_str("Uptime: ");
    if (days > 0)
    {
        put_num(static_cast<i64>(days));
        print_str(" day");
        if (days != 1)
            print_str("s");
        print_str(", ");
    }
    if (hours > 0 || days > 0)
    {
        put_num(static_cast<i64>(hours % 24));
        print_str(" hour");
        if ((hours % 24) != 1)
            print_str("s");
        print_str(", ");
    }
    put_num(static_cast<i64>(mins % 60));
    print_str(" minute");
    if ((mins % 60) != 1)
        print_str("s");
    print_str(", ");
    put_num(static_cast<i64>(secs % 60));
    print_str(" second");
    if ((secs % 60) != 1)
        print_str("s");
    print_str("\n");
    last_rc = RC_OK;
}

/**
 * @brief Explain the most recent command error.
 *
 * @details
 * The shell stores a "last return code" and optional string explanation after
 * each command. `Why` prints those values in a user-friendly form to help
 * diagnose failures.
 */
void cmd_why()
{
    if (last_rc == RC_OK)
    {
        print_str("No error.\n");
    }
    else
    {
        print_str("Last return code: ");
        put_num(last_rc);
        if (last_error)
        {
            print_str(" - ");
            print_str(last_error);
        }
        print_str("\n");
    }
}

/**
 * @brief Change the current working directory.
 *
 * @details
 * If no path is given, changes to the root directory.
 * Supports both absolute and relative paths.
 *
 * @param args Path argument (may be null for root).
 */
void cmd_cd(const char *args)
{
    const char *path = "/";
    if (args && args[0])
    {
        path = args;
    }

    i32 result = sys::chdir(path);
    if (result < 0)
    {
        print_str("CD: ");
        print_str(path);
        print_str(": No such directory\n");
        last_rc = RC_ERROR;
        last_error = "Directory not found";
        return;
    }

    // Refresh the current_dir from the kernel
    refresh_current_dir();
    last_rc = RC_OK;
}

/**
 * @brief Print the current working directory.
 *
 * @details
 * Gets the CWD from the kernel and prints it.
 */
void cmd_pwd()
{
    char buf[256];
    i64 len = sys::getcwd(buf, sizeof(buf));
    if (len < 0)
    {
        print_str("PWD: Failed to get current directory\n");
        last_rc = RC_ERROR;
        last_error = "getcwd failed";
        return;
    }
    print_str(buf);
    print_str("\n");
    last_rc = RC_OK;
}

/**
 * @brief Show physical memory availability information.
 *
 * @details
 * Calls `SYS_MEM_INFO` and prints a table similar to AmigaOS `Avail`. The
 * kernel provides page and byte counts; this command formats them into KiB for
 * readability and also prints page-level statistics.
 */
void cmd_avail()
{
    MemInfo info;
    if (sys::mem_info(&info) != 0)
    {
        print_str("AVAIL: Failed to get memory info\n");
        last_rc = RC_ERROR;
        last_error = "Memory info syscall failed";
        return;
    }

    print_str("\nType      Available         In-Use          Total\n");
    print_str("-------  ----------     ----------     ----------\n");

    // Display memory in KB for readability
    u64 free_kb = info.free_bytes / 1024;
    u64 used_kb = info.used_bytes / 1024;
    u64 total_kb = info.total_bytes / 1024;

    print_str("chip     ");

    // Free KB (right-aligned in 10 chars)
    char buf[16];
    int pos = 0;
    u64 n = free_kb;
    do
    {
        buf[pos++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    for (int i = pos; i < 10; i++)
        print_char(' ');
    while (pos > 0)
        print_char(buf[--pos]);
    print_str(" K   ");

    // Used KB
    n = used_kb;
    pos = 0;
    do
    {
        buf[pos++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    for (int i = pos; i < 10; i++)
        print_char(' ');
    while (pos > 0)
        print_char(buf[--pos]);
    print_str(" K   ");

    // Total KB
    n = total_kb;
    pos = 0;
    do
    {
        buf[pos++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    for (int i = pos; i < 10; i++)
        print_char(' ');
    while (pos > 0)
        print_char(buf[--pos]);
    print_str(" K\n");

    print_str("\n");

    // Show page info
    print_str("Memory: ");
    put_num(static_cast<i64>(info.free_pages));
    print_str(" pages free (");
    put_num(static_cast<i64>(info.total_pages));
    print_str(" total, ");
    put_num(static_cast<i64>(info.page_size));
    print_str(" bytes/page)\n");

    last_rc = RC_OK;
}

/**
 * @brief Display a snapshot of running tasks/processes.
 *
 * @details
 * Calls `SYS_TASK_LIST` to request an array of @ref TaskInfo entries and then
 * prints a small status table including task ID, state, priority, name, and
 * some flags.
 *
 * Depending on kernel maturity, `SYS_TASK_LIST` may be unimplemented; in that
 * case this command will report failure.
 */
void cmd_status()
{
    TaskInfo tasks[16];
    i32 count = sys::task_list(tasks, 16);

    if (count < 0)
    {
        print_str("STATUS: Failed to get task list\n");
        last_rc = RC_ERROR;
        last_error = "Task list syscall failed";
        return;
    }

    print_str("\nProcess Status:\n\n");
    print_str("  ID  State     Pri  Name\n");
    print_str("  --  --------  ---  --------------------------------\n");

    for (i32 i = 0; i < count; i++)
    {
        TaskInfo &t = tasks[i];

        // ID (right-aligned, 3 chars)
        print_str("  ");
        if (t.id < 10)
            print_str(" ");
        if (t.id < 100)
            print_str(" ");
        put_num(t.id);
        print_str("  ");

        // State
        switch (t.state)
        {
            case TASK_STATE_READY:
                print_str("Ready   ");
                break;
            case TASK_STATE_RUNNING:
                print_str("Running ");
                break;
            case TASK_STATE_BLOCKED:
                print_str("Blocked ");
                break;
            case TASK_STATE_EXITED:
                print_str("Exited  ");
                break;
            default:
                print_str("Unknown ");
                break;
        }
        print_str("  ");

        // Priority (right-aligned, 3 chars)
        if (t.priority < 10)
            print_str(" ");
        if (t.priority < 100)
            print_str(" ");
        put_num(t.priority);
        print_str("  ");

        // Name
        print_str(t.name);

        // Flags
        if (t.flags & TASK_FLAG_IDLE)
        {
            print_str(" [idle]");
        }
        if (t.flags & TASK_FLAG_KERNEL)
        {
            print_str(" [kernel]");
        }

        print_str("\n");
    }

    print_str("\n");
    put_num(count);
    print_str(" task");
    if (count != 1)
        print_str("s");
    print_str(" total\n");

    last_rc = RC_OK;
}

/**
 * @brief `Run` command: spawn and execute a program from the filesystem.
 *
 * @details
 * Uses the `SYS_TASK_SPAWN` syscall to load and execute an ELF binary from
 * the specified path. The spawned process runs concurrently with vinit.
 *
 * @param path Path to the ELF executable to run.
 */
void cmd_run(const char *path)
{
    if (!path || *path == '\0')
    {
        print_str("Run: missing program path\n");
        last_rc = RC_ERROR;
        last_error = "No path specified";
        return;
    }

    u64 pid = 0;
    u64 tid = 0;
    i64 err = sys::spawn(path, nullptr, &pid, &tid);

    if (err < 0)
    {
        print_str("Run: failed to spawn \"");
        print_str(path);
        print_str("\" (error ");
        put_num(err);
        print_str(")\n");
        last_rc = RC_FAIL;
        last_error = "Spawn failed";
        return;
    }

    print_str("Started process ");
    put_num(static_cast<i64>(pid));
    print_str(" (task ");
    put_num(static_cast<i64>(tid));
    print_str(")\n");

    // Wait for the child process to exit
    i32 status = 0;
    i64 exited_pid = sys::waitpid(pid, &status);

    if (exited_pid < 0)
    {
        print_str("Run: wait failed (error ");
        put_num(exited_pid);
        print_str(")\n");
        last_rc = RC_FAIL;
        last_error = "Wait failed";
        return;
    }

    print_str("Process ");
    put_num(exited_pid);
    print_str(" exited with status ");
    put_num(static_cast<i64>(status));
    print_str("\n");
    last_rc = RC_OK;
}

/**
 * @brief Display capability table information and run small capability demos.
 *
 * @details
 * Without arguments, `Caps` enumerates the current process capability table
 * using `SYS_CAP_LIST` and prints each entry's handle value, kind, rights, and
 * generation.
 *
 * With an optional handle argument (e.g., `Caps 0x1234`), the command also:
 * - Queries the specific handle using `SYS_CAP_QUERY`.
 * - Attempts to derive a read-only handle as a demonstration.
 * - Revokes the derived handle and verifies that it is invalidated.
 *
 * Depending on kernel maturity, the capability syscalls may be unimplemented.
 * In that case the command reports failure.
 *
 * @param args Optional argument substring containing a handle value in hex.
 */
void cmd_caps(const char *args)
{
    // Get capability count first
    i32 count = sys::cap_list(nullptr, 0);
    if (count < 0)
    {
        print_str("CAPS: Failed to get capability list\n");
        last_rc = RC_ERROR;
        last_error = "Capability list syscall failed";
        return;
    }

    if (count == 0)
    {
        print_str("No capabilities registered.\n");
        last_rc = RC_OK;
        return;
    }

    // Get actual entries
    CapListEntry caps[32];
    i32 actual = sys::cap_list(caps, 32);

    if (actual < 0)
    {
        print_str("CAPS: Failed to list capabilities\n");
        last_rc = RC_ERROR;
        last_error = "Capability list syscall failed";
        return;
    }

    print_str("\nCapability Table:\n\n");
    print_str("  Handle   Kind        Rights       Gen\n");
    print_str("  ------   ---------   ---------    ---\n");

    for (i32 i = 0; i < actual; i++)
    {
        CapListEntry &c = caps[i];

        // Handle (hex)
        print_str("  ");
        put_hex(c.handle);
        print_str("  ");

        // Kind name (left-aligned, 10 chars)
        const char *kind_name = sys::cap_kind_name(c.kind);
        print_str(kind_name);
        usize klen = strlen(kind_name);
        while (klen < 10)
        {
            print_char(' ');
            klen++;
        }
        print_str("  ");

        // Rights string
        char rights_buf[16];
        sys::cap_rights_str(c.rights, rights_buf, sizeof(rights_buf));
        print_str(rights_buf);
        print_str("    ");

        // Generation
        put_num(c.generation);

        print_str("\n");
    }

    print_str("\n");
    put_num(actual);
    print_str(" capabilit");
    if (actual != 1)
        print_str("ies");
    else
        print_str("y");
    print_str(" total\n");

    // If args provided, try to query/derive specific handle
    if (args && *args != '\0')
    {
        // Parse handle number
        u32 handle = 0;
        const char *p = args;

        // Skip "0x" prefix if present
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        {
            p += 2;
        }

        // Parse hex number
        while (*p)
        {
            char c = *p;
            if (c >= '0' && c <= '9')
            {
                handle = handle * 16 + (c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                handle = handle * 16 + (c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                handle = handle * 16 + (c - 'A' + 10);
            }
            else
            {
                break;
            }
            p++;
        }

        print_str("\nQuerying handle ");
        put_hex(handle);
        print_str(":\n");

        CapInfo info;
        i32 result = sys::cap_query(handle, &info);
        if (result < 0)
        {
            print_str("  Error: Invalid or revoked handle\n");
        }
        else
        {
            print_str("  Kind: ");
            print_str(sys::cap_kind_name(info.kind));
            print_str("\n  Rights: ");
            char rbuf[16];
            sys::cap_rights_str(info.rights, rbuf, sizeof(rbuf));
            print_str(rbuf);
            print_str(" (0x");
            put_hex(info.rights);
            print_str(")\n  Generation: ");
            put_num(info.generation);
            print_str("\n");

            // Try to derive a read-only handle as a demo
            print_str("\n  Testing derive (read-only)... ");
            i32 derived = sys::cap_derive(handle, CAP_RIGHT_READ);
            if (derived < 0)
            {
                print_str("Failed (no DERIVE right or error)\n");
            }
            else
            {
                print_str("Success! New handle: ");
                put_hex(static_cast<u32>(derived));
                print_str("\n");

                // Query the derived handle
                CapInfo derived_info;
                if (sys::cap_query(static_cast<u32>(derived), &derived_info) == 0)
                {
                    print_str("  Derived rights: ");
                    char dbuf[16];
                    sys::cap_rights_str(derived_info.rights, dbuf, sizeof(dbuf));
                    print_str(dbuf);
                    print_str("\n");
                }

                // Revoke it to demonstrate revocation
                print_str("  Revoking derived handle... ");
                if (sys::cap_revoke(static_cast<u32>(derived)) == 0)
                {
                    print_str("Success!\n");

                    // Verify it's gone
                    print_str("  Verifying revocation... ");
                    if (sys::cap_query(static_cast<u32>(derived), &derived_info) < 0)
                    {
                        print_str("Handle correctly invalidated.\n");
                    }
                    else
                    {
                        print_str("Warning: Handle still valid?\n");
                    }
                }
                else
                {
                    print_str("Failed!\n");
                }
            }
        }
    }

    last_rc = RC_OK;
}

/**
 * @brief Display the current date (placeholder).
 *
 * @details
 * The kernel does not yet provide a wall-clock time syscall. This command
 * exists as a placeholder and prints a message to that effect.
 */
void cmd_date()
{
    // TODO: Implement when date/time syscall is available
    print_str("DATE: Date/time not yet available\n");
    last_rc = RC_OK;
}

/**
 * @brief Display the current time (placeholder).
 *
 * @details
 * Like @ref cmd_date, this is a placeholder until a proper time-of-day API is
 * available.
 */
void cmd_time()
{
    // TODO: Implement when date/time syscall is available
    print_str("TIME: Date/time not yet available\n");
    last_rc = RC_OK;
}

/**
 * @brief List or manage logical device assigns.
 *
 * @details
 * With no arguments, `Assign` lists all assign mappings provided by the kernel
 * using `SYS_ASSIGN_LIST`.
 *
 * The argument-parsing and mutation (`Assign NAME: DIR`) are currently
 * unimplemented in `vinit`; the command prints usage and returns a warning in
 * that case.
 *
 * @param args Optional argument substring.
 */
void cmd_assign(const char *args)
{
    if (!args || *args == '\0')
    {
        // List all assigns using real syscall
        sys::AssignInfo assigns[16];
        usize count = 0;

        i32 result = sys::assign_list(assigns, 16, &count);
        if (result < 0)
        {
            print_str("Assign: failed to list assigns\n");
            last_rc = RC_ERROR;
            return;
        }

        print_str("Current assigns:\n");
        print_str("  Name         Handle     Kind        Rights     Flags\n");
        print_str("  -----------  ---------  ----------  ---------  ------\n");

        for (usize i = 0; i < count; i++)
        {
            // Name (left-aligned, 11 chars)
            print_str("  ");
            print_str(assigns[i].name);
            print_str(":");
            usize namelen = strlen(assigns[i].name) + 1;
            while (namelen < 11)
            {
                print_char(' ');
                namelen++;
            }
            print_str("  ");

            // Handle
            put_hex(assigns[i].handle);
            print_str("   ");

            // Query capability info
            CapInfo cap_info;
            if (sys::cap_query(assigns[i].handle, &cap_info) == 0)
            {
                // Kind
                const char *kind = sys::cap_kind_name(cap_info.kind);
                print_str(kind);
                usize klen = strlen(kind);
                while (klen < 10)
                {
                    print_char(' ');
                    klen++;
                }
                print_str("  ");

                // Rights
                char rights[16];
                sys::cap_rights_str(cap_info.rights, rights, sizeof(rights));
                print_str(rights);
                print_str("  ");
            }
            else
            {
                print_str("(invalid)   ");
                print_str("---------  ");
            }

            // Flags
            if (assigns[i].flags & sys::ASSIGN_SYSTEM)
            {
                print_str("SYS");
            }
            if (assigns[i].flags & sys::ASSIGN_MULTI)
            {
                if (assigns[i].flags & sys::ASSIGN_SYSTEM)
                    print_str(",");
                print_str("MULTI");
            }
            if (assigns[i].flags == 0)
            {
                print_str("-");
            }
            print_str("\n");
        }

        if (count == 0)
        {
            print_str("  (no assigns defined)\n");
        }

        print_str("\n");
        put_num(static_cast<i64>(count));
        print_str(" assign");
        if (count != 1)
            print_str("s");
        print_str(" defined\n");

        last_rc = RC_OK;
    }
    else
    {
        // Parse: NAME: handle or just NAME:
        // For now just show usage
        print_str("Usage: Assign           - List all assigns\n");
        print_str("       Assign NAME: DIR - Set assign (not yet implemented)\n");
        last_rc = RC_WARN;
    }
}

/**
 * @brief Resolve and inspect an assign-prefixed path.
 *
 * @details
 * With no arguments, prints the current prompt path (currently a static `SYS:`).
 *
 * With an argument, uses `SYS_ASSIGN_RESOLVE` to resolve an assign-prefixed
 * path (e.g., `SYS:certs/roots.der`) into a capability handle and prints:
 * - The resolved handle value.
 * - The capability kind and rights (via `SYS_CAP_QUERY`).
 * - For file handles: file size and a small text preview.
 * - For directory handles: the number of entries encountered by enumeration.
 *
 * The command closes the resolved handle before returning.
 *
 * @param args Path string to resolve (may be null/empty).
 */
void cmd_path(const char *args)
{
    if (!args || *args == '\0')
    {
        // Show current directory
        print_str("Current path: SYS:\n");
        last_rc = RC_OK;
    }
    else
    {
        // Resolve the given path using assign syscall
        u32 handle = 0;
        i32 result = sys::assign_resolve(args, &handle);
        if (result < 0)
        {
            print_str("Path: cannot resolve \"");
            print_str(args);
            print_str("\" - not found or invalid assign\n");
            last_rc = RC_ERROR;
            return;
        }

        print_str("Path \"");
        print_str(args);
        print_str("\"\n");
        print_str("  Handle: ");
        put_hex(handle);
        print_str("\n");

        // Query capability info
        CapInfo cap_info;
        if (sys::cap_query(handle, &cap_info) == 0)
        {
            print_str("  Kind:   ");
            print_str(sys::cap_kind_name(cap_info.kind));
            print_str("\n");

            print_str("  Rights: ");
            char rights[16];
            sys::cap_rights_str(cap_info.rights, rights, sizeof(rights));
            print_str(rights);
            print_str("\n");

            // If it's a file, show size and first bytes
            if (cap_info.kind == CAP_KIND_FILE)
            {
                // Seek to end to get size
                i64 size = sys::io_seek(handle, 0, sys::SEEK_END);
                if (size >= 0)
                {
                    print_str("  Size:   ");
                    put_num(size);
                    print_str(" bytes\n");

                    // Seek back to start and read first bytes
                    sys::io_seek(handle, 0, sys::SEEK_SET);
                    char preview[65];
                    i64 bytes = sys::io_read(handle, preview, 64);
                    if (bytes > 0)
                    {
                        preview[bytes] = '\0';
                        // Show first line or truncate
                        print_str("  Preview: \"");
                        for (i64 i = 0; i < bytes && i < 40; i++)
                        {
                            char c = preview[i];
                            if (c == '\n' || c == '\r')
                                break;
                            if (c >= 32 && c < 127)
                            {
                                print_char(c);
                            }
                            else
                            {
                                print_char('.');
                            }
                        }
                        print_str("...\"\n");
                    }
                }
            }

            // If it's a directory, show entry count
            if (cap_info.kind == CAP_KIND_DIRECTORY)
            {
                sys::FsDirEnt entry;
                int count = 0;
                while (sys::fs_read_dir(handle, &entry) > 0)
                {
                    count++;
                }
                print_str("  Entries: ");
                put_num(count);
                print_str("\n");
            }
        }

        // Close the handle
        sys::fs_close(handle);
        last_rc = RC_OK;
    }
}

/**
 * @brief `Dir` command: brief directory listing (Amiga-style).
 *
 * @details
 * Opens the directory at `path` using the path-based file descriptor API and
 * calls `SYS_READDIR` once to fetch a packed list of directory entries.
 *
 * The output format is intentionally simple: entries are printed in a fixed
 * three-column layout and directories are suffixed with `/`.
 *
 * @param path Directory path. If null/empty, defaults to `/`.
 */
void cmd_dir(const char *path)
{
    // Default to current directory
    if (!path || *path == '\0')
    {
        path = current_dir;
    }

    i32 fd = sys::open(path, sys::O_RDONLY);
    if (fd < 0)
    {
        print_str("Dir: cannot open \"");
        print_str(path);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "Directory not found";
        return;
    }

    // Buffer for directory entries
    u8 buf[2048];
    i64 bytes = sys::readdir(fd, buf, sizeof(buf));

    if (bytes < 0)
    {
        print_str("Dir: not a directory\n");
        sys::close(fd);
        last_rc = RC_ERROR;
        last_error = "Not a directory";
        return;
    }

    // Count entries and display in Amiga style (3 columns)
    usize offset = 0;
    usize count = 0;
    usize col = 0;

    while (offset < static_cast<usize>(bytes))
    {
        sys::DirEnt *ent = reinterpret_cast<sys::DirEnt *>(buf + offset);

        // Print name (directories get special treatment)
        if (ent->type == 2)
        {
            print_str("  ");
            print_str(ent->name);
            print_str("/");
            // Pad to 20 chars
            usize namelen = strlen(ent->name) + 1;
            while (namelen < 18)
            {
                print_char(' ');
                namelen++;
            }
        }
        else
        {
            print_str("  ");
            print_str(ent->name);
            usize namelen = strlen(ent->name);
            while (namelen < 18)
            {
                print_char(' ');
                namelen++;
            }
        }

        col++;
        if (col >= 3)
        {
            print_str("\n");
            col = 0;
        }

        count++;
        offset += ent->reclen;
    }

    if (col > 0)
        print_str("\n");

    put_num(static_cast<i64>(count));
    print_str(" entries\n");

    sys::close(fd);
    last_rc = RC_OK;
}

/**
 * @brief `List` command: detailed directory listing (Amiga-style).
 *
 * @details
 * Similar to @ref cmd_dir, but prints one entry per line with a wider name
 * column and a very small "flags" placeholder. The current implementation
 * reads directory entries in one syscall and does not paginate.
 *
 * @param path Directory path. If null/empty, defaults to `/`.
 */
void cmd_list(const char *path)
{
    // Default to current directory
    if (!path || *path == '\0')
    {
        path = current_dir;
    }

    i32 fd = sys::open(path, sys::O_RDONLY);
    if (fd < 0)
    {
        print_str("List: cannot open \"");
        print_str(path);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "Directory not found";
        return;
    }

    // Buffer for directory entries
    u8 buf[2048];
    i64 bytes = sys::readdir(fd, buf, sizeof(buf));

    if (bytes < 0)
    {
        print_str("List: not a directory\n");
        sys::close(fd);
        last_rc = RC_ERROR;
        last_error = "Not a directory";
        return;
    }

    print_str("Directory \"");
    print_str(path);
    print_str("\"\n\n");

    // Iterate through entries
    usize offset = 0;
    usize file_count = 0;
    usize dir_count = 0;

    while (offset < static_cast<usize>(bytes))
    {
        sys::DirEnt *ent = reinterpret_cast<sys::DirEnt *>(buf + offset);

        // Print name with padding
        print_str(ent->name);
        usize namelen = strlen(ent->name);
        while (namelen < 32)
        {
            print_char(' ');
            namelen++;
        }

        // Print type and flags
        if (ent->type == 2)
        {
            print_str("  <dir>    rwed");
            dir_count++;
        }
        else
        {
            print_str("           rwed");
            file_count++;
        }
        print_str("\n");

        offset += ent->reclen;
    }

    print_str("\n");
    put_num(static_cast<i64>(file_count));
    print_str(" file");
    if (file_count != 1)
        print_str("s");
    print_str(", ");
    put_num(static_cast<i64>(dir_count));
    print_str(" director");
    if (dir_count != 1)
        print_str("ies");
    else
        print_str("y");
    print_str("\n");

    sys::close(fd);
    last_rc = RC_OK;
}

/**
 * @brief `Type` command: display file contents (Amiga-style `cat`).
 *
 * @details
 * Opens the file at `path` read-only and streams its contents to the console.
 * Output is raw: binary bytes may be printed as-is and could contain escape
 * sequences.
 *
 * @param path File path to display. Must be non-null and non-empty.
 */
void cmd_type(const char *path)
{
    if (!path || *path == '\0')
    {
        print_str("Type: missing file argument\n");
        last_rc = RC_ERROR;
        last_error = "Missing filename";
        return;
    }

    i32 fd = sys::open(path, sys::O_RDONLY);
    if (fd < 0)
    {
        print_str("Type: cannot open \"");
        print_str(path);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "File not found";
        return;
    }

    // Read and print file contents
    char buf[512];
    while (true)
    {
        i64 bytes = sys::read(fd, buf, sizeof(buf) - 1);
        if (bytes <= 0)
            break;

        buf[bytes] = '\0';
        print_str(buf);
    }

    // Ensure output ends with a newline for clean prompt display
    print_str("\n");

    sys::close(fd);
    last_rc = RC_OK;
}

/**
 * @brief `Copy` command: copy a file from one path to another.
 *
 * @details
 * This is a simple file copy implementation intended for bring-up:
 * - Parses two path arguments, optionally accepting the keyword `TO`.
 * - Opens the source read-only and destination write/create.
 * - Copies data in fixed-size chunks.
 * - On failure, attempts to delete the partially created destination.
 *
 * It does not currently preserve timestamps, permissions, or copy directories.
 *
 * @param args Argument substring containing `source` and `dest`.
 */
void cmd_copy(const char *args)
{
    if (!args || *args == '\0')
    {
        print_str("Copy: missing arguments\n");
        print_str("Usage: Copy <source> <dest>\n");
        print_str("       Copy <source> TO <dest>\n");
        last_rc = RC_ERROR;
        last_error = "Missing arguments";
        return;
    }

    // Parse: source dest  or  source TO dest
    const char *p = args;
    while (*p && *p != ' ')
        p++;

    if (*p != ' ')
    {
        print_str("Copy: missing destination\n");
        print_str("Usage: Copy <source> <dest>\n");
        last_rc = RC_ERROR;
        last_error = "Missing destination";
        return;
    }

    // Copy source path
    char src_path[256];
    usize src_len = p - args;
    if (src_len >= 256)
        src_len = 255;
    for (usize i = 0; i < src_len; i++)
    {
        src_path[i] = args[i];
    }
    src_path[src_len] = '\0';

    // Skip spaces
    while (*p == ' ')
        p++;

    // Skip optional "TO" keyword (case insensitive)
    if ((p[0] == 'T' || p[0] == 't') && (p[1] == 'O' || p[1] == 'o') && p[2] == ' ')
    {
        p += 3;
        while (*p == ' ')
            p++;
    }

    if (*p == '\0')
    {
        print_str("Copy: missing destination\n");
        last_rc = RC_ERROR;
        last_error = "Missing destination";
        return;
    }

    const char *dst_path = p;

    // Open source file for reading
    i32 src_fd = sys::open(src_path, sys::O_RDONLY);
    if (src_fd < 0)
    {
        print_str("Copy: cannot open source \"");
        print_str(src_path);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "Cannot open source file";
        return;
    }

    // Get source file size for progress
    sys::Stat st;
    if (sys::fstat(src_fd, &st) < 0)
    {
        print_str("Copy: cannot stat source file\n");
        sys::close(src_fd);
        last_rc = RC_ERROR;
        last_error = "Cannot stat source";
        return;
    }

    // Open/create destination file
    i32 dst_fd = sys::open(dst_path, sys::O_WRONLY | sys::O_CREAT);
    if (dst_fd < 0)
    {
        print_str("Copy: cannot create destination \"");
        print_str(dst_path);
        print_str("\"\n");
        sys::close(src_fd);
        last_rc = RC_ERROR;
        last_error = "Cannot create destination";
        return;
    }

    // Copy data in chunks
    char buf[512];
    u64 total_copied = 0;
    bool error = false;

    while (true)
    {
        i64 bytes_read = sys::read(src_fd, buf, sizeof(buf));
        if (bytes_read < 0)
        {
            print_str("Copy: read error\n");
            error = true;
            break;
        }
        if (bytes_read == 0)
        {
            break; // EOF
        }

        // Write all read data (handle partial writes)
        u64 written = 0;
        while (written < static_cast<u64>(bytes_read))
        {
            i64 w = sys::write(dst_fd, buf + written, bytes_read - written);
            if (w < 0)
            {
                print_str("Copy: write error\n");
                error = true;
                break;
            }
            if (w == 0)
            {
                print_str("Copy: write returned 0 (disk full?)\n");
                error = true;
                break;
            }
            written += w;
        }

        if (error)
            break;
        total_copied += written;
    }

    sys::close(src_fd);
    sys::close(dst_fd);

    if (error)
    {
        // Try to clean up partial copy
        sys::unlink(dst_path);
        last_rc = RC_ERROR;
        last_error = "Copy failed";
        return;
    }

    print_str("Copied ");
    put_num(static_cast<i64>(total_copied));
    print_str(" bytes: ");
    print_str(src_path);
    print_str(" -> ");
    print_str(dst_path);
    print_str("\n");
    last_rc = RC_OK;
}

/**
 * @brief `Delete` command: delete a file or directory.
 *
 * @details
 * Attempts to delete the path in two steps:
 * 1. Call `SYS_UNLINK` (file deletion).
 * 2. If that fails, call `SYS_RMDIR` (directory removal).
 *
 * This matches typical shell ergonomics where users want a single command that
 * works for both files and (empty) directories. It does not recursively delete
 * non-empty directories.
 *
 * @param args Path to delete.
 */
void cmd_delete(const char *args)
{
    if (!args || *args == '\0')
    {
        print_str("Delete: missing file argument\n");
        print_str("Usage: Delete <file>\n");
        last_rc = RC_ERROR;
        last_error = "Missing filename";
        return;
    }

    // Try unlink first (for files)
    i32 result = sys::unlink(args);
    if (result == 0)
    {
        print_str("Deleted: ");
        print_str(args);
        print_str("\n");
        last_rc = RC_OK;
        return;
    }

    // Try rmdir (for directories)
    result = sys::rmdir(args);
    if (result == 0)
    {
        print_str("Deleted directory: ");
        print_str(args);
        print_str("\n");
        last_rc = RC_OK;
        return;
    }

    print_str("Delete: cannot delete \"");
    print_str(args);
    print_str("\"\n");
    last_rc = RC_ERROR;
    last_error = "Delete failed";
}

/**
 * @brief `MakeDir` command: create a directory.
 *
 * @details
 * Calls `SYS_MKDIR` on the provided argument and prints a confirmation message.
 *
 * @param args Directory path to create.
 */
void cmd_makedir(const char *args)
{
    if (!args || *args == '\0')
    {
        print_str("MakeDir: missing directory name\n");
        print_str("Usage: MakeDir <dirname>\n");
        last_rc = RC_ERROR;
        last_error = "Missing directory name";
        return;
    }

    i32 result = sys::mkdir(args);
    if (result == 0)
    {
        print_str("Created directory: ");
        print_str(args);
        print_str("\n");
        last_rc = RC_OK;
    }
    else
    {
        print_str("MakeDir: cannot create \"");
        print_str(args);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "Directory creation failed";
    }
}

/**
 * @brief `Rename` command: rename or move a filesystem entry.
 *
 * @details
 * Parses two whitespace-separated paths and calls `SYS_RENAME`. This command is
 * intentionally simple and does not currently support quoting/escaping spaces.
 *
 * @param args Argument substring containing `<old> <new>`.
 */
void cmd_rename(const char *args)
{
    if (!args || *args == '\0')
    {
        print_str("Rename: missing arguments\n");
        print_str("Usage: Rename <old> <new>\n");
        last_rc = RC_ERROR;
        last_error = "Missing arguments";
        return;
    }

    // Parse: old_name new_name
    // Find first space to split arguments
    const char *p = args;
    while (*p && *p != ' ')
        p++;

    if (*p != ' ')
    {
        print_str("Rename: missing new name\n");
        print_str("Usage: Rename <old> <new>\n");
        last_rc = RC_ERROR;
        last_error = "Missing new name";
        return;
    }

    // Copy old name
    char old_name[256];
    usize old_len = p - args;
    if (old_len >= 256)
        old_len = 255;
    for (usize i = 0; i < old_len; i++)
    {
        old_name[i] = args[i];
    }
    old_name[old_len] = '\0';

    // Skip spaces
    while (*p == ' ')
        p++;

    if (*p == '\0')
    {
        print_str("Rename: missing new name\n");
        last_rc = RC_ERROR;
        last_error = "Missing new name";
        return;
    }

    const char *new_name = p;

    i32 result = sys::rename(old_name, new_name);
    if (result == 0)
    {
        print_str("Renamed: ");
        print_str(old_name);
        print_str(" -> ");
        print_str(new_name);
        print_str("\n");
        last_rc = RC_OK;
    }
    else
    {
        print_str("Rename: cannot rename \"");
        print_str(old_name);
        print_str("\" to \"");
        print_str(new_name);
        print_str("\"\n");
        last_rc = RC_ERROR;
        last_error = "Rename failed";
    }
}

/**
 * @brief Parsed URL components used by the `Fetch` command.
 *
 * @details
 * This is a deliberately small parser for HTTP/HTTPS demo code. It supports:
 * - Optional `http://` or `https://` schemes.
 * - Optional `:port` suffix.
 * - Optional `/path` component (defaults to `/`).
 *
 * It does not support query strings, fragments, IPv6 literals, or username/
 * password syntax.
 */
struct ParsedUrl
{
    char host[128]; /**< Hostname (NUL-terminated, truncated if needed). */
    u16 port;       /**< Port number (defaults to 80/443 based on scheme). */
    char path[256]; /**< Path portion beginning with `/` (defaults to `/`). */
    bool is_https;  /**< True when `https://` is selected. */
};

/**
 * @brief Parse a URL string into host/port/path fields.
 *
 * @details
 * Populates `out` with defaults and then parses:
 * - Scheme (`http://` or `https://`) to set default port and TLS mode.
 * - Hostname until `/` or `:` delimiter.
 * - Optional port following `:`.
 * - Optional path following `/`.
 *
 * @param url Input URL string (may be scheme-less for HTTP).
 * @param out Output structure to fill.
 * @return True if parsing produced a non-empty hostname; false otherwise.
 */
bool parse_url(const char *url, ParsedUrl *out)
{
    out->host[0] = '\0';
    out->port = 80;
    out->path[0] = '/';
    out->path[1] = '\0';
    out->is_https = false;

    const char *p = url;

    // Check for https://
    if (strstart(p, "https://"))
    {
        out->is_https = true;
        out->port = 443;
        p += 8;
    }
    else if (strstart(p, "http://"))
    {
        p += 7;
    }

    // Parse host
    usize host_len = 0;
    while (*p && *p != '/' && *p != ':' && host_len < 127)
    {
        out->host[host_len++] = *p++;
    }
    out->host[host_len] = '\0';

    if (host_len == 0)
        return false;

    // Parse port if present
    if (*p == ':')
    {
        p++;
        u16 port = 0;
        while (*p >= '0' && *p <= '9')
        {
            port = port * 10 + (*p - '0');
            p++;
        }
        if (port > 0)
            out->port = port;
    }

    // Parse path
    if (*p == '/')
    {
        usize path_len = 0;
        while (*p && path_len < 255)
        {
            out->path[path_len++] = *p++;
        }
        out->path[path_len] = '\0';
    }

    return true;
}

/**
 * @brief `Fetch` command: perform a simple HTTP/HTTPS GET request.
 *
 * @details
 * This is a demo command intended to exercise the kernel networking stack:
 * - Resolves the hostname using `SYS_DNS_RESOLVE`.
 * - Creates and connects a TCP socket to the remote host.
 * - For HTTPS: creates a TLS session and performs a handshake.
 * - Sends a minimal HTTP/1.0 GET request and prints the response to console.
 *
 * TLS certificate verification is currently disabled in `vinit` for bring-up;
 * the command still prints negotiated TLS version/cipher information when
 * available.
 *
 * @param url URL or hostname to fetch. If no scheme is provided, HTTP is used.
 */
void cmd_fetch(const char *url)
{
    if (!url || *url == '\0')
    {
        print_str("Fetch: usage: Fetch <url>\n");
        print_str("  Examples:\n");
        print_str("    Fetch example.com\n");
        print_str("    Fetch http://example.com/page\n");
        print_str("    Fetch https://example.com\n");
        last_rc = RC_ERROR;
        last_error = "Missing URL";
        return;
    }

    // Parse URL
    ParsedUrl parsed;

    // If no protocol specified, treat as hostname
    if (!strstart(url, "http://") && !strstart(url, "https://"))
    {
        // Simple hostname - use HTTP
        usize i = 0;
        while (url[i] && url[i] != '/' && i < 127)
        {
            parsed.host[i] = url[i];
            i++;
        }
        parsed.host[i] = '\0';
        parsed.port = 80;
        parsed.path[0] = '/';
        parsed.path[1] = '\0';
        parsed.is_https = false;
    }
    else
    {
        if (!parse_url(url, &parsed))
        {
            print_str("Fetch: invalid URL\n");
            last_rc = RC_ERROR;
            last_error = "Invalid URL";
            return;
        }
    }

    print_str("Resolving ");
    print_str(parsed.host);
    print_str("...\n");

    // Resolve hostname
    u32 ip = 0;
    if (sys::dns_resolve(parsed.host, &ip) != 0)
    {
        print_str("Fetch: DNS resolution failed\n");
        last_rc = RC_ERROR;
        last_error = "DNS resolution failed";
        return;
    }

    print_str("Connecting to ");
    put_num((ip >> 24) & 0xFF);
    print_char('.');
    put_num((ip >> 16) & 0xFF);
    print_char('.');
    put_num((ip >> 8) & 0xFF);
    print_char('.');
    put_num(ip & 0xFF);
    print_char(':');
    put_num(parsed.port);
    if (parsed.is_https)
    {
        print_str(" (HTTPS)");
    }
    print_str("...\n");

    // Create socket
    i32 sock = sys::socket_create();
    if (sock < 0)
    {
        print_str("Fetch: failed to create socket\n");
        last_rc = RC_FAIL;
        last_error = "Socket creation failed";
        return;
    }

    // Connect
    if (sys::socket_connect(sock, ip, parsed.port) != 0)
    {
        print_str("Fetch: connection failed\n");
        sys::socket_close(sock);
        last_rc = RC_ERROR;
        last_error = "Connection failed";
        return;
    }

    print_str("Connected!");

    // For HTTPS, perform TLS handshake
    i32 tls_session = -1;
    if (parsed.is_https)
    {
        print_str(" Starting TLS handshake...\n");

        // Pass false for verify - certificate verification not yet implemented
        tls_session = sys::tls_create(sock, parsed.host, false);
        if (tls_session < 0)
        {
            print_str("Fetch: TLS session creation failed\n");
            sys::socket_close(sock);
            last_rc = RC_ERROR;
            last_error = "TLS creation failed";
            return;
        }

        if (sys::tls_handshake(tls_session) != 0)
        {
            print_str("Fetch: TLS handshake failed\n");
            sys::tls_close(tls_session);
            sys::socket_close(sock);
            last_rc = RC_ERROR;
            last_error = "TLS handshake failed";
            return;
        }

        print_str("TLS handshake complete. ");
    }

    print_str(" Sending request...\n");

    // Build HTTP request
    char request[512];
    usize pos = 0;

    // GET path HTTP/1.0
    const char *get = "GET ";
    while (*get)
        request[pos++] = *get++;
    const char *path = parsed.path;
    while (*path && pos < 400)
        request[pos++] = *path++;
    const char *proto = " HTTP/1.0\r\nHost: ";
    while (*proto)
        request[pos++] = *proto++;
    const char *host = parsed.host;
    while (*host && pos < 450)
        request[pos++] = *host++;
    const char *tail = "\r\nUser-Agent: ViperOS/0.2\r\nConnection: close\r\n\r\n";
    while (*tail)
        request[pos++] = *tail++;
    request[pos] = '\0';

    // Send request (TLS or plain)
    i64 sent;
    if (parsed.is_https)
    {
        sent = sys::tls_send(tls_session, request, pos);
    }
    else
    {
        sent = sys::socket_send(sock, request, pos);
    }

    if (sent <= 0)
    {
        print_str("Fetch: send failed\n");
        if (parsed.is_https)
            sys::tls_close(tls_session);
        sys::socket_close(sock);
        last_rc = RC_ERROR;
        last_error = "Send failed";
        return;
    }

    print_str("Request sent, receiving response...\n\n");

    // Receive response
    char buf[512];
    usize total = 0;
    for (int tries = 0; tries < 100; tries++)
    {
        i64 n;
        if (parsed.is_https)
        {
            n = sys::tls_recv(tls_session, buf, sizeof(buf) - 1);
        }
        else
        {
            n = sys::socket_recv(sock, buf, sizeof(buf) - 1);
        }

        if (n > 0)
        {
            buf[n] = '\0';
            print_str(buf);
            total += n;
        }
        else if (total > 0)
        {
            break; // Got some data, done
        }
        // Small delay to allow more data
        for (int i = 0; i < 100000; i++)
        {
            asm volatile("" ::: "memory");
        }
    }

    print_str("\n\n[Received ");
    put_num(static_cast<i64>(total));
    print_str(" bytes");
    if (parsed.is_https)
    {
        print_str(", encrypted");
    }
    print_str("]\n");

    // Show TLS session info for HTTPS connections
    if (parsed.is_https)
    {
        TLSInfo tls_info_data;
        if (sys::tls_info(tls_session, &tls_info_data) == 0)
        {
            print_str("[TLS Info] ");

            // Protocol version
            if (tls_info_data.protocol_version == TLS_VERSION_1_3)
            {
                print_str("TLS 1.3");
            }
            else if (tls_info_data.protocol_version == TLS_VERSION_1_2)
            {
                print_str("TLS 1.2");
            }
            else
            {
                print_str("TLS ?");
            }

            // Cipher suite
            print_str(", Cipher: ");
            if (tls_info_data.cipher_suite == TLS_CIPHER_CHACHA20_POLY1305_SHA256)
            {
                print_str("CHACHA20-POLY1305");
            }
            else if (tls_info_data.cipher_suite == TLS_CIPHER_AES_128_GCM_SHA256)
            {
                print_str("AES-128-GCM");
            }
            else if (tls_info_data.cipher_suite == TLS_CIPHER_AES_256_GCM_SHA384)
            {
                print_str("AES-256-GCM");
            }
            else
            {
                print_str("0x");
                put_hex(tls_info_data.cipher_suite);
            }

            // Verified status
            print_str(", Verified: ");
            print_str(tls_info_data.verified ? "NO (NOVERIFY)" : "YES");

            print_str("\n");
        }

        sys::tls_close(tls_session);
    }
    sys::socket_close(sock);
    last_rc = RC_OK;
}

/** @} */

/**
 * @brief Return the argument substring following a command name.
 *
 * @details
 * Given an input line and the length of the command token, this helper:
 * - Skips the command portion.
 * - Skips one or more spaces.
 * - Returns a pointer to the first non-space character.
 *
 * If the line contains no characters beyond the command token (or only
 * whitespace), the function returns `nullptr`.
 *
 * @param line Full input line (NUL-terminated).
 * @param cmd_len Length of the command token in bytes.
 * @return Pointer into `line` where arguments begin, or `nullptr` if none.
 */
const char *get_args(const char *line, usize cmd_len)
{
    if (strlen(line) <= cmd_len)
        return nullptr;
    const char *args = line + cmd_len;
    while (*args == ' ')
        args++;
    if (*args == '\0')
        return nullptr;
    return args;
}

/**
 * @brief Main interactive shell loop.
 *
 * @details
 * Repeatedly:
 * 1. Prints an Amiga-style prompt.
 * 2. Reads a line with editing support via @ref readline.
 * 3. Records the line into the history ring.
 * 4. Dispatches to a matching `cmd_*` handler (case-insensitive).
 *
 * The loop terminates when the user enters `EndShell` (or common aliases such
 * as `exit`/`quit`).
 */
void shell_loop()
{
    char line[256];

    print_str("\n========================================\n");
    print_str("        ViperOS 0.2.0 Shell\n");
    print_str("========================================\n");
    print_str("Type 'Help' for available commands.\n\n");

    // Enable cursor visibility via ANSI escape sequence
    print_str("\x1B[?25h");

    // Initialize current_dir from kernel's CWD
    refresh_current_dir();

    while (true)
    {
        // Amiga-style prompt: SYS: for root, or path>
        if (current_dir[0] == '/' && current_dir[1] == '\0')
        {
            print_str("SYS:");
        }
        else
        {
            print_str("SYS:");
            print_str(current_dir);
        }
        print_str("> ");

        usize len = readline(line, sizeof(line));

        if (len == 0)
            continue;

        // Add to history
        history_add(line);

        // Check for "read" prefix to enable paging
        bool do_paging = false;
        char *cmd_line = line;
        if (strcasestart(line, "read "))
        {
            do_paging = true;
            cmd_line = const_cast<char *>(get_args(line, 5));
            if (!cmd_line || *cmd_line == '\0')
            {
                print_str("Read: missing command\n");
                print_str("Usage: read <command> - run command with paged output\n");
                last_rc = RC_ERROR;
                continue;
            }
            paging_start();
        }

        // Parse and execute command (case-insensitive)
        // Help
        if (strcaseeq(cmd_line, "help") || strcaseeq(cmd_line, "?"))
        {
            cmd_help();
        }
        // Cls (clear screen)
        else if (strcaseeq(cmd_line, "cls") || strcaseeq(cmd_line, "clear"))
        {
            cmd_cls();
        }
        // Echo
        else if (strcasestart(cmd_line, "echo ") || strcaseeq(cmd_line, "echo"))
        {
            cmd_echo(get_args(cmd_line, 5));
        }
        // Version (was uname)
        else if (strcaseeq(cmd_line, "version"))
        {
            cmd_version();
        }
        // Uptime
        else if (strcaseeq(cmd_line, "uptime"))
        {
            cmd_uptime();
        }
        // History
        else if (strcaseeq(cmd_line, "history"))
        {
            cmd_history();
        }
        // Why (explain last error)
        else if (strcaseeq(cmd_line, "why"))
        {
            cmd_why();
        }
        // chdir (change directory)
        else if (strcaseeq(cmd_line, "chdir") || strcasestart(cmd_line, "chdir "))
        {
            cmd_cd(get_args(cmd_line, 6));
        }
        // cwd (current working directory)
        else if (strcaseeq(cmd_line, "cwd"))
        {
            cmd_pwd();
        }
        // Avail (memory)
        else if (strcaseeq(cmd_line, "avail"))
        {
            cmd_avail();
        }
        // Status (processes)
        else if (strcaseeq(cmd_line, "status"))
        {
            cmd_status();
        }
        // Run (spawn program)
        else if (strcasestart(cmd_line, "run "))
        {
            cmd_run(get_args(cmd_line, 4));
        }
        else if (strcaseeq(cmd_line, "run"))
        {
            print_str("Run: missing program path\n");
            last_rc = RC_ERROR;
        }
        // Caps (capabilities)
        else if (strcaseeq(cmd_line, "caps") || strcasestart(cmd_line, "caps "))
        {
            cmd_caps(get_args(cmd_line, 5));
        }
        // Date
        else if (strcaseeq(cmd_line, "date"))
        {
            cmd_date();
        }
        // Time
        else if (strcaseeq(cmd_line, "time"))
        {
            cmd_time();
        }
        // Assign
        else if (strcasestart(cmd_line, "assign ") || strcaseeq(cmd_line, "assign"))
        {
            cmd_assign(get_args(cmd_line, 7));
        }
        // Path
        else if (strcasestart(cmd_line, "path ") || strcaseeq(cmd_line, "path"))
        {
            cmd_path(get_args(cmd_line, 5));
        }
        // Dir (brief listing)
        else if (strcaseeq(cmd_line, "dir") || strcasestart(cmd_line, "dir "))
        {
            cmd_dir(get_args(cmd_line, 4));
        }
        // List (detailed listing)
        else if (strcaseeq(cmd_line, "list") || strcasestart(cmd_line, "list "))
        {
            cmd_list(get_args(cmd_line, 5));
        }
        // Type (was cat)
        else if (strcasestart(cmd_line, "type "))
        {
            cmd_type(get_args(cmd_line, 5));
        }
        else if (strcaseeq(cmd_line, "type"))
        {
            print_str("Type: missing file argument\n");
            last_rc = RC_ERROR;
        }
        // Copy
        else if (strcasestart(cmd_line, "copy ") || strcaseeq(cmd_line, "copy"))
        {
            cmd_copy(get_args(cmd_line, 5));
        }
        // Delete
        else if (strcasestart(cmd_line, "delete ") || strcaseeq(cmd_line, "delete"))
        {
            cmd_delete(get_args(cmd_line, 7));
        }
        // MakeDir
        else if (strcasestart(cmd_line, "makedir ") || strcaseeq(cmd_line, "makedir"))
        {
            cmd_makedir(get_args(cmd_line, 8));
        }
        // Rename
        else if (strcasestart(cmd_line, "rename ") || strcaseeq(cmd_line, "rename"))
        {
            cmd_rename(get_args(cmd_line, 7));
        }
        // Fetch
        else if (strcasestart(cmd_line, "fetch "))
        {
            cmd_fetch(get_args(cmd_line, 6));
        }
        else if (strcaseeq(cmd_line, "fetch"))
        {
            print_str("Fetch: usage: Fetch <hostname>\n");
            last_rc = RC_ERROR;
        }
        // EndShell (was exit/quit)
        else if (strcaseeq(cmd_line, "endshell") || strcaseeq(cmd_line, "exit") || strcaseeq(cmd_line, "quit"))
        {
            print_str("Goodbye!\n");
            if (do_paging)
                paging_stop();
            break;
        }
        // Legacy command aliases for compatibility
        else if (strcaseeq(cmd_line, "ls") || strcasestart(cmd_line, "ls "))
        {
            print_str("Note: Use 'Dir' or 'List' instead of 'ls'\n");
            cmd_dir(get_args(cmd_line, 3));
        }
        else if (strcasestart(cmd_line, "cat "))
        {
            print_str("Note: Use 'Type' instead of 'cat'\n");
            cmd_type(get_args(cmd_line, 4));
        }
        else if (strcaseeq(cmd_line, "uname"))
        {
            print_str("Note: Use 'Version' instead of 'uname'\n");
            cmd_version();
        }
        else
        {
            print_str("Unknown command: ");
            print_str(cmd_line);
            print_str("\nType 'Help' for available commands.\n");
            last_rc = RC_WARN;
            last_error = "Unknown command";
        }

        // Stop paging if it was enabled
        if (do_paging)
        {
            paging_stop();
        }
    }
}

/**
 * @brief User-space entry point for the init process.
 *
 * @details
 * The kernel loads `vinit` as the first user-space program and transfers
 * control to `_start`. This function prints a small banner, shows a bring-up
 * "assigns" message (currently informational), and then runs the interactive
 * shell loop.
 *
 * When the shell exits, the process terminates via @ref sys::exit.
 */
// Simple sbrk wrapper for startup malloc test
static void *vinit_sbrk(long increment)
{
    sys::SyscallResult r = sys::syscall1(0x0A, static_cast<u64>(increment));
    if (r.error < 0)
    {
        return reinterpret_cast<void *>(-1);
    }
    return reinterpret_cast<void *>(r.val0);
}

// Quick malloc test at startup
static void test_malloc_at_startup()
{
    print_str("[vinit] Testing malloc/sbrk...\n");

    // Get initial break
    void *brk = vinit_sbrk(0);
    print_str("[vinit]   Initial heap: ");
    put_hex(reinterpret_cast<u64>(brk));
    print_str("\n");

    // Allocate 1KB
    void *ptr = vinit_sbrk(1024);
    if (ptr == reinterpret_cast<void *>(-1))
    {
        print_str("[vinit]   ERROR: sbrk(1024) failed!\n");
        return;
    }

    print_str("[vinit]   Allocated 1KB at: ");
    put_hex(reinterpret_cast<u64>(ptr));
    print_str("\n");

    // Write to it
    char *cptr = static_cast<char *>(ptr);
    for (int i = 0; i < 1024; i++)
    {
        cptr[i] = static_cast<char>(i & 0xFF);
    }

    // Verify
    bool ok = true;
    for (int i = 0; i < 1024; i++)
    {
        if (cptr[i] != static_cast<char>(i & 0xFF))
        {
            ok = false;
            break;
        }
    }

    if (ok)
    {
        print_str("[vinit]   Memory R/W test PASSED\n");
    }
    else
    {
        print_str("[vinit]   ERROR: Memory verification FAILED!\n");
    }
}

extern "C" void _start()
{
    print_str("========================================\n");
    print_str("  ViperOS 0.2.0 - Init Process\n");
    print_str("========================================\n\n");

    print_str("[vinit] Starting ViperOS...\n");
    print_str("[vinit] Loaded from SYS:viper\\vinit.vpr\n");
    print_str("[vinit] Setting up assigns...\n");
    print_str("  SYS: = D0:\\\n");
    print_str("  C:   = SYS:c\n");
    print_str("  S:   = SYS:s\n");
    print_str("  T:   = SYS:t\n");
    print_str("\n");

    // Run startup malloc test
    test_malloc_at_startup();

    // Run the shell
    shell_loop();

    print_str("[vinit] EndShell - Shutting down.\n");
    sys::exit(0);
}
