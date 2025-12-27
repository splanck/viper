#include "gcon.hpp"
#include "../drivers/ramfb.hpp"
#include "font.hpp"

/**
 * @file gcon.cpp
 * @brief Implementation of the framebuffer-backed graphics console.
 *
 * @details
 * This file implements a small text renderer that draws fixed-width glyphs
 * into the framebuffer exposed by `ramfb`. The renderer maintains a cursor in
 * character-cell coordinates and implements minimal terminal-like behavior
 * (newline, carriage return, tab, backspace, wrapping, and scrolling).
 *
 * The console supports basic ANSI escape sequences for cursor positioning,
 * screen clearing, and color control, enabling proper terminal applications.
 *
 * Scrolling is implemented by copying framebuffer pixel rows upward by one
 * text line and clearing the last line to the current background color. This
 * is simple and adequate for early boot output but is not optimized for high
 * throughput.
 */
namespace gcon
{

namespace
{
// Console state
bool initialized = false;
u32 cursor_x = 0;
u32 cursor_y = 0;
u32 cols = 0;
u32 rows = 0;
u32 fg_color = colors::VIPER_GREEN;
u32 bg_color = colors::VIPER_DARK_BROWN;

// Border constants
constexpr u32 BORDER_WIDTH = 20;  // 20-pixel thick border
constexpr u32 BORDER_PADDING = 8; // 8-pixel padding inside border
constexpr u32 TEXT_INSET = BORDER_WIDTH + BORDER_PADDING; // Total 28-pixel inset
constexpr u32 BORDER_COLOR = 0xFF00AA00; // VIPER_GREEN for border

// Default colors for reset
u32 default_fg = colors::VIPER_GREEN;
u32 default_bg = colors::VIPER_DARK_BROWN;

// Cursor state
bool cursor_visible = false;      // Whether cursor should be shown
bool cursor_blink_state = false;  // Current blink state (on/off)
bool cursor_drawn = false;        // Whether cursor is currently drawn on screen
u64 last_blink_time = 0;          // Last time cursor blink toggled
constexpr u64 CURSOR_BLINK_MS = 500; // Cursor blink interval in milliseconds

/**
 * @brief ANSI escape sequence parser states.
 */
enum class AnsiState
{
    NORMAL,  // Normal character output
    ESC,     // Saw ESC (0x1B)
    CSI,     // Saw ESC[ (Control Sequence Introducer)
    PARAM    // Collecting numeric parameters
};

// ANSI parser state
AnsiState ansi_state = AnsiState::NORMAL;
constexpr usize MAX_PARAMS = 8;
u32 ansi_params[MAX_PARAMS];
usize ansi_param_count = 0;
u32 ansi_current_param = 0;
bool ansi_param_started = false;
bool ansi_private_mode = false; // True if CSI sequence started with '?'

/**
 * @brief ANSI standard color palette (30-37 foreground, 40-47 background).
 *
 * @details
 * Maps ANSI color codes to 32-bit ARGB pixel values.
 * 0=black, 1=red, 2=green, 3=yellow, 4=blue, 5=magenta, 6=cyan, 7=white
 */
constexpr u32 ansi_colors[8] = {
    0xFF000000, // 0: Black
    0xFFCC3333, // 1: Red
    0xFF00AA44, // 2: Green
    0xFFCCAA00, // 3: Yellow
    0xFF3366CC, // 4: Blue
    0xFFCC33CC, // 5: Magenta
    0xFF33CCCC, // 6: Cyan
    0xFFEEEEEE  // 7: White
};

/**
 * @brief Bright ANSI color palette (90-97 foreground, 100-107 background).
 */
constexpr u32 ansi_bright_colors[8] = {
    0xFF666666, // 0: Bright Black (Gray)
    0xFFFF6666, // 1: Bright Red
    0xFF66FF66, // 2: Bright Green
    0xFFFFFF66, // 3: Bright Yellow
    0xFF6699FF, // 4: Bright Blue
    0xFFFF66FF, // 5: Bright Magenta
    0xFF66FFFF, // 6: Bright Cyan
    0xFFFFFFFF  // 7: Bright White
};

/**
 * @brief Fill a rectangle with a solid color.
 *
 * @details
 * Efficiently fills a rectangular region of the framebuffer with the
 * specified color. Used for drawing borders and clearing regions.
 *
 * @param x X coordinate of top-left corner (pixels).
 * @param y Y coordinate of top-left corner (pixels).
 * @param width Width of rectangle (pixels).
 * @param height Height of rectangle (pixels).
 * @param color Fill color (32-bit ARGB).
 */
void fill_rect(u32 x, u32 y, u32 width, u32 height, u32 color)
{
    const auto &fb = ramfb::get_info();
    u32 *framebuffer = ramfb::get_framebuffer();
    u32 stride = fb.pitch / 4;

    // Clamp to framebuffer bounds
    u32 x_end = x + width;
    u32 y_end = y + height;
    if (x_end > fb.width)
        x_end = fb.width;
    if (y_end > fb.height)
        y_end = fb.height;

    for (u32 py = y; py < y_end; py++)
    {
        for (u32 px = x; px < x_end; px++)
        {
            framebuffer[py * stride + px] = color;
        }
    }
}

/**
 * @brief Draw the green border around the console.
 *
 * @details
 * Draws a 4-pixel thick green border around the entire framebuffer
 * and fills the inner area (padding region) with the background color.
 * The text area is inset by 8 pixels (4px border + 4px padding) on all sides.
 */
void draw_border()
{
    const auto &fb = ramfb::get_info();

    // Draw top border
    fill_rect(0, 0, fb.width, BORDER_WIDTH, BORDER_COLOR);

    // Draw bottom border
    fill_rect(0, fb.height - BORDER_WIDTH, fb.width, BORDER_WIDTH, BORDER_COLOR);

    // Draw left border
    fill_rect(0, 0, BORDER_WIDTH, fb.height, BORDER_COLOR);

    // Draw right border
    fill_rect(fb.width - BORDER_WIDTH, 0, BORDER_WIDTH, fb.height, BORDER_COLOR);

    // Fill inner padding area with background color
    // This clears the area between border and text
    fill_rect(BORDER_WIDTH, BORDER_WIDTH,
              fb.width - 2 * BORDER_WIDTH, fb.height - 2 * BORDER_WIDTH,
              bg_color);
}

// Draw a single character at position (cx, cy) in character cells
/**
 * @brief Render one glyph into the framebuffer at the given cell location.
 *
 * @details
 * The glyph bitmap is retrieved from @ref font::get_glyph and then expanded
 * into pixels according to the font scaling parameters. Each "on" bit is
 * drawn with the current foreground color and each "off" bit with the
 * current background color.
 *
 * Coordinates are specified in character-cell units; the function converts
 * these into pixel coordinates using the effective font width/height.
 *
 * @param cx Column in character cells.
 * @param cy Row in character cells.
 * @param c  Printable ASCII character to render.
 */
void draw_char(u32 cx, u32 cy, char c)
{
    const u8 *glyph = font::get_glyph(c);
    // Add TEXT_INSET offset to account for border + padding
    u32 px = TEXT_INSET + cx * font::WIDTH;
    u32 py = TEXT_INSET + cy * font::HEIGHT;

    // Render with fractional scaling (SCALE_NUM / SCALE_DEN)
    for (u32 row = 0; row < font::BASE_HEIGHT; row++)
    {
        u8 bits = glyph[row];
        // Calculate Y span for this row
        u32 y0 = (row * font::SCALE_NUM) / font::SCALE_DEN;
        u32 y1 = ((row + 1) * font::SCALE_NUM) / font::SCALE_DEN;

        for (u32 col = 0; col < font::BASE_WIDTH; col++)
        {
            // Bits are stored MSB first
            u32 color = (bits & (0x80 >> col)) ? fg_color : bg_color;
            // Calculate X span for this column
            u32 x0 = (col * font::SCALE_NUM) / font::SCALE_DEN;
            u32 x1 = ((col + 1) * font::SCALE_NUM) / font::SCALE_DEN;

            // Fill the scaled rectangle
            for (u32 sy = y0; sy < y1; sy++)
            {
                for (u32 sx = x0; sx < x1; sx++)
                {
                    ramfb::put_pixel(px + sx, py + sy, color);
                }
            }
        }
    }
}

/**
 * @brief Draw or erase the cursor using XOR.
 *
 * @details
 * XORs the pixels at the cursor position with a bright value, making the
 * cursor visible on any background. Calling this function twice restores
 * the original pixels.
 */
void xor_cursor()
{
    u32 *framebuffer = ramfb::get_framebuffer();
    const auto &fb = ramfb::get_info();
    u32 stride = fb.pitch / 4;

    // Add TEXT_INSET offset to account for border + padding
    u32 px = TEXT_INSET + cursor_x * font::WIDTH;
    u32 py = TEXT_INSET + cursor_y * font::HEIGHT;

    // XOR a block at the cursor position (bottom portion for underline-style,
    // or full block - we'll use full block)
    for (u32 row = 0; row < font::HEIGHT; row++)
    {
        for (u32 col = 0; col < font::WIDTH; col++)
        {
            u32 x = px + col;
            u32 y = py + row;
            if (x < fb.width && y < fb.height)
            {
                // XOR with bright white to toggle visibility
                framebuffer[y * stride + x] ^= 0x00FFFFFF;
            }
        }
    }
}

/**
 * @brief Draw the cursor if it should be visible.
 */
void draw_cursor_if_visible()
{
    if (cursor_visible && cursor_blink_state && !cursor_drawn)
    {
        xor_cursor();
        cursor_drawn = true;
    }
}

/**
 * @brief Erase the cursor if currently drawn.
 */
void erase_cursor_if_drawn()
{
    if (cursor_drawn)
    {
        xor_cursor();
        cursor_drawn = false;
    }
}

// Scroll the screen up by one line
/**
 * @brief Scroll the visible contents up by one text row.
 *
 * @details
 * Copies the framebuffer up by `font::HEIGHT` pixel rows, effectively
 * discarding the top text line, and then clears the newly exposed bottom
 * area to the current background color.
 *
 * This is a straightforward "memmove in pixels" approach and assumes the
 * framebuffer is a linear packed 32-bit pixel buffer.
 */
void scroll()
{
    // Hide cursor during scroll operation
    bool was_drawn = cursor_drawn;
    erase_cursor_if_drawn();

    const auto &fb = ramfb::get_info();
    u32 *framebuffer = ramfb::get_framebuffer();

    u32 line_height = font::HEIGHT;
    u32 stride = fb.pitch / 4;

    // Calculate the inner text area bounds (excluding border + padding)
    u32 inner_x_start = TEXT_INSET;
    u32 inner_x_end = fb.width - TEXT_INSET;
    u32 inner_y_start = TEXT_INSET;
    u32 inner_y_end = fb.height - TEXT_INSET;

    // Move all lines up by one text line (within inner area only)
    for (u32 y = inner_y_start; y < inner_y_end - line_height; y++)
    {
        for (u32 x = inner_x_start; x < inner_x_end; x++)
        {
            framebuffer[y * stride + x] = framebuffer[(y + line_height) * stride + x];
        }
    }

    // Clear the bottom line (within inner area only)
    for (u32 y = inner_y_end - line_height; y < inner_y_end; y++)
    {
        for (u32 x = inner_x_start; x < inner_x_end; x++)
        {
            framebuffer[y * stride + x] = bg_color;
        }
    }

    // Restore cursor if it was visible
    if (was_drawn && cursor_visible && cursor_blink_state)
    {
        xor_cursor();
        cursor_drawn = true;
    }
}

// Advance cursor, handling wrap and scroll
/**
 * @brief Advance the cursor to the next cell, wrapping and scrolling.
 *
 * @details
 * Moves the cursor one column to the right. If the cursor reaches the end
 * of the line, wraps to the start of the next line. If the cursor reaches
 * the bottom of the screen, triggers a scroll and places the cursor on the
 * last line.
 */
void advance_cursor()
{
    cursor_x++;
    if (cursor_x >= cols)
    {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= rows)
        {
            scroll();
            cursor_y = rows - 1;
        }
    }
}

// Move to next line
/**
 * @brief Move the cursor to the start of the next line, scrolling if needed.
 *
 * @details
 * Resets the cursor column to zero and advances the row. If the cursor
 * moves beyond the last visible row, scrolls the framebuffer by one line
 * and keeps the cursor on the last row.
 */
void newline()
{
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= rows)
    {
        scroll();
        cursor_y = rows - 1;
    }
}

