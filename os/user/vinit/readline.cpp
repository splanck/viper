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

void refresh_current_dir()
{
    if (sys::getcwd(current_dir, sizeof(current_dir)) < 0)
    {
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

void history_add(const char *line)
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

const char *history_get(usize index)
{
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

static void redraw_line_from(const char *buf, usize len, usize pos)
{
    for (usize i = pos; i < len; i++)
    {
        print_char(buf[i]);
    }
    print_char(' ');
    for (usize i = len + 1; i > pos; i--)
    {
        print_char('\b');
    }
}

static void cursor_left(usize n)
{
    while (n--)
    {
        print_str("\033[D");
    }
}

static void cursor_right(usize n)
{
    while (n--)
    {
        print_str("\033[C");
    }
}

static void replace_line(char *buf, usize *len, usize *pos, const char *newline)
{
    cursor_left(*pos);
    for (usize i = 0; i < *len; i++)
        print_char(' ');
    cursor_left(*len);
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

// =============================================================================
// Tab Completion
// =============================================================================

static const char *commands[] = {
    "Assign", "Avail",  "Caps",     "chdir",  "Cls",  "Copy",    "cwd",    "Date",    "Delete",
    "Dir",    "Echo",   "EndShell", "Fetch",  "Help", "History", "Info",   "List",    "MakeDir",
    "Path",   "Rename", "Run",      "Status", "Time", "Type",    "Uptime", "Version", "Why"};
static const usize num_commands = sizeof(commands) / sizeof(commands[0]);

static usize common_prefix(const char *a, const char *b)
{
    usize i = 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return i;
}

// =============================================================================
// Readline
// =============================================================================

usize readline(char *buf, usize maxlen)
{
    usize len = 0;
    usize pos = 0;

    static char saved_line[256];
    saved_line[0] = '\0';
    history_index = history_count;

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
                    case 'A': // Up arrow
                        if (history_index > 0)
                        {
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
                    case 'B': // Down arrow
                        if (history_index < history_count)
                        {
                            history_index++;
                            if (history_index == history_count)
                            {
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
                    case '3':               // Delete key
                        c = sys::getchar(); // consume '~'
                        if (pos < len)
                        {
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

        if (c == '\r' || c == '\n')
        {
            // Many serial terminals send CRLF for Enter. If we broke on CR,
            // opportunistically consume a following LF so it doesn't leak into
            // the next foreground program (e.g., password prompts).
            if (c == '\r')
            {
                i32 next = sys::try_getchar();
                if (next == '\n')
                {
                    // consumed
                }
            }
            print_char('\r');
            print_char('\n');
            break;
        }

        if (c == 127 || c == '\b')
        {
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

        if (c == '\t')
        {
            buf[len] = '\0';
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
                        prefix_len = common_prefix(first_match, commands[i]);
                        if (prefix_len < len)
                            prefix_len = len;
                    }
                    match_count++;
                }
            }

            if (match_count == 1)
            {
                replace_line(buf, &len, &pos, first_match);
            }
            else if (match_count > 1)
            {
                if (prefix_len > len)
                {
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
        {
            if (len >= maxlen - 1)
                continue;
            memmove(buf + pos + 1, buf + pos, len - pos);
            buf[pos] = c;
            len++;
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
