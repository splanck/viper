//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/servers/consoled/main.cpp
// Purpose: Console server (consoled) - GUI-based terminal emulator.
// Key invariants: Renders text in a GUI window via displayd.
// Ownership/Lifetime: Long-running service process.
// Links: user/servers/consoled/console_protocol.hpp
//
//===----------------------------------------------------------------------===//

/**
 * @file main.cpp
 * @brief Console server (consoled) main entry point.
 *
 * @details
 * This server provides console output services to user-space processes via IPC.
 * Text is rendered in a GUI window managed by displayd. The kernel gcon is
 * disabled once the console window is created, so all framebuffer output goes
 * through displayd.
 */

#include "../../syscall.hpp"
#include "../../include/viper_colors.h"
#include "console_protocol.hpp"
#include <gui.h>

using namespace console_protocol;

// =============================================================================
// Constants
// =============================================================================

static constexpr uint32_t FONT_SCALE = 3;  // Scale in half-units: 2=1x, 3=1.5x, 4=2x
static constexpr uint32_t FONT_WIDTH = 8 * FONT_SCALE / 2;   // 12 pixels at 1.5x
static constexpr uint32_t FONT_HEIGHT = 8 * FONT_SCALE / 2;  // 12 pixels at 1.5x
static constexpr uint32_t PADDING = 8;

// Colors (from centralized viper_colors.h)
static constexpr uint32_t DEFAULT_FG = VIPER_COLOR_TEXT;
static constexpr uint32_t DEFAULT_BG = VIPER_COLOR_CONSOLE_BG;

// =============================================================================
// Debug Output (serial only)
// =============================================================================

static void debug_print(const char *msg)
{
    sys::print(msg);
}