/**
 * @brief Clear from cursor to end of screen.
 */
void clear_to_end_of_screen()
{
    const auto &fb = ramfb::get_info();
    u32 *framebuffer = ramfb::get_framebuffer();
    u32 stride = fb.pitch / 4;

    // Calculate inner area bounds
    u32 inner_x_end = fb.width - TEXT_INSET;
    u32 inner_y_end = fb.height - TEXT_INSET;

    // Clear rest of current line (with TEXT_INSET offset)
    u32 px_start = TEXT_INSET + cursor_x * font::WIDTH;
    u32 py_start = TEXT_INSET + cursor_y * font::HEIGHT;
    for (u32 y = py_start; y < py_start + font::HEIGHT && y < inner_y_end; y++)
    {
        for (u32 x = px_start; x < inner_x_end; x++)
        {
            framebuffer[y * stride + x] = bg_color;
        }
    }

    // Clear all lines below (within inner area)
    u32 py_next = TEXT_INSET + (cursor_y + 1) * font::HEIGHT;
    for (u32 y = py_next; y < inner_y_end; y++)
    {
        for (u32 x = TEXT_INSET; x < inner_x_end; x++)
        {
            framebuffer[y * stride + x] = bg_color;
        }
    }
}

/**
 * @brief Clear from cursor to end of line.
 */
