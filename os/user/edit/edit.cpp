/**
 * @file edit.cpp
 * @brief Simple nano-like text editor for ViperOS.
 *
 * @details
 * A minimal full-screen text editor with basic editing capabilities:
 * - Arrow keys for cursor movement
 * - Home/End for line navigation
 * - Backspace/Delete for character deletion
 * - Ctrl+S to save, Ctrl+Q to quit
 * - Ctrl+G for help
 *
 * Uses libc for file I/O to route through fsd (microkernel path).
 */

#include <syscall.hpp>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

// Screen dimensions (TODO: query from terminal)
// Console is ~96x35 with 1024x768 framebuffer and 10x20 font
constexpr int SCREEN_ROWS = 23;
constexpr int SCREEN_COLS = 80;
constexpr int TEXT_ROWS = SCREEN_ROWS - 2; // Leave room for status bars

// Editor limits
constexpr int MAX_LINES = 1000;
constexpr int MAX_LINE_LEN = 512;
constexpr int MAX_FILENAME = 256;

// Text buffer
static char lines[MAX_LINES][MAX_LINE_LEN];
static int line_count = 1;

// Cursor position
static int cursor_row = 0;
static int cursor_col = 0;

// Viewport (for scrolling)
static int view_row = 0;
static int view_col = 0;

// File state
static char filename[MAX_FILENAME] = {0};
static bool modified = false;
static bool running = true;

// Message bar
static char message[128] = {0};
static bool show_help = false;

// =============================================================================
// Utility Functions
// =============================================================================

static int strlen(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    return len;
}

static void strcpy(char *dst, const char *src)
{
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

static void strncpy(char *dst, const char *src, int n)
{
    while (n-- > 0 && *src)
        *dst++ = *src++;
    *dst = '\0';
}

static void memmove(void *dst, const void *src, int n)
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
}

