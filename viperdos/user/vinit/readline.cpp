/**
 * @file readline.cpp
 * @brief Line editing and command history for vinit shell.
 */
#include "vinit.hpp"

// External memmove from io.cpp
extern "C" void *memmove(void *dst, const void *src, usize n);

// =============================================================================
// Shell State
// =============================================================================

int last_rc = RC_OK;
const char *last_error = nullptr;
char current_dir[MAX_PATH_LEN] = "/";

/// @brief Refresh the current_dir buffer from the kernel working directory.
void refresh_current_dir() {
    if (sys::getcwd(current_dir, sizeof(current_dir)) < 0) {
        current_dir[0] = '/';
        current_dir[1] = '\0';
    }
}

// =============================================================================
// History
// =============================================================================

static char history[HISTORY_SIZE][HISTORY_LINE_LEN];
static usize history_count = 0;
static usize history_index = 0;

/// @brief Add a command line to the circular history buffer.
/// @param line Null-terminated command string (empty lines are ignored).
void history_add(const char *line) {
    if (strlen(line) == 0)
        return;

    // Don't add duplicates of the last command
    if (history_count > 0) {
        usize last = (history_count - 1) % HISTORY_SIZE;
        if (streq(history[last], line))
            return;
    }

    // Copy to history buffer
    usize idx = history_count % HISTORY_SIZE;
    usize i = 0;
    for (; line[i] && i < HISTORY_LINE_LEN - 1; i++) {
        history[idx][i] = line[i];
    }
    history[idx][i] = '\0';
    history_count++;
}