void clear_to_end_of_line()
{
    const auto &fb = ramfb::get_info();
    u32 *framebuffer = ramfb::get_framebuffer();
    u32 stride = fb.pitch / 4;

    // Calculate inner area bounds
    u32 inner_x_end = fb.width - TEXT_INSET;
    u32 inner_y_end = fb.height - TEXT_INSET;

    // Add TEXT_INSET offset for cursor position
    u32 px_start = TEXT_INSET + cursor_x * font::WIDTH;
    u32 py_start = TEXT_INSET + cursor_y * font::HEIGHT;

    for (u32 y = py_start; y < py_start + font::HEIGHT && y < inner_y_end; y++)
    {
        for (u32 x = px_start; x < inner_x_end; x++)
        {
            framebuffer[y * stride + x] = bg_color;
        }
    }
}

/**
 * @brief Reset ANSI parser state.
 */
void ansi_reset()
{
    ansi_state = AnsiState::NORMAL;
    ansi_param_count = 0;
    ansi_current_param = 0;
    ansi_param_started = false;
    ansi_private_mode = false;
}

/**
 * @brief Finalize current parameter and prepare for next.
 */
void ansi_finish_param()
{
    if (ansi_param_count < MAX_PARAMS)
    {
        ansi_params[ansi_param_count++] = ansi_current_param;
    }
    ansi_current_param = 0;
    ansi_param_started = false;
}

