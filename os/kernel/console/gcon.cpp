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
    u32 px = cx * font::WIDTH;
    u32 py = cy * font::HEIGHT;

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
    const auto &fb = ramfb::get_info();
    u32 *framebuffer = ramfb::get_framebuffer();

    u32 line_height = font::HEIGHT;
    u32 stride = fb.pitch / 4;

    // Move all lines up by one text line
    for (u32 y = 0; y < fb.height - line_height; y++)
    {
        for (u32 x = 0; x < fb.width; x++)
        {
            framebuffer[y * stride + x] = framebuffer[(y + line_height) * stride + x];
        }
    }

    // Clear the bottom line
    for (u32 y = fb.height - line_height; y < fb.height; y++)
    {
        for (u32 x = 0; x < fb.width; x++)
        {
            framebuffer[y * stride + x] = bg_color;
        }
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
} // namespace

/** @copydoc gcon::init */
bool init()
{
    if (!ramfb::get_info().address)
    {
        return false; // No framebuffer available
    }

    const auto &fb = ramfb::get_info();

    // Calculate console dimensions
    cols = fb.width / font::WIDTH;
    rows = fb.height / font::HEIGHT;

    // Set default colors
    fg_color = colors::VIPER_GREEN;
    bg_color = colors::VIPER_DARK_BROWN;

    // Clear screen
    ramfb::clear(bg_color);

    // Reset cursor
    cursor_x = 0;
    cursor_y = 0;

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
        default:
            if (c >= 32 && c < 127)
            {
                draw_char(cursor_x, cursor_y, c);
                advance_cursor();
            }
            break;
    }
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

    ramfb::clear(bg_color);
    cursor_x = 0;
    cursor_y = 0;
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
    if (x < cols)
        cursor_x = x;
    if (y < rows)
        cursor_y = y;
}

/** @copydoc gcon::get_size */
void get_size(u32 &c, u32 &r)
{
    c = cols;
    r = rows;
}

} // namespace gcon