static void debug_print_dec(uint64_t val)
{
    if (val == 0)
    {
        sys::print("0");
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    while (val > 0 && i > 0)
    {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    sys::print(&buf[i]);
}

// =============================================================================
// Console State
// =============================================================================

// Character cell in the text buffer
struct Cell
{
    char ch;
    uint32_t fg;
    uint32_t bg;
};

// GUI window
static gui_window_t *g_window = nullptr;
static uint32_t g_window_width = 0;
static uint32_t g_window_height = 0;

// Text grid dimensions
static uint32_t g_cols = 0;
static uint32_t g_rows = 0;

// Cursor position
static uint32_t g_cursor_x = 0;
static uint32_t g_cursor_y = 0;
static bool g_cursor_visible = true;

// Current colors
static uint32_t g_fg_color = DEFAULT_FG;
static uint32_t g_bg_color = DEFAULT_BG;

// Text buffer (allocated dynamically)
static Cell *g_buffer = nullptr;

// Service channel
static int32_t g_service_channel = -1;

// Multi-instance support
static bool g_is_primary = false;  // True if registered as CONSOLED service
static uint32_t g_instance_id = 0; // Instance counter for window titles

// Local input buffer for interactive mode
static constexpr size_t INPUT_BUF_SIZE = 256;
static char g_input_buf[INPUT_BUF_SIZE];
static size_t g_input_len = 0;

// =============================================================================
// Buffer Management
// =============================================================================

static inline Cell &cell_at(uint32_t x, uint32_t y)
{
    return g_buffer[y * g_cols + x];
}

static void clear_buffer()
{
    for (uint32_t i = 0; i < g_cols * g_rows; i++)
    {
        g_buffer[i].ch = ' ';
        g_buffer[i].fg = g_fg_color;
        g_buffer[i].bg = g_bg_color;
    }
}

// =============================================================================
// Rendering
// =============================================================================

static void draw_cell(uint32_t cx, uint32_t cy)
{
    Cell &c = cell_at(cx, cy);
    uint32_t px = PADDING + cx * FONT_WIDTH;
    uint32_t py = PADDING + cy * FONT_HEIGHT;
    gui_draw_char_scaled(g_window, px, py, c.ch, c.fg, c.bg, FONT_SCALE);
}

static void draw_cursor()
{
    if (!g_cursor_visible)
        return;

    uint32_t px = PADDING + g_cursor_x * FONT_WIDTH;
    uint32_t py = PADDING + g_cursor_y * FONT_HEIGHT;

    // Draw cursor as inverse block
    Cell &c = cell_at(g_cursor_x, g_cursor_y);
    gui_draw_char_scaled(g_window, px, py, c.ch, c.bg, c.fg, FONT_SCALE);
}

static void redraw_all()
{
    // Draw all cells (each cell draws its own background - no full fill needed)
    for (uint32_t y = 0; y < g_rows; y++)
    {
        for (uint32_t x = 0; x < g_cols; x++)
        {
            draw_cell(x, y);
        }
    }

    // Draw cursor
    draw_cursor();
}

static void present_cell(uint32_t cx, uint32_t cy)
{
    uint32_t px = PADDING + cx * FONT_WIDTH;
    uint32_t py = PADDING + cy * FONT_HEIGHT;
    gui_present_region(g_window, px, py, FONT_WIDTH, FONT_HEIGHT);
}

// =============================================================================
// Presentation Tracking
// =============================================================================

// Flag to track if we need to present (declared here so scroll_up can use it)
static bool g_needs_present = false;

// =============================================================================
// Scrolling
// =============================================================================

static void scroll_up()
{
    // Shift buffer up by one row
    for (uint32_t y = 0; y < g_rows - 1; y++)
    {
        for (uint32_t x = 0; x < g_cols; x++)
        {
            cell_at(x, y) = cell_at(x, y + 1);
        }
    }

    // Clear bottom row
    for (uint32_t x = 0; x < g_cols; x++)
    {
        Cell &c = cell_at(x, g_rows - 1);
        c.ch = ' ';
        c.fg = g_fg_color;
        c.bg = g_bg_color;
    }

    // Redraw entire window and mark for presentation.
    // Don't call gui_present() here - it does a synchronous busy-spin wait
    // which causes severe slowdown during rapid scrolling. Let the main loop
    // handle presentation asynchronously with frame rate limiting.
    redraw_all();
    g_needs_present = true;
}

// =============================================================================
// Text Output
// =============================================================================

// Frame rate limiting - target ~60 FPS (16ms frame time)
static constexpr uint64_t FRAME_INTERVAL_MS = 16;
static uint64_t g_last_present_time = 0;

// Message batching - process messages aggressively
// Keyboard input is now handled directly by kernel, so no need to limit
static constexpr uint32_t MAX_MESSAGES_PER_BATCH = 256;

static void newline()
{
    // Erase old cursor
    if (g_cursor_visible)
    {
        draw_cell(g_cursor_x, g_cursor_y);
    }

    g_cursor_x = 0;
    g_cursor_y++;

    if (g_cursor_y >= g_rows)
    {
        g_cursor_y = g_rows - 1;
        scroll_up();
    }

    // Draw new cursor
    if (g_cursor_visible)
    {
        draw_cursor();
    }
    g_needs_present = true;
}

static void putchar_at_cursor(char ch)
{
    // Erase old cursor first
    if (g_cursor_visible)
    {
        draw_cell(g_cursor_x, g_cursor_y);
    }

    // Update buffer and draw
    Cell &c = cell_at(g_cursor_x, g_cursor_y);
    c.ch = ch;
    c.fg = g_fg_color;
    c.bg = g_bg_color;
    draw_cell(g_cursor_x, g_cursor_y);
    g_needs_present = true;

    // Advance cursor
    g_cursor_x++;
    if (g_cursor_x >= g_cols)
    {
        g_cursor_x = 0;
        g_cursor_y++;
        if (g_cursor_y >= g_rows)
        {
            g_cursor_y = g_rows - 1;
            scroll_up();
        }
    }

    // Draw new cursor (but don't present yet)
    if (g_cursor_visible)
    {
        draw_cursor();
    }
}

// =============================================================================
// ANSI Escape Sequence Parser
// =============================================================================

// ANSI parser state
static enum {
    ANSI_NORMAL,
    ANSI_ESC,      // Saw ESC
    ANSI_CSI,      // Saw ESC[
    ANSI_CSI_PRIV, // Saw ESC[? (private sequence)
    ANSI_OSC,      // Saw ESC]
} g_ansi_state = ANSI_NORMAL;

// CSI parameter buffer
static constexpr size_t CSI_MAX_PARAMS = 8;
static uint32_t g_csi_params[CSI_MAX_PARAMS];
static size_t g_csi_param_count = 0;
static uint32_t g_csi_current_param = 0;
static bool g_csi_has_param = false;

// Standard ANSI colors (index 0-7) - from centralized viper_colors.h
static constexpr uint32_t ansi_colors[8] = {
    ANSI_COLOR_BLACK,
    ANSI_COLOR_RED,
    ANSI_COLOR_GREEN,
    ANSI_COLOR_YELLOW,
    ANSI_COLOR_BLUE,
    ANSI_COLOR_MAGENTA,
    ANSI_COLOR_CYAN,
    ANSI_COLOR_WHITE,
};

// Bright ANSI colors (index 8-15) - from centralized viper_colors.h
static constexpr uint32_t ansi_bright_colors[8] = {
    ANSI_COLOR_BRIGHT_BLACK,
    ANSI_COLOR_BRIGHT_RED,
    ANSI_COLOR_BRIGHT_GREEN,
    ANSI_COLOR_BRIGHT_YELLOW,
    ANSI_COLOR_BRIGHT_BLUE,
    ANSI_COLOR_BRIGHT_MAGENTA,
    ANSI_COLOR_BRIGHT_CYAN,
    ANSI_COLOR_BRIGHT_WHITE,
};

// Track if bold/bright mode is active for colors
static bool g_bold_mode = false;
static bool g_reverse_mode = false;

// Saved cursor position for DECSC/DECRC
static uint32_t g_saved_cursor_x = 0;
static uint32_t g_saved_cursor_y = 0;

static void csi_reset()
{
    g_csi_param_count = 0;
    g_csi_current_param = 0;
    g_csi_has_param = false;
    for (size_t i = 0; i < CSI_MAX_PARAMS; i++)
        g_csi_params[i] = 0;
}

static void csi_push_param()
{
    if (g_csi_param_count < CSI_MAX_PARAMS)
    {
        g_csi_params[g_csi_param_count++] = g_csi_has_param ? g_csi_current_param : 0;
    }
    g_csi_current_param = 0;
    g_csi_has_param = false;
}

static uint32_t csi_get_param(size_t index, uint32_t default_val)
{
    if (index < g_csi_param_count && g_csi_params[index] > 0)
        return g_csi_params[index];
    return default_val;
}

// Handle SGR (Select Graphic Rendition) - colors and attributes
static void handle_sgr()
{
    // If no parameters, treat as reset
    if (g_csi_param_count == 0)
    {
        g_fg_color = DEFAULT_FG;
        g_bg_color = DEFAULT_BG;
        g_bold_mode = false;
        g_reverse_mode = false;
        return;
    }

    for (size_t i = 0; i < g_csi_param_count; i++)
    {
        uint32_t param = g_csi_params[i];

        switch (param)
        {
            case 0:  // Reset all attributes
                g_fg_color = DEFAULT_FG;
                g_bg_color = DEFAULT_BG;
                g_bold_mode = false;
                g_reverse_mode = false;
                break;

            case 1:  // Bold / bright
                g_bold_mode = true;
                break;

            case 7:  // Reverse video
                g_reverse_mode = true;
                break;

            case 22: // Normal intensity (not bold)
                g_bold_mode = false;
                break;

            case 27: // Reverse off
                g_reverse_mode = false;
                break;

            // Foreground colors 30-37
            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
                if (g_bold_mode)
                    g_fg_color = ansi_bright_colors[param - 30];
                else
                    g_fg_color = ansi_colors[param - 30];
                break;

            case 39: // Default foreground
                g_fg_color = DEFAULT_FG;
                break;

            // Background colors 40-47
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                g_bg_color = ansi_colors[param - 40];
                break;

            case 49: // Default background
                g_bg_color = DEFAULT_BG;
                break;

            // Bright foreground colors 90-97
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                g_fg_color = ansi_bright_colors[param - 90];
                break;

            // Bright background colors 100-107
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                g_bg_color = ansi_bright_colors[param - 100];
                break;

            default:
                // Ignore unknown SGR parameters
                break;
        }
    }
}

// Clear from cursor to end of line
static void clear_to_eol()
{
    for (uint32_t x = g_cursor_x; x < g_cols; x++)
    {
        Cell &c = cell_at(x, g_cursor_y);
        c.ch = ' ';
        c.fg = g_fg_color;
        c.bg = g_bg_color;
        draw_cell(x, g_cursor_y);
    }
    g_needs_present = true;
}

// Clear from start of line to cursor
static void clear_to_bol()
{
    for (uint32_t x = 0; x <= g_cursor_x && x < g_cols; x++)
    {
        Cell &c = cell_at(x, g_cursor_y);
        c.ch = ' ';
        c.fg = g_fg_color;
        c.bg = g_bg_color;
        draw_cell(x, g_cursor_y);
    }
    g_needs_present = true;
}

// Clear entire line
static void clear_line()
{
    for (uint32_t x = 0; x < g_cols; x++)
    {
        Cell &c = cell_at(x, g_cursor_y);
        c.ch = ' ';
        c.fg = g_fg_color;
        c.bg = g_bg_color;
        draw_cell(x, g_cursor_y);
    }
    g_needs_present = true;
}

// Clear from cursor to end of screen
static void clear_to_eos()
{
    // Clear rest of current line
    clear_to_eol();

    // Clear all lines below
    for (uint32_t y = g_cursor_y + 1; y < g_rows; y++)
    {
        for (uint32_t x = 0; x < g_cols; x++)
        {
            Cell &c = cell_at(x, y);
            c.ch = ' ';
            c.fg = g_fg_color;
            c.bg = g_bg_color;
            draw_cell(x, y);
        }
    }
    g_needs_present = true;
}

// Clear from start of screen to cursor
static void clear_to_bos()
{
    // Clear all lines above
    for (uint32_t y = 0; y < g_cursor_y; y++)
    {
        for (uint32_t x = 0; x < g_cols; x++)
        {
            Cell &c = cell_at(x, y);
            c.ch = ' ';
            c.fg = g_fg_color;
            c.bg = g_bg_color;
            draw_cell(x, y);
        }
    }

    // Clear start of current line
    clear_to_bol();
    g_needs_present = true;
}

// Handle CSI sequence (final character determines action)
static void handle_csi(char final_char)
{
    // Push the last parameter if there is one
    if (g_csi_has_param || g_csi_param_count > 0)
        csi_push_param();

    uint32_t n, m;

    switch (final_char)
    {
        case 'A': // Cursor Up
            n = csi_get_param(0, 1);
            if (g_cursor_visible)
                draw_cell(g_cursor_x, g_cursor_y);
            if (g_cursor_y >= n)
                g_cursor_y -= n;
            else
                g_cursor_y = 0;
            if (g_cursor_visible)
                draw_cursor();
            g_needs_present = true;
            break;

        case 'B': // Cursor Down
            n = csi_get_param(0, 1);
            if (g_cursor_visible)
                draw_cell(g_cursor_x, g_cursor_y);
            g_cursor_y += n;
            if (g_cursor_y >= g_rows)
                g_cursor_y = g_rows - 1;
            if (g_cursor_visible)
                draw_cursor();
            g_needs_present = true;
            break;

        case 'C': // Cursor Forward
            n = csi_get_param(0, 1);
            if (g_cursor_visible)
                draw_cell(g_cursor_x, g_cursor_y);
            g_cursor_x += n;
            if (g_cursor_x >= g_cols)
                g_cursor_x = g_cols - 1;
            if (g_cursor_visible)
                draw_cursor();
            g_needs_present = true;
            break;

        case 'D': // Cursor Back
            n = csi_get_param(0, 1);
            if (g_cursor_visible)
                draw_cell(g_cursor_x, g_cursor_y);
            if (g_cursor_x >= n)
                g_cursor_x -= n;
            else
                g_cursor_x = 0;
            if (g_cursor_visible)
                draw_cursor();
            g_needs_present = true;
            break;

        case 'H': // Cursor Position (also 'f')
        case 'f':
            n = csi_get_param(0, 1);  // Row (1-based)
            m = csi_get_param(1, 1);  // Column (1-based)
            if (g_cursor_visible)
                draw_cell(g_cursor_x, g_cursor_y);
            // Convert to 0-based
            g_cursor_y = (n > 0) ? n - 1 : 0;
            g_cursor_x = (m > 0) ? m - 1 : 0;
            if (g_cursor_y >= g_rows)
                g_cursor_y = g_rows - 1;
            if (g_cursor_x >= g_cols)
                g_cursor_x = g_cols - 1;
            if (g_cursor_visible)
                draw_cursor();
            g_needs_present = true;
            break;

        case 'J': // Erase in Display
            n = csi_get_param(0, 0);
            switch (n)
            {
                case 0: clear_to_eos(); break;
                case 1: clear_to_bos(); break;
                case 2: // Clear entire screen
                case 3: // Clear entire screen and scrollback (treat same as 2)
                    clear_buffer();
                    g_cursor_x = 0;
                    g_cursor_y = 0;
                    redraw_all();
                    g_needs_present = true;
                    break;
            }
            break;

        case 'K': // Erase in Line
            n = csi_get_param(0, 0);
            switch (n)
            {
                case 0: clear_to_eol(); break;
                case 1: clear_to_bol(); break;
                case 2: clear_line(); break;
            }
            break;

        case 'm': // SGR (Select Graphic Rendition) - colors/attributes
            handle_sgr();
            break;

        case 's': // Save cursor position
            g_saved_cursor_x = g_cursor_x;
            g_saved_cursor_y = g_cursor_y;
            break;

        case 'u': // Restore cursor position
            if (g_cursor_visible)
                draw_cell(g_cursor_x, g_cursor_y);
            g_cursor_x = g_saved_cursor_x;
            g_cursor_y = g_saved_cursor_y;
            if (g_cursor_x >= g_cols)
                g_cursor_x = g_cols - 1;
            if (g_cursor_y >= g_rows)
                g_cursor_y = g_rows - 1;
            if (g_cursor_visible)
                draw_cursor();
            g_needs_present = true;
            break;

        case 'n': // Device Status Report
            // We ignore DSR requests for now
            break;

        default:
            // Unknown CSI sequence - ignore
            break;
    }
}

// Handle private CSI sequence (ESC[?...)
static void handle_csi_private(char final_char)
{
    // Push the last parameter if there is one
    if (g_csi_has_param || g_csi_param_count > 0)
        csi_push_param();

    uint32_t n = csi_get_param(0, 0);

    switch (final_char)
    {
        case 'h': // Set Mode
            if (n == 25)
            {
                // Show cursor
                g_cursor_visible = true;
                draw_cursor();
                g_needs_present = true;
            }
            break;

        case 'l': // Reset Mode
            if (n == 25)
            {
                // Hide cursor
                draw_cell(g_cursor_x, g_cursor_y);
                g_cursor_visible = false;
                g_needs_present = true;
            }
            break;

        default:
            // Unknown private sequence - ignore
            break;
    }
}

static void write_text(const char *text, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        char c = text[i];
        if (c == '\0')
            break;

        // ANSI escape sequence handling
        switch (g_ansi_state)
        {
            case ANSI_NORMAL:
                if (c == '\x1B')
                {
                    g_ansi_state = ANSI_ESC;
                    continue;
                }
                break;

            case ANSI_ESC:
                if (c == '[')
                {
                    g_ansi_state = ANSI_CSI;
                    csi_reset();
                    continue;
                }
                else if (c == ']')
                {
                    g_ansi_state = ANSI_OSC;
                    continue;
                }
                else
                {
                    // Unknown escape, return to normal
                    g_ansi_state = ANSI_NORMAL;
                    continue;
                }

            case ANSI_CSI:
                // Check for private sequence indicator
                if (c == '?')
                {
                    g_ansi_state = ANSI_CSI_PRIV;
                    continue;
                }
                // Collect parameters
                if (c >= '0' && c <= '9')
                {
                    g_csi_current_param = g_csi_current_param * 10 + (c - '0');
                    g_csi_has_param = true;
                    continue;
                }
                if (c == ';')
                {
                    csi_push_param();
                    continue;
                }
                // Final character (0x40-0x7E)
                if (c >= 0x40 && c <= 0x7E)
                {
                    handle_csi(c);
                    g_ansi_state = ANSI_NORMAL;
                    continue;
                }
                // Intermediate bytes (0x20-0x2F) - ignore for now
                if (c >= 0x20 && c <= 0x2F)
                {
                    continue;
                }
                // Unknown - abort sequence
                g_ansi_state = ANSI_NORMAL;
                continue;

            case ANSI_CSI_PRIV:
                // Collect parameters
                if (c >= '0' && c <= '9')
                {
                    g_csi_current_param = g_csi_current_param * 10 + (c - '0');
                    g_csi_has_param = true;
                    continue;
                }
                if (c == ';')
                {
                    csi_push_param();
                    continue;
                }
                // Final character
                if (c >= 0x40 && c <= 0x7E)
                {
                    handle_csi_private(c);
                    g_ansi_state = ANSI_NORMAL;
                    continue;
                }
                // Unknown - abort sequence
                g_ansi_state = ANSI_NORMAL;
                continue;

            case ANSI_OSC:
                // OSC sequence ends with BEL or ST (ESC \)
                if (c == '\x07' || c == '\\')
                {
                    g_ansi_state = ANSI_NORMAL;
                }
                continue;
        }

        if (c == '\n')
        {
            newline();
        }
        else if (c == '\r')
        {
            // Carriage return
            if (g_cursor_visible)
            {
                draw_cell(g_cursor_x, g_cursor_y);
            }
            g_cursor_x = 0;
            if (g_cursor_visible)
            {
                draw_cursor();
            }
            g_needs_present = true;
        }
        else if (c == '\t')
        {
            // Tab - advance to next 8-column boundary
            uint32_t next_tab = (g_cursor_x + 8) & ~7u;
            if (next_tab > g_cols)
                next_tab = g_cols;
            while (g_cursor_x < next_tab)
            {
                putchar_at_cursor(' ');
            }
        }
        else if (c == '\b')
        {
            // Backspace
            if (g_cursor_x > 0)
            {
                if (g_cursor_visible)
                {
                    draw_cell(g_cursor_x, g_cursor_y);
                }
                g_cursor_x--;
                // Clear the character
                Cell &cell = cell_at(g_cursor_x, g_cursor_y);
                cell.ch = ' ';
                draw_cell(g_cursor_x, g_cursor_y);
                if (g_cursor_visible)
                {
                    draw_cursor();
                }
                g_needs_present = true;
            }
        }
        else if (c >= 0x20 && c < 0x7F)
        {
            // Printable character
            putchar_at_cursor(c);
        }
    }
}

// =============================================================================
// Keycode to ASCII Conversion
// =============================================================================

// Linux evdev keycodes (only those used in keycode_to_ascii)
enum KeyCode : uint16_t
{
    KEY_ESC = 1,
    KEY_1 = 2,
    KEY_0 = 11,
    KEY_MINUS = 12,
    KEY_EQUAL = 13,
    KEY_BACKSPACE = 14,
    KEY_TAB = 15,
    KEY_Q = 16,
    KEY_P = 25,
    KEY_LEFTBRACE = 26,
    KEY_RIGHTBRACE = 27,
    KEY_ENTER = 28,
    KEY_A = 30,
    KEY_L = 38,
    KEY_SEMICOLON = 39,
    KEY_APOSTROPHE = 40,
    KEY_GRAVE = 41,
    KEY_BACKSLASH = 43,
    KEY_Z = 44,
    KEY_M = 50,
    KEY_COMMA = 51,
    KEY_DOT = 52,
    KEY_SLASH = 53,
    KEY_SPACE = 57,
};

// Convert keycode to ASCII character
static char keycode_to_ascii(uint16_t keycode, uint8_t modifiers)
{
    bool shift = (modifiers & 0x01) != 0;
    bool ctrl = (modifiers & 0x02) != 0;

    // Letters
    if (keycode >= KEY_Q && keycode <= KEY_P)
    {
        static const char row1[] = "qwertyuiop";
        char c = row1[keycode - KEY_Q];
        // Handle Ctrl+letter -> control character (Ctrl+A=1, Ctrl+Q=17, etc.)
        if (ctrl)
            return static_cast<char>((c - 'a') + 1);
        return shift ? static_cast<char>(c - 32) : c;
    }
    if (keycode >= KEY_A && keycode <= KEY_L)
    {
        static const char row2[] = "asdfghjkl";
        char c = row2[keycode - KEY_A];
        if (ctrl)
            return static_cast<char>((c - 'a') + 1);
        return shift ? static_cast<char>(c - 32) : c;
    }
    if (keycode >= KEY_Z && keycode <= KEY_M)
    {
        static const char row3[] = "zxcvbnm";
        char c = row3[keycode - KEY_Z];
        if (ctrl)
            return static_cast<char>((c - 'a') + 1);
        return shift ? static_cast<char>(c - 32) : c;
    }

    // Numbers
    if (keycode >= KEY_1 && keycode <= KEY_0)
    {
        static const char nums[] = "1234567890";
        static const char syms[] = "!@#$%^&*()";
        int idx = (keycode == KEY_0) ? 9 : (keycode - KEY_1);
        return shift ? syms[idx] : nums[idx];
    }

    // Special keys
    switch (keycode)
    {
        case KEY_SPACE: return ' ';
        case KEY_ENTER: return '\r';
        case KEY_BACKSPACE: return '\b';
        case KEY_TAB: return '\t';
        case KEY_ESC: return 27;
        case KEY_MINUS: return shift ? '_' : '-';
        case KEY_EQUAL: return shift ? '+' : '=';
        case KEY_LEFTBRACE: return shift ? '{' : '[';
        case KEY_RIGHTBRACE: return shift ? '}' : ']';
        case KEY_SEMICOLON: return shift ? ':' : ';';
        case KEY_APOSTROPHE: return shift ? '"' : '\'';
        case KEY_GRAVE: return shift ? '~' : '`';
        case KEY_BACKSLASH: return shift ? '|' : '\\';
        case KEY_COMMA: return shift ? '<' : ',';
        case KEY_DOT: return shift ? '>' : '.';
        case KEY_SLASH: return shift ? '?' : '/';
    }

    return 0;  // Unknown or special key
}

// =============================================================================
// Local Shell (for secondary instances)
// =============================================================================

static void print_prompt()
{
    const char *prompt = "> ";
    write_text(prompt, 2);
}

static int64_t spawn_program(const char *path)
{
    uint64_t pid = 0, tid = 0;
    int64_t result;

    __asm__ volatile(
        "mov x0, %[path]\n\t"
        "mov x1, xzr\n\t"       // name = NULL
        "mov x2, xzr\n\t"       // args = NULL
        "mov x8, #0x05\n\t"     // SYS_TASK_SPAWN
        "svc #0\n\t"
        "mov %[result], x0\n\t"
        "mov %[pid], x1\n\t"
        "mov %[tid], x2\n\t"
        : [result] "=r" (result), [pid] "=r" (pid), [tid] "=r" (tid)
        : [path] "r" (path)
        : "x0", "x1", "x2", "x8", "memory"
    );

    if (result == 0)
    {
        return static_cast<int64_t>(pid);
    }
    return result;  // Error code (negative)
}

static bool str_starts_with(const char *str, const char *prefix)
{
    while (*prefix)
    {
        if (*str++ != *prefix++)
            return false;
    }
    return true;
}

static bool str_equal(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a++ != *b++)
            return false;
    }
    return *a == *b;
}