/**
 * @brief Handle SGR (Select Graphic Rendition) escape sequence.
 *
 * @details
 * Processes ESC[{n}m sequences for color control:
 * - 0: Reset to default colors
 * - 30-37: Set foreground to standard color
 * - 40-47: Set background to standard color
 * - 90-97: Set foreground to bright color
 * - 100-107: Set background to bright color
 * - 39: Default foreground
 * - 49: Default background
 */
void handle_sgr()
{
    // If no parameters, treat as reset (ESC[m same as ESC[0m)
    if (ansi_param_count == 0)
    {
        fg_color = default_fg;
        bg_color = default_bg;
        return;
    }

    for (usize i = 0; i < ansi_param_count; i++)
    {
        u32 param = ansi_params[i];

        if (param == 0)
        {
            // Reset
            fg_color = default_fg;
            bg_color = default_bg;
        }
        else if (param >= 30 && param <= 37)
        {
            // Standard foreground colors
            fg_color = ansi_colors[param - 30];
        }
        else if (param >= 40 && param <= 47)
        {
            // Standard background colors
            bg_color = ansi_colors[param - 40];
        }
        else if (param >= 90 && param <= 97)
        {
            // Bright foreground colors
            fg_color = ansi_bright_colors[param - 90];
        }
        else if (param >= 100 && param <= 107)
        {
            // Bright background colors
            bg_color = ansi_bright_colors[param - 100];
        }
        else if (param == 39)
        {
            // Default foreground
            fg_color = default_fg;
        }
        else if (param == 49)
        {
            // Default background
            bg_color = default_bg;
        }
        else if (param == 1)
        {
            // Bold - we could use bright colors, but for now just ignore
        }
        else if (param == 7)
        {
            // Reverse video
            u32 tmp = fg_color;
            fg_color = bg_color;
            bg_color = tmp;
        }
        else if (param == 27)
        {
            // Reverse off - swap back
            u32 tmp = fg_color;
            fg_color = bg_color;
            bg_color = tmp;
        }
        // Other SGR codes are ignored
    }
}