static void itoa(int n, char *buf)
{
    char tmp[16];
    int i = 0;
    bool neg = false;

    if (n < 0)
    {
        neg = true;
        n = -n;
    }
    if (n == 0)
    {
        tmp[i++] = '0';
    }
    else
    {
        while (n > 0)
        {
            tmp[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    if (neg)
        tmp[i++] = '-';

    int j = 0;
    while (i > 0)
        buf[j++] = tmp[--i];
    buf[j] = '\0';
}

// =============================================================================
// Terminal I/O
// =============================================================================

static void term_write(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    write(STDOUT_FILENO, s, len);
}

static void term_write_char(char c)
{
    write(STDOUT_FILENO, &c, 1);
}

static char term_getchar()
{
    // Use libc read() to route through consoled for GUI input
    char c = 0;
    read(STDIN_FILENO, &c, 1);
    return c;
}

static void term_clear()
{
    term_write("\033[2J");
}

static void term_home()
{
    term_write("\033[H");
}

static void term_goto(int row, int col)
{
    char buf[32];
    term_write("\033[");
    itoa(row + 1, buf);
    term_write(buf);
    term_write(";");
    itoa(col + 1, buf);
    term_write(buf);
    term_write("H");
}

static void term_clear_line()
{
    term_write("\033[K");
}

static void term_reverse_on()
{
    term_write("\033[7m");
}

static void term_reverse_off()
{
    term_write("\033[0m");
}

static void term_hide_cursor()
{
    term_write("\033[?25l");
}

static void term_show_cursor()
{
    term_write("\033[?25h");
}

// Terminal mode state
static struct termios orig_termios;
static bool termios_saved = false;

static void term_enable_raw_mode()
{
    if (tcgetattr(STDIN_FILENO, &orig_termios) == 0)
    {
        termios_saved = true;
        struct termios raw = orig_termios;
        cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
}

static void term_restore_mode()
{
    if (termios_saved)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    }
}

// =============================================================================
// Prompt Input
// =============================================================================

/**
 * @brief Prompt user for text input at the bottom of the screen.
 * @param prompt The prompt text to display
 * @param buf Buffer to store user input
 * @param bufsize Size of buffer
 * @return true if user entered text, false if cancelled (Escape/Ctrl+C)
 */
static bool prompt_string(const char *prompt, char *buf, int bufsize)
{
    int len = 0;
    buf[0] = '\0';

    term_show_cursor();

    while (true)
    {
        // Draw prompt line
        term_goto(TEXT_ROWS + 1, 0);
        term_clear_line();
        term_reverse_on();
        term_write(prompt);
        term_reverse_off();
        term_write(buf);

        // Get key
        char c = term_getchar();

        if (c == 27) // Escape - cancel
        {
            buf[0] = '\0';
            return false;
        }
        else if (c == 3) // Ctrl+C - cancel
        {
            buf[0] = '\0';
            return false;
        }
        else if (c == '\r' || c == '\n') // Enter - confirm
        {
            return len > 0;
        }
        else if (c == 127 || c == '\b') // Backspace
        {
            if (len > 0)
            {
                len--;
                buf[len] = '\0';
            }
        }
        else if (c >= 32 && c < 127 && len < bufsize - 1)
        {
            buf[len++] = c;
            buf[len] = '\0';
        }
    }
}

// =============================================================================
// Editor Display
// =============================================================================

static void draw_line(int screen_row, int file_row)
{
    term_goto(screen_row, 0);
    term_clear_line();

    if (file_row >= line_count)
    {
        term_write("~");
        return;
    }

    const char *line = lines[file_row];
    int len = strlen(line);
    int start = view_col;
    int end = view_col + SCREEN_COLS - 1;

    for (int i = start; i < end && i < len; i++)
    {
        char c = line[i];
        if (c == '\t')
        {
            term_write_char(' ');
        }
        else if (c >= 32 && c < 127)
        {
            term_write_char(c);
        }
        else
        {
            term_write_char('?');
        }
    }
}

static void draw_status_bar()
{
    term_goto(TEXT_ROWS, 0);
    term_reverse_on();

    // Left side: filename and modified indicator
    char status[SCREEN_COLS + 1];
    int pos = 0;

    if (filename[0])
    {
        const char *f = filename;
        while (*f && pos < 40)
            status[pos++] = *f++;
    }
    else
    {
        const char *f = "[New File]";
        while (*f && pos < 40)
            status[pos++] = *f++;
    }

    if (modified)
    {
        const char *m = " [Modified]";
        while (*m && pos < 52)
            status[pos++] = *m++;
    }

    // Right side: line/col position
    char lineinfo[32];
    char numbuf[16];
    int li = 0;
    lineinfo[li++] = ' ';
    lineinfo[li++] = 'L';
    itoa(cursor_row + 1, numbuf);
    for (int i = 0; numbuf[i]; i++)
        lineinfo[li++] = numbuf[i];
    lineinfo[li++] = '/';
    itoa(line_count, numbuf);
    for (int i = 0; numbuf[i]; i++)
        lineinfo[li++] = numbuf[i];
    lineinfo[li++] = ' ';
    lineinfo[li++] = 'C';
    itoa(cursor_col + 1, numbuf);
    for (int i = 0; numbuf[i]; i++)
        lineinfo[li++] = numbuf[i];
    lineinfo[li++] = ' ';
    lineinfo[li] = '\0';

    // Pad middle with spaces
    int right_start = SCREEN_COLS - li;
    while (pos < right_start)
        status[pos++] = ' ';

    // Append line info
    for (int i = 0; lineinfo[i] && pos < SCREEN_COLS; i++)
        status[pos++] = lineinfo[i];

    status[pos] = '\0';
    term_write(status);
    term_reverse_off();
}

static void draw_help_bar()
{
    term_goto(TEXT_ROWS + 1, 0);
    term_clear_line();

    if (show_help)
    {
        term_write("^O Open  ^S Save  ^Q Quit  ^G Help  Arrows  Home/End  Bksp/Del");
    }
    else if (message[0])
    {
        term_write(message);
        message[0] = '\0';
    }
    else
    {
        term_write("^G Help");
    }
}

static void refresh_screen()
{
    term_hide_cursor();

    // Adjust view to keep cursor visible
    if (cursor_row < view_row)
        view_row = cursor_row;
    if (cursor_row >= view_row + TEXT_ROWS)
        view_row = cursor_row - TEXT_ROWS + 1;
    if (cursor_col < view_col)
        view_col = cursor_col;
    if (cursor_col >= view_col + SCREEN_COLS - 1)
        view_col = cursor_col - SCREEN_COLS + 2;

    // Draw text lines
    for (int i = 0; i < TEXT_ROWS; i++)
    {
        draw_line(i, view_row + i);
    }

    // Draw status bars
    draw_status_bar();
    draw_help_bar();

    // Position cursor
    term_goto(cursor_row - view_row, cursor_col - view_col);
    term_show_cursor();
}

static void set_message(const char *msg)
{
    strncpy(message, msg, sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
}

// =============================================================================
// File Operations
// =============================================================================

static bool load_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        // New file
        line_count = 1;
        lines[0][0] = '\0';
        strcpy(filename, path);
        return true;
    }

    // Read file content
    char buf[4096];
    line_count = 0;
    int col = 0;

    while (true)
    {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;

        for (int i = 0; i < n; i++)
        {
            char c = buf[i];
            if (c == '\n' || c == '\r')
            {
                lines[line_count][col] = '\0';
                line_count++;
                col = 0;
                if (line_count >= MAX_LINES)
                    break;
                // Skip \r\n as single newline
                if (c == '\r' && i + 1 < n && buf[i + 1] == '\n')
                    i++;
            }
            else if (col < MAX_LINE_LEN - 1)
            {
                lines[line_count][col++] = c;
            }
        }
        if (line_count >= MAX_LINES)
            break;
    }

    // Handle last line without newline
    if (col > 0 || line_count == 0)
    {
        lines[line_count][col] = '\0';
        line_count++;
    }

    close(fd);
    strcpy(filename, path);
    modified = false;

    char msg[64] = "Loaded ";
    char numbuf[16];
    itoa(line_count, numbuf);
    int p = 7;
    for (int i = 0; numbuf[i]; i++)
        msg[p++] = numbuf[i];
    const char *suf = " lines";
    for (int i = 0; suf[i]; i++)
        msg[p++] = suf[i];
    msg[p] = '\0';
    set_message(msg);

    return true;
}

static bool save_file()
{
    // Prompt for filename if not set
    if (!filename[0])
    {
        char new_name[MAX_FILENAME];
        if (!prompt_string("Save as: ", new_name, MAX_FILENAME))
        {
            set_message("Save cancelled");
            return false;
        }
        strcpy(filename, new_name);
    }

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        set_message("Error: Cannot open file for writing");
        return false;
    }

    int total_bytes = 0;
    for (int i = 0; i < line_count; i++)
    {
        int len = strlen(lines[i]);
        if (len > 0)
        {
            write(fd, lines[i], len);
            total_bytes += len;
        }
        if (i < line_count - 1)
        {
            write(fd, "\n", 1);
            total_bytes++;
        }
    }

    // Sync to ensure data reaches disk
    fsync(fd);
    close(fd);
    modified = false;

    char msg[64] = "Saved ";
    char numbuf[16];
    itoa(total_bytes, numbuf);
    int p = 6;
    for (int i = 0; numbuf[i]; i++)
        msg[p++] = numbuf[i];
    const char *suf = " bytes";
    for (int i = 0; suf[i]; i++)
        msg[p++] = suf[i];
    msg[p] = '\0';
    set_message(msg);

    return true;
}

static void open_file()
{
    // Warn about unsaved changes
    if (modified)
    {
        set_message("Unsaved changes! Save first or press Ctrl+O again.");
        modified = false; // Allow open on second press
        return;
    }

    char new_name[MAX_FILENAME];
    if (!prompt_string("Open file: ", new_name, MAX_FILENAME))
    {
        set_message("Open cancelled");
        return;
    }

    // Reset editor state
    cursor_row = 0;
    cursor_col = 0;
    view_row = 0;
    view_col = 0;

    load_file(new_name);
}

// =============================================================================
// Editing Operations
// =============================================================================

static int current_line_len()
{
    return strlen(lines[cursor_row]);
}

static void insert_char(char c)
{
    int len = current_line_len();
    if (len >= MAX_LINE_LEN - 1)
        return;

    // Shift characters right
    memmove(
        &lines[cursor_row][cursor_col + 1], &lines[cursor_row][cursor_col], len - cursor_col + 1);
    lines[cursor_row][cursor_col] = c;
    cursor_col++;
    modified = true;
}

static void insert_newline()
{
    if (line_count >= MAX_LINES)
        return;

    // Shift lines down
    for (int i = line_count; i > cursor_row + 1; i--)
    {
        strcpy(lines[i], lines[i - 1]);
    }
    line_count++;

    // Split current line
    strcpy(lines[cursor_row + 1], &lines[cursor_row][cursor_col]);
    lines[cursor_row][cursor_col] = '\0';

    cursor_row++;
    cursor_col = 0;
    modified = true;
}

static void delete_char()
{
    int len = current_line_len();
    if (cursor_col < len)
    {
        // Delete character at cursor
        memmove(
            &lines[cursor_row][cursor_col], &lines[cursor_row][cursor_col + 1], len - cursor_col);
        modified = true;
    }
    else if (cursor_row < line_count - 1)
    {
        // Join with next line
        int next_len = strlen(lines[cursor_row + 1]);
        if (len + next_len < MAX_LINE_LEN - 1)
        {
            strcpy(&lines[cursor_row][len], lines[cursor_row + 1]);
            // Shift lines up
            for (int i = cursor_row + 1; i < line_count - 1; i++)
            {
                strcpy(lines[i], lines[i + 1]);
            }
            line_count--;
            modified = true;
        }
    }
}

static void backspace()
{
    if (cursor_col > 0)
    {
        cursor_col--;
        delete_char();
    }
    else if (cursor_row > 0)
    {
        // Join with previous line
        int prev_len = strlen(lines[cursor_row - 1]);
        cursor_col = prev_len;
        cursor_row--;
        delete_char();
    }
}

// =============================================================================
// Cursor Movement
// =============================================================================

static void move_up()
{
    if (cursor_row > 0)
    {
        cursor_row--;
        int len = current_line_len();
        if (cursor_col > len)
            cursor_col = len;
    }
}

static void move_down()
{
    if (cursor_row < line_count - 1)
    {
        cursor_row++;
        int len = current_line_len();
        if (cursor_col > len)
            cursor_col = len;
    }
}

static void move_left()
{
    if (cursor_col > 0)
    {
        cursor_col--;
    }
    else if (cursor_row > 0)
    {
        cursor_row--;
        cursor_col = current_line_len();
    }
}

static void move_right()
{
    int len = current_line_len();
    if (cursor_col < len)
    {
        cursor_col++;
    }
    else if (cursor_row < line_count - 1)
    {
        cursor_row++;
        cursor_col = 0;
    }
}

static void move_home()
{
    cursor_col = 0;
}

static void move_end()
{
    cursor_col = current_line_len();
}

static void page_up()
{
    for (int i = 0; i < TEXT_ROWS - 1 && cursor_row > 0; i++)
    {
        cursor_row--;
    }
    int len = current_line_len();
    if (cursor_col > len)
        cursor_col = len;
}

static void page_down()
{
    for (int i = 0; i < TEXT_ROWS - 1 && cursor_row < line_count - 1; i++)
    {
        cursor_row++;
    }
    int len = current_line_len();
    if (cursor_col > len)
        cursor_col = len;
}

// =============================================================================
// Input Handling
// =============================================================================

static void handle_escape_sequence()
{
    char c2 = term_getchar();
    if (c2 != '[')
        return;

    char c3 = term_getchar();
    switch (c3)
    {
        case 'A':
            move_up();
            break;
        case 'B':
            move_down();
            break;
        case 'C':
            move_right();
            break;
        case 'D':
            move_left();
            break;
        case 'H':
            move_home();
            break;
        case 'F':
            move_end();
            break;
        case '3':           // Delete key
            term_getchar(); // consume '~'
            delete_char();
            break;
        case '5':           // Page Up
            term_getchar(); // consume '~'
            page_up();
            break;
        case '6':           // Page Down
            term_getchar(); // consume '~'
            page_down();
            break;
    }
}

static void process_key()
{
    char c = term_getchar();

    if (c == '\033')
    {
        handle_escape_sequence();
        return;
    }

    switch (c)
    {
        case 7: // Ctrl+G - Help toggle
            show_help = !show_help;
            break;

        case 15: // Ctrl+O - Open file
            open_file();
            break;

        case 17: // Ctrl+Q - Quit
            if (modified)
            {
                set_message("Unsaved changes! Press Ctrl+Q again to quit without saving.");
                modified = false; // Allow quit on second press
            }
            else
            {
                running = false;
            }
            break;

        case 19: // Ctrl+S - Save
            save_file();
            break;

        case '\r':
        case '\n':
            insert_newline();
            break;

        case 127: // Backspace (DEL)
        case '\b':
            backspace();
            break;

        case '\t':
            // Insert spaces for tab
            for (int i = 0; i < 4; i++)
                insert_char(' ');
            break;

        default:
            if (c >= 32 && c < 127)
            {
                insert_char(c);
            }
            break;
    }
}

// =============================================================================
// Main
// =============================================================================

extern "C" int main(int argc, char **argv)
{
    // Enable raw terminal mode for character-by-character input
    term_enable_raw_mode();

    // Initialize empty buffer
    line_count = 1;
    lines[0][0] = '\0';

    // Load file if specified
    if (argc > 1)
    {
        load_file(argv[1]);
    }
    else
    {
        set_message("New file. ^O Open  ^S Save  ^Q Quit  ^G Help");
    }

    // Clear screen and enter editor mode
    term_clear();
    term_home();

    // Main loop
    while (running)
    {
        refresh_screen();
        process_key();
    }

    // Clean up
    term_restore_mode();
    term_clear();
    term_home();
    term_write("Goodbye!\n");

    return 0;
}