/// @brief Retrieve a command from history by absolute index.
/// @param index Zero-based absolute index into the history.
/// @return Pointer to the history string, or nullptr if out of range.
const char *history_get(usize index) {
    if (index >= history_count)
        return nullptr;
    usize first = (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
    if (index < first)
        return nullptr;
    return history[index % HISTORY_SIZE];
}

// =============================================================================
// Line Editing Helpers
// =============================================================================

/// @brief Redraw the edit buffer from a given cursor position to the end.
/// @param buf The line buffer contents.
/// @param len Current length of the line.
/// @param pos Cursor position from which to start redrawing.
static void redraw_line_from(const char *buf, usize len, usize pos) {
    for (usize i = pos; i < len; i++) {
        print_char(buf[i]);
    }
    print_char(' ');
    for (usize i = len + 1; i > pos; i--) {
        print_char('\b');
    }
}

/// @brief Move the terminal cursor left by n columns using ANSI escapes.
/// @param n Number of columns to move left.
static void cursor_left(usize n) {
    while (n--) {
        print_str("\033[D");
    }
}

/// @brief Move the terminal cursor right by n columns using ANSI escapes.
/// @param n Number of columns to move right.
static void cursor_right(usize n) {
    while (n--) {
        print_str("\033[C");
    }
}

/// @brief Replace the current edit buffer with a new string, updating display.
/// @param buf The line buffer to overwrite.
/// @param len Pointer to current line length (updated on return).
/// @param pos Pointer to current cursor position (updated on return).
/// @param newline Replacement string to display.
static void replace_line(char *buf, usize *len, usize *pos, const char *newline) {
    cursor_left(*pos);
    for (usize i = 0; i < *len; i++)
        print_char(' ');
    cursor_left(*len);
    *len = 0;
    *pos = 0;
    for (usize i = 0; newline[i] && i < 255; i++) {
        buf[i] = newline[i];
        print_char(newline[i]);
        (*len)++;
        (*pos)++;
    }
    buf[*len] = '\0';
}

// =============================================================================
// Tab Completion
// =============================================================================

static const char *commands[] = {"Assign", "Avail",  "Caps",    "chdir",  "Cls",    "Copy",
                                 "cwd",    "Date",   "Delete",  "Dir",    "Echo",   "EndShell",
                                 "Fetch",  "Help",   "History", "Info",   "List",   "MakeDir",
                                 "Path",   "Rename", "Run",     "RunFSD", "Status", "Time",
                                 "Type",   "Uptime", "Version", "Why"};
static const usize num_commands = sizeof(commands) / sizeof(commands[0]);

/// @brief Compute the length of the common prefix between two strings.
/// @return Number of leading characters that are identical.
static usize common_prefix(const char *a, const char *b) {
    usize i = 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return i;
}

// =============================================================================
// Input Abstraction
// =============================================================================

// Special key codes (negative values)
static constexpr i32 KEY_UP_ARROW = -103;
static constexpr i32 KEY_DOWN_ARROW = -108;
static constexpr i32 KEY_LEFT_ARROW = -105;
static constexpr i32 KEY_RIGHT_ARROW = -106;

/// @brief Get one input character, blocking until available.
/// @return Positive value for ASCII chars, negative for special key codes.
static i32 get_input_char() {
    if (is_console_ready()) {
        return getchar_from_console();
    } else {
        return static_cast<i32>(static_cast<u8>(sys::getchar()));
    }
}

/// @brief Try to get one input character without blocking (for CRLF handling).
/// @return Character value if available, or negative if none ready.
static i32 try_get_input_char() {
    if (is_console_ready()) {
        return try_getchar_from_console();
    } else {
        return sys::try_getchar();
    }
}

// =============================================================================
// Readline
// =============================================================================

/// @brief Read a line of input with full line-editing, history, and tab completion.
/// @param buf Buffer to store the resulting null-terminated line.
/// @param maxlen Maximum buffer capacity including the null terminator.
/// @return Length of the line (excluding the null terminator).
usize readline(char *buf, usize maxlen) {
    usize len = 0;
    usize pos = 0;

    static char saved_line[256];
    saved_line[0] = '\0';
    history_index = history_count;

    while (len < maxlen - 1) {
        i32 input = get_input_char();

        // Handle special keys from consoled (negative values)
        if (input < 0) {
            switch (input) {
                case KEY_UP_ARROW:
                    if (history_index > 0) {
                        if (history_index == history_count && len > 0) {
                            for (usize i = 0; i <= len; i++)
                                saved_line[i] = buf[i];
                        }
                        history_index--;
                        usize first =
                            (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
                        if (history_index >= first) {
                            const char *hist = history_get(history_index);
                            if (hist)
                                replace_line(buf, &len, &pos, hist);
                        }
                    }
                    continue;
                case KEY_DOWN_ARROW:
                    if (history_index < history_count) {
                        history_index++;
                        if (history_index == history_count) {
                            replace_line(buf, &len, &pos, saved_line);
                        } else {
                            const char *hist = history_get(history_index);
                            if (hist)
                                replace_line(buf, &len, &pos, hist);
                        }
                    }
                    continue;
                case KEY_LEFT_ARROW:
                    if (pos > 0) {
                        cursor_left(1);
                        pos--;
                    }
                    continue;
                case KEY_RIGHT_ARROW:
                    if (pos < len) {
                        cursor_right(1);
                        pos++;
                    }
                    continue;
                default:
                    continue; // Unknown special key
            }
        }

        char c = static_cast<char>(input);

        // Handle escape sequences (only when using kernel console)
        if (c == '\033' && !is_console_ready()) {
            char c2 = sys::getchar();
            if (c2 == '[') {
                // Parse CSI sequence: ESC [ [params] final_char
                // xterm sends ESC[1;2A for Shift+Up, etc.
                char seq[16];
                int seq_len = 0;
                char c3;

                // Read until we get a letter (final character)
                while (seq_len < 15) {
                    c3 = sys::getchar();
                    if (c3 >= 'A' && c3 <= 'Z')
                        break; // Final character
                    if (c3 >= 'a' && c3 <= 'z')
                        break; // Final character
                    if (c3 == '~')
                        break; // Function key final
                    seq[seq_len++] = c3;
                }
                seq[seq_len] = '\0';

                // Check for Shift modifier (;2 before final char)
                bool shift = (seq_len >= 2 && seq[seq_len - 2] == ';' && seq[seq_len - 1] == '2');

                switch (c3) {
                    case 'A': // Up arrow (or Shift+Up for scroll)
                        if (shift) {
                            // Shift+Up: pass through to console for scrolling
                            // by echoing the sequence
                            print_str("\033[1;2A");
                            break;
                        }
                        if (history_index > 0) {
                            if (history_index == history_count && len > 0) {
                                for (usize i = 0; i <= len; i++)
                                    saved_line[i] = buf[i];
                            }
                            history_index--;
                            usize first =
                                (history_count > HISTORY_SIZE) ? (history_count - HISTORY_SIZE) : 0;
                            if (history_index >= first) {
                                const char *hist = history_get(history_index);
                                if (hist)
                                    replace_line(buf, &len, &pos, hist);
                            }
                        }
                        break;
                    case 'B': // Down arrow (or Shift+Down for scroll)
                        if (shift) {
                            // Shift+Down: pass through to console for scrolling
                            print_str("\033[1;2B");
                            break;
                        }
                        if (history_index < history_count) {
                            history_index++;
                            if (history_index == history_count) {
                                replace_line(buf, &len, &pos, saved_line);
                            } else {
                                const char *hist = history_get(history_index);
                                if (hist)
                                    replace_line(buf, &len, &pos, hist);
                            }
                        }
                        break;
                    case 'C': // Right arrow
                        if (pos < len) {
                            cursor_right(1);
                            pos++;
                        }
                        break;
                    case 'D': // Left arrow
                        if (pos > 0) {
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
                    case '3':               // Delete key
                        c = sys::getchar(); // consume '~'
                        if (pos < len) {
                            memmove(buf + pos, buf + pos + 1, len - pos);
                            len--;
                            redraw_line_from(buf, len, pos);
                        }
                        break;
                    case '5':           // Page Up
                    case '6':           // Page Down
                        sys::getchar(); // consume '~'
                        break;
                }
                continue;
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            // Many serial terminals send CRLF for Enter. If we broke on CR,
            // opportunistically consume a following LF so it doesn't leak into
            // the next foreground program (e.g., password prompts).
            if (c == '\r') {
                i32 next = try_get_input_char();
                if (next == '\n') {
                    // consumed
                }
            }
            print_char('\r');
            print_char('\n');
            break;
        }

        if (c == 127 || c == '\b') {
            if (pos > 0) {
                pos--;
                memmove(buf + pos, buf + pos + 1, len - pos);
                len--;
                print_char('\b');
                redraw_line_from(buf, len, pos);
            }
            continue;
        }

        if (c == 3) // Ctrl+C
        {
            print_str("^C\n");
            len = 0;
            pos = 0;
            break;
        }

        if (c == 1) // Ctrl+A
        {
            cursor_left(pos);
            pos = 0;
            continue;
        }

        if (c == 5) // Ctrl+E
        {
            cursor_right(len - pos);
            pos = len;
            continue;
        }

        if (c == 21) // Ctrl+U
        {
            cursor_left(pos);
            for (usize i = 0; i < len; i++)
                print_char(' ');
            cursor_left(len);
            len = 0;
            pos = 0;
            continue;
        }

        if (c == 11) // Ctrl+K
        {
            for (usize i = pos; i < len; i++)
                print_char(' ');
            cursor_left(len - pos);
            len = pos;
            continue;
        }

        if (c == '\t') {
            buf[len] = '\0';
            const char *first_match = nullptr;
            usize match_count = 0;
            usize prefix_len = 0;

            for (usize i = 0; i < num_commands; i++) {
                if (strstart(commands[i], buf)) {
                    if (match_count == 0) {
                        first_match = commands[i];
                        prefix_len = strlen(commands[i]);
                    } else {
                        prefix_len = common_prefix(first_match, commands[i]);
                        if (prefix_len < len)
                            prefix_len = len;
                    }
                    match_count++;
                }
            }

            if (match_count == 1) {
                replace_line(buf, &len, &pos, first_match);
            } else if (match_count > 1) {
                if (prefix_len > len) {
                    for (usize i = len; i < prefix_len; i++) {
                        buf[i] = first_match[i];
                        print_char(first_match[i]);
                    }
                    len = prefix_len;
                    pos = len;
                    buf[len] = '\0';
                } else {
                    print_str("\n");
                    for (usize i = 0; i < num_commands; i++) {
                        if (strstart(commands[i], buf)) {
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

        if (c >= 32 && c < 127) {
            if (len >= maxlen - 1)
                continue;
            memmove(buf + pos + 1, buf + pos, len - pos);
            buf[pos] = c;
            len++;
            print_char(c);
            pos++;
            if (pos < len) {
                redraw_line_from(buf, len, pos);
            }
        }
    }

    buf[len] = '\0';
    return len;
}