/**
 * @brief Handle CSI (Control Sequence Introducer) final character.
 *
 * @param final The final character of the sequence (e.g., 'H', 'J', 'K', 'm')
 */
void handle_csi(char final)
{
    // Get parameters (with defaults)
    u32 p1 = (ansi_param_count > 0) ? ansi_params[0] : 0;
    u32 p2 = (ansi_param_count > 1) ? ansi_params[1] : 0;

    switch (final)
    {
        case 'H': // CUP - Cursor Position
        case 'f': // HVP - Horizontal and Vertical Position
            // ESC[H = home, ESC[row;colH = position (1-based)
            {
                u32 row = (p1 > 0) ? p1 - 1 : 0;
                u32 col = (p2 > 0) ? p2 - 1 : 0;
                if (row >= rows)
                    row = rows - 1;
                if (col >= cols)
                    col = cols - 1;
                cursor_y = row;
                cursor_x = col;
            }
            break;

        case 'J': // ED - Erase in Display
            if (p1 == 0)
            {
                // Clear from cursor to end of screen
                clear_to_end_of_screen();
            }
            else if (p1 == 2 || p1 == 3)
            {
                // Clear entire screen (preserve border)
                const auto &fb_info = ramfb::get_info();
                fill_rect(TEXT_INSET, TEXT_INSET,
                          fb_info.width - 2 * TEXT_INSET, fb_info.height - 2 * TEXT_INSET,
                          bg_color);
                cursor_x = 0;
                cursor_y = 0;
            }
            break;

        case 'K': // EL - Erase in Line
            if (p1 == 0)
            {
                // Clear from cursor to end of line
                clear_to_end_of_line();
            }
            else if (p1 == 2)
            {
                // Clear entire line
                u32 saved_x = cursor_x;
                cursor_x = 0;
                clear_to_end_of_line();
                cursor_x = saved_x;
            }
            break;

        case 'm': // SGR - Select Graphic Rendition
            handle_sgr();
            break;

        case 'A': // CUU - Cursor Up
            {
                u32 n = (p1 > 0) ? p1 : 1;
                if (cursor_y >= n)
                    cursor_y -= n;
                else
                    cursor_y = 0;
            }
            break;

        case 'B': // CUD - Cursor Down
            {
                u32 n = (p1 > 0) ? p1 : 1;
                cursor_y += n;
                if (cursor_y >= rows)
                    cursor_y = rows - 1;
            }
            break;

        case 'C': // CUF - Cursor Forward
            {
                u32 n = (p1 > 0) ? p1 : 1;
                cursor_x += n;
                if (cursor_x >= cols)
                    cursor_x = cols - 1;
            }
            break;

        case 'D': // CUB - Cursor Back
            {
                u32 n = (p1 > 0) ? p1 : 1;
                if (cursor_x >= n)
                    cursor_x -= n;
                else
                    cursor_x = 0;
            }
            break;

        case 'h': // SM - Set Mode
            if (ansi_private_mode && p1 == 25)
            {
                // ESC[?25h - Show cursor (DECTCEM)
                cursor_visible = true;
                cursor_blink_state = true;
                draw_cursor_if_visible();
            }
            break;

        case 'l': // RM - Reset Mode
            if (ansi_private_mode && p1 == 25)
            {
                // ESC[?25l - Hide cursor (DECTCEM)
                erase_cursor_if_drawn();
                cursor_visible = false;
                cursor_blink_state = false;
            }
            break;

        case 's': // SCP - Save Cursor Position (not implemented, ignore)
        case 'u': // RCP - Restore Cursor Position (not implemented, ignore)
        default:
            // Unknown sequence - ignore
            break;
    }
}