static void handle_local_command(const char *cmd, size_t len)
{
    // Skip leading whitespace
    while (len > 0 && (*cmd == ' ' || *cmd == '\t'))
    {
        cmd++;
        len--;
    }

    // Skip trailing whitespace
    while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t'))
    {
        len--;
    }

    if (len == 0)
    {
        print_prompt();
        return;
    }

    // Built-in commands
    if (str_equal(cmd, "clear") || str_equal(cmd, "cls"))
    {
        clear_buffer();
        g_cursor_x = 0;
        g_cursor_y = 0;
        redraw_all();
        g_needs_present = true;
        print_prompt();
        return;
    }

    if (str_equal(cmd, "exit") || str_equal(cmd, "quit"))
    {
        sys::exit(0);
    }

    if (str_equal(cmd, "help") || str_equal(cmd, "?"))
    {
        write_text("Commands:\n", 10);
        write_text("  clear     - Clear screen\n", 27);
        write_text("  exit      - Close this console\n", 33);
        write_text("  help      - Show this help\n", 29);
        write_text("  run PATH  - Run a program\n", 28);
        write_text("  /sys/X    - Run /sys/X directly\n", 34);
        write_text("  /c/X      - Run /c/X directly\n", 32);
        print_prompt();
        return;
    }

    // Run command
    const char *path = nullptr;
    char path_buf[128];

    if (str_starts_with(cmd, "run "))
    {
        path = cmd + 4;
        while (*path == ' ')
            path++;
    }
    else if (cmd[0] == '/')
    {
        // Direct path
        path = cmd;
    }
    else
    {
        // Try as program name in /c/
        size_t i = 0;
        path_buf[i++] = '/';
        path_buf[i++] = 'c';
        path_buf[i++] = '/';
        for (size_t j = 0; j < len && i < 120; j++)
        {
            path_buf[i++] = cmd[j];
        }
        // Add .prg extension if not present
        if (i > 4 && path_buf[i - 4] != '.')
        {
            path_buf[i++] = '.';
            path_buf[i++] = 'p';
            path_buf[i++] = 'r';
            path_buf[i++] = 'g';
        }
        path_buf[i] = '\0';
        path = path_buf;
    }

    if (path && *path)
    {
        write_text("Launching: ", 11);
        write_text(path, 0);  // Write until null
        for (const char *p = path; *p; p++)
            write_text(p, 1);
        write_text("\n", 1);

        int64_t result = spawn_program(path);
        if (result < 0)
        {
            write_text("Error: Failed to spawn (", 24);
            char num[16];
            int64_t v = -result;
            int ni = 15;
            num[ni] = '\0';
            do { num[--ni] = '0' + (v % 10); v /= 10; } while (v > 0);
            write_text(&num[ni], 16 - ni);
            write_text(")\n", 2);
        }
        else
        {
            write_text("Started (pid ", 13);
            char num[16];
            int64_t v = result;
            int ni = 15;
            num[ni] = '\0';
            do { num[--ni] = '0' + (v % 10); v /= 10; } while (v > 0);
            write_text(&num[ni], 16 - ni);
            write_text(")\n", 2);
        }
    }
    else
    {
        write_text("Unknown command: ", 17);
        write_text(cmd, len);
        write_text("\n", 1);
    }

    print_prompt();
}