/**
 * @brief Process a character through the ANSI state machine.
 *
 * @param c Character to process.
 * @return true if character was consumed by escape sequence, false if it should be printed.
 */
bool ansi_process(char c)
{
    switch (ansi_state)
    {
        case AnsiState::NORMAL:
            if (c == '\x1B')
            {
                ansi_state = AnsiState::ESC;
                return true;
            }
            return false;

        case AnsiState::ESC:
            if (c == '[')
            {
                ansi_state = AnsiState::CSI;
                ansi_param_count = 0;
                ansi_current_param = 0;
                ansi_param_started = false;
                return true;
            }
            // Not a CSI sequence - reset and process character normally
            ansi_reset();
            return false;

        case AnsiState::CSI:
        case AnsiState::PARAM:
            if (c == '?' && ansi_state == AnsiState::CSI && !ansi_param_started)
            {
                // Private mode indicator (e.g., ESC[?25h)
                ansi_private_mode = true;
                return true;
            }
            else if (c >= '0' && c <= '9')
            {
                // Digit - accumulate parameter
                ansi_state = AnsiState::PARAM;
                ansi_current_param = ansi_current_param * 10 + (c - '0');
                ansi_param_started = true;
                return true;
            }
            else if (c == ';')
            {
                // Parameter separator
                ansi_finish_param();
                ansi_state = AnsiState::PARAM;
                return true;
            }
            else if (c >= 0x40 && c <= 0x7E)
            {
                // Final character - execute command
                if (ansi_param_started || ansi_param_count > 0)
                {
                    ansi_finish_param();
                }
                handle_csi(c);
                ansi_reset();
                return true;
            }
            else
            {
                // Unknown character - abort sequence
                ansi_reset();
                return false;
            }
    }

    return false;
}

} // namespace

/** @copydoc gcon::init */
bool init()
{
    if (!ramfb::get_info().address)
    {
        return false; // No framebuffer available
    }

    const auto &fb = ramfb::get_info();

    // Calculate console dimensions accounting for border + padding (16px total reduction)
    // Text area is inset by TEXT_INSET (8px) on all sides
    cols = (fb.width - 2 * TEXT_INSET) / font::WIDTH;
    rows = (fb.height - 2 * TEXT_INSET) / font::HEIGHT;

    // Set default colors
    fg_color = colors::VIPER_GREEN;
    bg_color = colors::VIPER_DARK_BROWN;
    default_fg = colors::VIPER_GREEN;
    default_bg = colors::VIPER_DARK_BROWN;

    // Draw border and fill inner area with background color
    draw_border();

    // Reset cursor
    cursor_x = 0;
    cursor_y = 0;

    // Reset ANSI parser
    ansi_reset();

    initialized = true;
    return true;
}

/** @copydoc gcon::is_available */
bool is_available()
{
    return initialized;
}

/** @copydoc gcon::putc */
void putc(char c)
{
    if (!initialized)
        return;

    // Process through ANSI state machine first
    if (ansi_process(c))
    {
        return; // Character was consumed by escape sequence
    }

    // Erase cursor before any operation that might affect its position
    erase_cursor_if_drawn();

    switch (c)
    {
        case '\n':
            newline();
            break;
        case '\r':
            cursor_x = 0;
            break;
        case '\t':
            // Align to next 8-column boundary
            do
            {
                draw_char(cursor_x, cursor_y, ' ');
                advance_cursor();
            } while (cursor_x % 8 != 0 && cursor_x < cols);
            break;
        case '\b':
            if (cursor_x > 0)
            {
                cursor_x--;
                draw_char(cursor_x, cursor_y, ' ');
            }
            break;
        case '\x1B':
            // ESC character - handled by ansi_process, but in case we get here
            break;
        default:
            if (c >= 32 && c < 127)
            {
                draw_char(cursor_x, cursor_y, c);
                advance_cursor();
            }
            break;
    }

    // Redraw cursor at new position
    draw_cursor_if_visible();
}

/** @copydoc gcon::puts */
void puts(const char *s)
{
    if (!initialized)
        return;

    while (*s)
    {
        putc(*s++);
    }
}

/** @copydoc gcon::clear */
void clear()
{
    if (!initialized)
        return;

    // Cursor will be erased by the clear operation
    cursor_drawn = false;

    // Only clear the inner text area, preserving the border
    const auto &fb = ramfb::get_info();
    fill_rect(TEXT_INSET, TEXT_INSET,
              fb.width - 2 * TEXT_INSET, fb.height - 2 * TEXT_INSET,
              bg_color);

    cursor_x = 0;
    cursor_y = 0;

    // Redraw cursor at new position
    draw_cursor_if_visible();
}

/** @copydoc gcon::set_colors */
void set_colors(u32 fg, u32 bg)
{
    fg_color = fg;
    bg_color = bg;
}

/** @copydoc gcon::get_cursor */
void get_cursor(u32 &x, u32 &y)
{
    x = cursor_x;
    y = cursor_y;
}

/** @copydoc gcon::set_cursor */
void set_cursor(u32 x, u32 y)
{
    if (!initialized)
        return;

    // Erase cursor at old position
    erase_cursor_if_drawn();

    if (x < cols)
        cursor_x = x;
    if (y < rows)
        cursor_y = y;

    // Redraw cursor at new position
    draw_cursor_if_visible();
}

/** @copydoc gcon::get_size */
void get_size(u32 &c, u32 &r)
{
    c = cols;
    r = rows;
}

/** @copydoc gcon::show_cursor */
void show_cursor()
{
    if (!initialized)
        return;

    cursor_visible = true;
    cursor_blink_state = true; // Start in visible state
    draw_cursor_if_visible();
}

/** @copydoc gcon::hide_cursor */
void hide_cursor()
{
    if (!initialized)
        return;

    erase_cursor_if_drawn();
    cursor_visible = false;
    cursor_blink_state = false;
}

/** @copydoc gcon::is_cursor_visible */
bool is_cursor_visible()
{
    return cursor_visible;
}

/** @copydoc gcon::update_cursor_blink */
void update_cursor_blink(u64 current_time_ms)
{
    if (!initialized || !cursor_visible)
        return;

    // Check if it's time to toggle the blink state
    if (current_time_ms - last_blink_time >= CURSOR_BLINK_MS)
    {
        last_blink_time = current_time_ms;

        // Toggle blink state
        if (cursor_blink_state)
        {
            // Currently on, turn off
            erase_cursor_if_drawn();
            cursor_blink_state = false;
        }
        else
        {
            // Currently off, turn on
            cursor_blink_state = true;
            draw_cursor_if_visible();
        }
    }
}

} // namespace gcon