static void handle_keyboard_input(char c)
{
    if (c == '\r' || c == '\n')
    {
        // Enter pressed - process command
        write_text("\n", 1);
        g_input_buf[g_input_len] = '\0';
        handle_local_command(g_input_buf, g_input_len);
        g_input_len = 0;
    }
    else if (c == '\b')
    {
        // Backspace
        if (g_input_len > 0)
        {
            g_input_len--;
            write_text("\b", 1);  // Moves cursor back and clears
        }
    }
    else if (c >= 0x20 && c < 0x7F)
    {
        // Printable character
        if (g_input_len < INPUT_BUF_SIZE - 1)
        {
            g_input_buf[g_input_len++] = c;
            char str[2] = {c, '\0'};
            write_text(str, 1);
        }
    }
}

// =============================================================================
// Request Handling
// =============================================================================

static void handle_request(int32_t client_channel, const uint8_t *data, size_t len,
                           uint32_t *handles, uint32_t handle_count)
{
    if (len < 4)
        return;

    uint32_t msg_type = *reinterpret_cast<const uint32_t *>(data);

    switch (msg_type)
    {
        case CON_WRITE:
        {
            if (len < sizeof(WriteRequest))
                return;
            auto *req = reinterpret_cast<const WriteRequest *>(data);

            const char *text = reinterpret_cast<const char *>(data + sizeof(WriteRequest));
            size_t text_len = len - sizeof(WriteRequest);
            if (text_len > req->length)
                text_len = req->length;

            write_text(text, text_len);
            // g_needs_present is set by write_text() - main loop handles presentation

            // Only send reply if client provided a reply channel
            if (client_channel >= 0)
            {
                WriteReply reply;
                reply.type = CON_WRITE_REPLY;
                reply.request_id = req->request_id;
                reply.status = 0;
                reply.written = static_cast<uint32_t>(text_len);
                sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            }
            break;
        }

        case CON_CLEAR:
        {
            if (len < sizeof(ClearRequest))
                return;
            auto *req = reinterpret_cast<const ClearRequest *>(data);

            // Clear buffer and screen
            clear_buffer();
            g_cursor_x = 0;
            g_cursor_y = 0;
            redraw_all();
            g_needs_present = true;

            ClearReply reply;
            reply.type = CON_CLEAR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_SET_CURSOR:
        {
            if (len < sizeof(SetCursorRequest))
                return;
            auto *req = reinterpret_cast<const SetCursorRequest *>(data);

            // Erase old cursor
            if (g_cursor_visible)
            {
                draw_cell(g_cursor_x, g_cursor_y);
                present_cell(g_cursor_x, g_cursor_y);
            }

            if (req->x < g_cols)
                g_cursor_x = req->x;
            if (req->y < g_rows)
                g_cursor_y = req->y;

            // Draw new cursor
            if (g_cursor_visible)
            {
                draw_cursor();
                present_cell(g_cursor_x, g_cursor_y);
            }

            SetCursorReply reply;
            reply.type = CON_SET_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_GET_CURSOR:
        {
            if (len < sizeof(GetCursorRequest))
                return;
            auto *req = reinterpret_cast<const GetCursorRequest *>(data);

            GetCursorReply reply;
            reply.type = CON_GET_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.x = g_cursor_x;
            reply.y = g_cursor_y;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_SET_COLORS:
        {
            if (len < sizeof(SetColorsRequest))
                return;
            auto *req = reinterpret_cast<const SetColorsRequest *>(data);

            g_fg_color = req->foreground;
            g_bg_color = req->background;

            SetColorsReply reply;
            reply.type = CON_SET_COLORS_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_GET_SIZE:
        {
            if (len < sizeof(GetSizeRequest))
                return;
            auto *req = reinterpret_cast<const GetSizeRequest *>(data);

            GetSizeReply reply;
            reply.type = CON_GET_SIZE_REPLY;
            reply.request_id = req->request_id;
            reply.cols = g_cols;
            reply.rows = g_rows;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_SHOW_CURSOR:
        {
            if (len < sizeof(ShowCursorRequest))
                return;
            auto *req = reinterpret_cast<const ShowCursorRequest *>(data);

            g_cursor_visible = true;
            draw_cursor();
            present_cell(g_cursor_x, g_cursor_y);

            ShowCursorReply reply;
            reply.type = CON_SHOW_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_HIDE_CURSOR:
        {
            if (len < sizeof(HideCursorRequest))
                return;
            auto *req = reinterpret_cast<const HideCursorRequest *>(data);

            g_cursor_visible = false;
            draw_cell(g_cursor_x, g_cursor_y);
            present_cell(g_cursor_x, g_cursor_y);

            HideCursorReply reply;
            reply.type = CON_HIDE_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_CONNECT:
        {
            // CON_CONNECT is now legacy - input goes through kernel TTY buffer.
            // Just send back console dimensions for compatibility.
            if (len < sizeof(ConnectRequest))
                return;
            auto *req = reinterpret_cast<const ConnectRequest *>(data);

            ConnectReply reply;
            reply.type = CON_CONNECT_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.cols = g_cols;
            reply.rows = g_rows;

            // Use handles[0] as reply channel if provided, else use client_channel
            uint32_t reply_channel = (handle_count > 0 && handles[0] != 0xFFFFFFFF)
                ? handles[0] : static_cast<uint32_t>(client_channel);
            sys::channel_send(static_cast<int32_t>(reply_channel), &reply, sizeof(reply), nullptr, 0);
            break;
        }

        default:
            debug_print("[consoled] Unknown message type: ");
            debug_print_dec(msg_type);
            debug_print("\n");
            break;
    }
}

// =============================================================================
// Main Entry Point
// =============================================================================

// BSS section symbols from linker script
extern "C" char __bss_start[];
extern "C" char __bss_end[];

static void clear_bss(void)
{
    for (char *p = __bss_start; p < __bss_end; p++)
    {
        *p = 0;
    }
}

// Global marker for earliest debug - avoids any stack operations
extern "C" void _start()
{
    // Clear BSS first - critical for C++ global initializers
    clear_bss();

    // Reset console colors to defaults (white on blue)
    sys::print("\033[0m");

    debug_print("[consoled] Starting console server (GUI mode)...\n");

    // Receive bootstrap capabilities (optional - may not exist if spawned from workbench)
    // Only wait briefly to avoid blocking secondary instances
    debug_print("[consoled] Checking bootstrap channel...\n");
    {
        constexpr int32_t BOOTSTRAP_RECV = 0;
        uint8_t dummy[1];
        uint32_t handles[4];
        uint32_t handle_count = 4;

        // Quick check - only a few attempts for secondary instances
        for (uint32_t i = 0; i < 50; i++)
        {
            handle_count = 4;
            int64_t n = sys::channel_recv(BOOTSTRAP_RECV, dummy, sizeof(dummy), handles, &handle_count);
            if (n >= 0)
            {
                debug_print("[consoled] Received bootstrap caps\n");
                sys::channel_close(BOOTSTRAP_RECV);
                break;
            }
            if (n != VERR_WOULD_BLOCK)
            {
                debug_print("[consoled] No bootstrap channel (secondary instance)\n");
                break;  // Error - probably spawned from workbench, no bootstrap
            }
            sys::yield();
        }
    }

    // Wait for displayd to be available (it must start before us)
    // Use sleep instead of yield to give displayd time to initialize
    debug_print("[consoled] Waiting for displayd...\n");
    bool displayd_found = false;
    for (uint32_t attempt = 0; attempt < 100; attempt++)
    {
        uint32_t handle = 0xFFFFFFFF;
        int64_t result = sys::assign_get("DISPLAY", &handle);
        if (result == 0 && handle != 0xFFFFFFFF)
        {
            sys::channel_close(static_cast<int32_t>(handle));
            displayd_found = true;
            debug_print("[consoled] Found displayd after ");
            debug_print_dec(attempt);
            debug_print(" attempts\n");
            break;
        }
        // Sleep 10ms between attempts (total max wait: 1 second)
        sys::sleep(10);
    }

    if (!displayd_found)
    {
        debug_print("[consoled] ERROR: displayd not found after 1 second\n");
        sys::exit(1);
    }

    // Initialize GUI library
    debug_print("[consoled] Initializing GUI...\n");
    if (gui_init() != 0)
    {
        debug_print("[consoled] Failed to initialize GUI library\n");
        sys::exit(1);
    }
    debug_print("[consoled] GUI initialized\n");

    // Get display information
    gui_display_info_t display;
    if (gui_get_display_info(&display) != 0)
    {
        debug_print("[consoled] Failed to get display info\n");
        sys::exit(1);
    }

    debug_print("[consoled] Display: ");
    debug_print_dec(display.width);
    debug_print("x");
    debug_print_dec(display.height);
    debug_print("\n");

    // Calculate window size (60% of screen)
    g_window_width = (display.width * 60) / 100;
    g_window_height = (display.height * 60) / 100;

    // Calculate text grid size (accounting for padding)
    g_cols = (g_window_width - 2 * PADDING) / FONT_WIDTH;
    g_rows = (g_window_height - 2 * PADDING) / FONT_HEIGHT;

    debug_print("[consoled] Console: ");
    debug_print_dec(g_cols);
    debug_print(" cols x ");
    debug_print_dec(g_rows);
    debug_print(" rows\n");

    // Allocate text buffer
    g_buffer = new Cell[g_cols * g_rows];
    if (!g_buffer)
    {
        debug_print("[consoled] Failed to allocate text buffer\n");
        sys::exit(1);
    }

    // Initialize buffer
    clear_buffer();

    // Check if another consoled is already registered (for unique window title)
    uint32_t existing_handle = 0xFFFFFFFF;
    bool consoled_exists = (sys::assign_get("CONSOLED", &existing_handle) == 0 && existing_handle != 0xFFFFFFFF);
    if (consoled_exists)
    {
        sys::channel_close(static_cast<int32_t>(existing_handle));
        g_instance_id = sys::uptime() % 1000;  // Use timestamp for unique ID
    }

    // Create console window with unique title for secondary instances
    char window_title[32] = "Console";
    if (consoled_exists)
    {
        // Format: "Console #123"
        char *p = window_title + 7;
        *p++ = ' ';
        *p++ = '#';
        uint32_t id = g_instance_id;
        char digits[4];
        int di = 0;
        do { digits[di++] = '0' + (id % 10); id /= 10; } while (id > 0 && di < 4);
        while (di > 0) *p++ = digits[--di];
        *p = '\0';
    }

    debug_print("[consoled] Creating window: ");
    debug_print(window_title);
    debug_print("\n");

    g_window = gui_create_window(window_title, g_window_width, g_window_height);
    if (!g_window)
    {
        debug_print("[consoled] Failed to create console window\n");
        sys::exit(1);
    }
    debug_print("[consoled] Window created successfully\n");

    // Position window - offset for secondary instances to avoid overlap
    int32_t win_x = 20 + (consoled_exists ? 40 : 0);
    int32_t win_y = 20 + (consoled_exists ? 40 : 0);
    gui_set_position(g_window, win_x, win_y);

    // Disable kernel gcon framebuffer output
    // TEMPORARILY DISABLED - vinit IPC path is disabled for debugging
    // sys::gcon_set_gui_mode(true);

    // Fill entire window background once (including padding areas)
    gui_fill_rect(g_window, 0, 0, g_window_width, g_window_height, g_bg_color);

    // Initial draw
    redraw_all();
    gui_present(g_window);

    // Create service channel
    auto ch_result = sys::channel_create();
    if (ch_result.error != 0)
    {
        debug_print("[consoled] Failed to create service channel\n");
        sys::exit(1);
    }
    int32_t send_ch = static_cast<int32_t>(ch_result.val0);
    int32_t recv_ch = static_cast<int32_t>(ch_result.val1);
    g_service_channel = recv_ch;

    // Register with assign system (may fail if another instance is primary)
    debug_print("[consoled] Attempting to register as CONSOLED service...\n");
    int64_t assign_result = sys::assign_set("CONSOLED", send_ch);
    if (assign_result < 0)
    {
        debug_print("[consoled] assign_set failed with error: ");
        debug_print_dec(static_cast<uint64_t>(-assign_result));
        debug_print("\n");
        debug_print("[consoled] Running as secondary instance (interactive mode)\n");
        g_is_primary = false;
        sys::channel_close(send_ch);
        sys::channel_close(recv_ch);
        g_service_channel = -1;

        // Show welcome message for interactive console
        write_text("ViperDOS Console (Interactive)\n", 31);
        write_text("Type 'help' for commands.\n\n", 27);
        print_prompt();
    }
    else
    {
        debug_print("[consoled] Service registered as CONSOLED\n");
        g_is_primary = true;
    }
    debug_print("[consoled] Ready.\n");

    // Main event loop
    uint8_t msg_buf[MAX_PAYLOAD];
    uint32_t handles[4];
    gui_event_t event;

    // Initialize presentation timing
    g_last_present_time = sys::uptime();

    while (true)
    {

        bool did_work = false;
        uint64_t now = sys::uptime();

        // STEP 1: Process IPC messages (primary instance only)
        if (g_is_primary && g_service_channel >= 0)
        {
            uint32_t messages_processed = 0;

            while (messages_processed < MAX_MESSAGES_PER_BATCH)
            {
                uint32_t handle_count = 4;
                int64_t n = sys::channel_recv(g_service_channel, msg_buf, sizeof(msg_buf),
                                              handles, &handle_count);

                if (n > 0)
                {
                    did_work = true;
                    messages_processed++;

                    int32_t client_ch = (handle_count > 0) ? static_cast<int32_t>(handles[0]) : -1;
                    handle_request(client_ch, msg_buf, static_cast<size_t>(n), handles, handle_count);

                    // Close received handles (skip those marked as consumed with 0xFFFFFFFF)
                    for (uint32_t i = 0; i < handle_count; i++)
                    {
                        if (handles[i] != 0xFFFFFFFF)
                        {
                            sys::channel_close(static_cast<int32_t>(handles[i]));
                        }
                    }
                }
                else
                {
                    break;
                }
            }
        }

        // STEP 2: Present with frame rate limiting (async for performance)
        now = sys::uptime();
        uint64_t time_since_present = now - g_last_present_time;

        if (g_needs_present && time_since_present >= FRAME_INTERVAL_MS)
        {
            gui_present_async(g_window);  // Non-blocking present
            g_needs_present = false;
            g_last_present_time = now;
        }

        // STEP 3: Process GUI events (keyboard input)
        // For secondary instances, we handle keyboard locally
        // For primary, keyboard goes through kernel TTY but we still process other events
        while (gui_poll_event(g_window, &event) == 0)
        {
            did_work = true;

            if (event.type == GUI_EVENT_KEY && event.key.pressed)
            {
                // Convert keycode to ASCII
                char c = keycode_to_ascii(event.key.keycode, event.key.modifiers);
                if (c != 0)
                {
                    if (!g_is_primary)
                    {
                        // Secondary instance: handle input locally
                        handle_keyboard_input(c);
                    }
                    // Primary instance: kernel timer already pushes to TTY
                    // (see timer.cpp is_gui_mode check) - don't double-push
                }
            }
            else if (event.type == GUI_EVENT_CLOSE)
            {
                // Allow closing secondary instances
                if (!g_is_primary)
                {
                    debug_print("[consoled] Secondary instance exiting on close event\n");
                    sys::exit(0);
                }
            }
        }

        // STEP 4: Yield if no work was done
        // This prevents starving other processes while still being responsive
        if (!did_work)
        {
            if (g_needs_present)
            {
                // Sleep until next frame time
                uint64_t remaining = FRAME_INTERVAL_MS - time_since_present;
                if (remaining > 0 && remaining <= FRAME_INTERVAL_MS)
                {
                    sys::sleep(remaining);
                }
            }
            else
            {
                sys::yield();
            }
        }
    }

    // Unreachable
    sys::exit(0);
}
