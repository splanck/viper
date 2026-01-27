//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "gcon.hpp"
#include "../console/serial.hpp"
#include "../drivers/ramfb.hpp"
#include "../include/constants.hpp"
#include "font.hpp"

namespace kc = kernel::constants;

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
 *
 * ## Rendering Architecture
 *
 * @verbatim
 * Character Input (putc)
 *       |
 *       v
 * ANSI Parser -----> State Machine (NORMAL/ESC/CSI/PARAM)
 *       |                   |
 *       |                   v
 *       |           Process escape sequence (cursor move, clear, color)
 *       v
 * buffer_put_char() -----> Scrollback Buffer (circular)
 *       |
 *       v
 * draw_char() -----> Font Lookup (get_glyph)
 *       |                   |
 *       |                   v
 *       |           8x16 bitmap scaled to display resolution
 *       v
 * ramfb::put_pixel() -----> Direct Framebuffer Write
 * @endverbatim
 *
 * ## Design Notes
 *
 * - **No double buffering**: Writes go directly to the visible framebuffer.
 *   This simplifies the implementation but may cause visible tearing on
 *   fast output. Acceptable for boot console / debug output.
 *
 * - **No dirty rectangle tracking**: Every character write updates all pixels
 *   in that cell. A dirty-rect system would track changed regions and batch
 *   updates, but adds complexity not needed for a boot console.
 *
 * - **Font scaling**: The 8x16 base font is scaled using integer ratios
 *   (SCALE_NUM/SCALE_DEN) to accommodate higher display resolutions while
 *   maintaining readable text size.
 *
 * - **Scrollback buffer**: A circular buffer of Cell structures stores
 *   historical output for scroll-back viewing. The buffer is separate from
 *   the framebuffer and redrawn on demand.
 *
 * - **GUI mode**: When displayd takes over the framebuffer (gui_mode_active),
 *   output is redirected to serial only. The console can be restored when
 *   GUI mode ends.
 */
namespace gcon {

namespace {
// Console state
bool initialized = false;
bool gui_mode_active = false; // When true, only output to serial (displayd owns framebuffer)
u32 cursor_x = 0;
u32 cursor_y = 0;
u32 cols = 0;
u32 rows = 0;
u32 fg_color = colors::WHITE;
u32 bg_color = colors::VIPER_BLUE;

// Border constants (use centralized values)
constexpr u32 BORDER_WIDTH = kc::display::BORDER_WIDTH;
constexpr u32 TEXT_INSET = kc::display::TEXT_INSET;
constexpr u32 BORDER_COLOR = 0xFF003366; // Darker blue border

// Default colors for reset
u32 default_fg = colors::WHITE;
u32 default_bg = colors::VIPER_BLUE;

// Cursor state
bool cursor_visible = false;     // Whether cursor should be shown
bool cursor_blink_state = false; // Current blink state (on/off)
bool cursor_drawn = false;       // Whether cursor is currently drawn on screen
u64 last_blink_time = 0;         // Last time cursor blink toggled
constexpr u64 CURSOR_BLINK_MS = kc::display::CURSOR_BLINK_MS; // Cursor blink interval

// =============================================================================
// Scrollback Buffer
// =============================================================================

/// A single character cell with colors
struct Cell {
    char ch;
    u32 fg;
    u32 bg;
};

/// Scrollback buffer constants
constexpr u32 SCROLLBACK_LINES = kc::display::SCROLLBACK_LINES;
constexpr u32 SCROLLBACK_COLS = kc::display::SCROLLBACK_COLS;

/// Circular buffer of lines (each line is an array of cells)
Cell scrollback[SCROLLBACK_LINES][SCROLLBACK_COLS];

/// Current write position in circular buffer (next line to write)
u32 buffer_head = 0;

/// Total lines written to buffer (capped at SCROLLBACK_LINES)
u32 buffer_count = 0;

/// Current scroll offset (0 = live view, >0 = scrolled back N lines)
u32 scroll_offset = 0;

/// Initialize a buffer line to empty (spaces with current colors)
void clear_buffer_line(u32 line_idx) {
    for (u32 i = 0; i < SCROLLBACK_COLS; i++) {
        scrollback[line_idx][i].ch = ' ';
        scrollback[line_idx][i].fg = default_fg;
        scrollback[line_idx][i].bg = default_bg;
    }
}

/// Store a character in the buffer at the current cursor position
void buffer_put_char(char c, u32 col, u32 row) {
    // Calculate which buffer line corresponds to this screen row
    // We store the current visible screen content plus scrollback
    // The "current" line is at (buffer_head - rows + row) mod SCROLLBACK_LINES
    if (rows == 0 || col >= SCROLLBACK_COLS)
        return;

    u32 base =
        (buffer_head >= rows) ? (buffer_head - rows) : (SCROLLBACK_LINES + buffer_head - rows);
    u32 line_idx = (base + row) % SCROLLBACK_LINES;

    scrollback[line_idx][col].ch = c;
    scrollback[line_idx][col].fg = fg_color;
    scrollback[line_idx][col].bg = bg_color;
}

/// Advance buffer head when a new line is needed (scrolling or newline at bottom)
void buffer_new_line() {
    // Clear the new line
    clear_buffer_line(buffer_head);

    // Advance head
    buffer_head = (buffer_head + 1) % SCROLLBACK_LINES;

    // Track total lines
    if (buffer_count < SCROLLBACK_LINES)
        buffer_count++;
}

/**
 * @brief ANSI escape sequence parser states.
 */
enum class AnsiState {
    NORMAL, // Normal character output
    ESC,    // Saw ESC (0x1B)
    CSI,    // Saw ESC[ (Control Sequence Introducer)
    PARAM   // Collecting numeric parameters
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
    kc::color::BLACK,   // 0: Black
    kc::color::RED,     // 1: Red
    kc::color::GREEN,   // 2: Green
    kc::color::YELLOW,  // 3: Yellow
    kc::color::BLUE,    // 4: Blue
    kc::color::MAGENTA, // 5: Magenta
    kc::color::CYAN,    // 6: Cyan
    kc::color::WHITE    // 7: White
};

/**
 * @brief Bright ANSI color palette (90-97 foreground, 100-107 background).
 */
constexpr u32 ansi_bright_colors[8] = {
    kc::color::GRAY,           // 0: Bright Black (Gray)
    kc::color::BRIGHT_RED,     // 1: Bright Red
    kc::color::BRIGHT_GREEN,   // 2: Bright Green
    kc::color::BRIGHT_YELLOW,  // 3: Bright Yellow
    kc::color::BRIGHT_BLUE,    // 4: Bright Blue
    kc::color::BRIGHT_MAGENTA, // 5: Bright Magenta
    kc::color::BRIGHT_CYAN,    // 6: Bright Cyan
    kc::color::BRIGHT_WHITE    // 7: Bright White
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
void fill_rect(u32 x, u32 y, u32 width, u32 height, u32 color) {
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

    for (u32 py = y; py < y_end; py++) {
        for (u32 px = x; px < x_end; px++) {
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
void draw_border() {
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
    fill_rect(BORDER_WIDTH,
              BORDER_WIDTH,
              fb.width - 2 * BORDER_WIDTH,
              fb.height - 2 * BORDER_WIDTH,
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
void draw_char(u32 cx, u32 cy, char c) {
    const u8 *glyph = font::get_glyph(c);
    // Add TEXT_INSET offset to account for border + padding
    u32 px = TEXT_INSET + cx * font::WIDTH;
    u32 py = TEXT_INSET + cy * font::HEIGHT;

    // Render with fractional scaling (SCALE_NUM / SCALE_DEN)
    for (u32 row = 0; row < font::BASE_HEIGHT; row++) {
        u8 bits = glyph[row];
        // Calculate Y span for this row
        u32 y0 = (row * font::SCALE_NUM) / font::SCALE_DEN;
        u32 y1 = ((row + 1) * font::SCALE_NUM) / font::SCALE_DEN;

        for (u32 col = 0; col < font::BASE_WIDTH; col++) {
            // Bits are stored MSB first
            u32 color = (bits & (0x80 >> col)) ? fg_color : bg_color;
            // Calculate X span for this column
            u32 x0 = (col * font::SCALE_NUM) / font::SCALE_DEN;
            u32 x1 = ((col + 1) * font::SCALE_NUM) / font::SCALE_DEN;

            // Fill the scaled rectangle
            for (u32 sy = y0; sy < y1; sy++) {
                for (u32 sx = x0; sx < x1; sx++) {
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
void xor_cursor() {
    u32 *framebuffer = ramfb::get_framebuffer();
    const auto &fb = ramfb::get_info();
    u32 stride = fb.pitch / 4;

    // Add TEXT_INSET offset to account for border + padding
    u32 px = TEXT_INSET + cursor_x * font::WIDTH;
    u32 py = TEXT_INSET + cursor_y * font::HEIGHT;

    // XOR a block at the cursor position (bottom portion for underline-style,
    // or full block - we'll use full block)
    for (u32 row = 0; row < font::HEIGHT; row++) {
        for (u32 col = 0; col < font::WIDTH; col++) {
            u32 x = px + col;
            u32 y = py + row;
            if (x < fb.width && y < fb.height) {
                // XOR with bright white to toggle visibility
                framebuffer[y * stride + x] ^= 0x00FFFFFF;
            }
        }
    }
}

/**
 * @brief Draw the cursor if it should be visible.
 */
void draw_cursor_if_visible() {
    if (cursor_visible && cursor_blink_state && !cursor_drawn) {
        xor_cursor();
        cursor_drawn = true;
    }
}

/**
 * @brief Erase the cursor if currently drawn.
 */
void erase_cursor_if_drawn() {
    if (cursor_drawn) {
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
void scroll() {
    // Hide cursor during scroll operation
    bool was_drawn = cursor_drawn;
    erase_cursor_if_drawn();

    // Add new line to scrollback buffer
    buffer_new_line();

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
    for (u32 y = inner_y_start; y < inner_y_end - line_height; y++) {
        for (u32 x = inner_x_start; x < inner_x_end; x++) {
            framebuffer[y * stride + x] = framebuffer[(y + line_height) * stride + x];
        }
    }

    // Clear the bottom line (within inner area only)
    for (u32 y = inner_y_end - line_height; y < inner_y_end; y++) {
        for (u32 x = inner_x_start; x < inner_x_end; x++) {
            framebuffer[y * stride + x] = bg_color;
        }
    }

    // Restore cursor if it was visible
    if (was_drawn && cursor_visible && cursor_blink_state) {
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
void advance_cursor() {
    cursor_x++;
    if (cursor_x >= cols) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= rows) {
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
void newline() {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= rows) {
        scroll();
        cursor_y = rows - 1;
    }
}

/**
 * @brief Clear from cursor to end of screen.
 */
void clear_to_end_of_screen() {
    const auto &fb = ramfb::get_info();
    u32 *framebuffer = ramfb::get_framebuffer();
    u32 stride = fb.pitch / 4;

    // Calculate inner area bounds
    u32 inner_x_end = fb.width - TEXT_INSET;
    u32 inner_y_end = fb.height - TEXT_INSET;

    // Clear rest of current line (with TEXT_INSET offset)
    u32 px_start = TEXT_INSET + cursor_x * font::WIDTH;
    u32 py_start = TEXT_INSET + cursor_y * font::HEIGHT;
    for (u32 y = py_start; y < py_start + font::HEIGHT && y < inner_y_end; y++) {
        for (u32 x = px_start; x < inner_x_end; x++) {
            framebuffer[y * stride + x] = bg_color;
        }
    }

    // Clear all lines below (within inner area)
    u32 py_next = TEXT_INSET + (cursor_y + 1) * font::HEIGHT;
    for (u32 y = py_next; y < inner_y_end; y++) {
        for (u32 x = TEXT_INSET; x < inner_x_end; x++) {
            framebuffer[y * stride + x] = bg_color;
        }
    }
}

/**
 * @brief Clear from cursor to end of line.
 */
void clear_to_end_of_line() {
    const auto &fb = ramfb::get_info();
    u32 *framebuffer = ramfb::get_framebuffer();
    u32 stride = fb.pitch / 4;

    // Calculate inner area bounds
    u32 inner_x_end = fb.width - TEXT_INSET;
    u32 inner_y_end = fb.height - TEXT_INSET;

    // Add TEXT_INSET offset for cursor position
    u32 px_start = TEXT_INSET + cursor_x * font::WIDTH;
    u32 py_start = TEXT_INSET + cursor_y * font::HEIGHT;

    for (u32 y = py_start; y < py_start + font::HEIGHT && y < inner_y_end; y++) {
        for (u32 x = px_start; x < inner_x_end; x++) {
            framebuffer[y * stride + x] = bg_color;
        }
    }
}

/**
 * @brief Reset ANSI parser state.
 */
void ansi_reset() {
    ansi_state = AnsiState::NORMAL;
    ansi_param_count = 0;
    ansi_current_param = 0;
    ansi_param_started = false;
    ansi_private_mode = false;
}

/**
 * @brief Finalize current parameter and prepare for next.
 */
void ansi_finish_param() {
    if (ansi_param_count < MAX_PARAMS) {
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
void handle_sgr() {
    // If no parameters, treat as reset (ESC[m same as ESC[0m)
    if (ansi_param_count == 0) {
        fg_color = default_fg;
        bg_color = default_bg;
        return;
    }

    for (usize i = 0; i < ansi_param_count; i++) {
        u32 param = ansi_params[i];

        if (param == 0) {
            // Reset
            fg_color = default_fg;
            bg_color = default_bg;
        } else if (param >= 30 && param <= 37) {
            // Standard foreground colors
            fg_color = ansi_colors[param - 30];
        } else if (param >= 40 && param <= 47) {
            // Standard background colors
            bg_color = ansi_colors[param - 40];
        } else if (param >= 90 && param <= 97) {
            // Bright foreground colors
            fg_color = ansi_bright_colors[param - 90];
        } else if (param >= 100 && param <= 107) {
            // Bright background colors
            bg_color = ansi_bright_colors[param - 100];
        } else if (param == 39) {
            // Default foreground
            fg_color = default_fg;
        } else if (param == 49) {
            // Default background
            bg_color = default_bg;
        } else if (param == 1) {
            // Bold - we could use bright colors, but for now just ignore
        } else if (param == 7) {
            // Reverse video
            u32 tmp = fg_color;
            fg_color = bg_color;
            bg_color = tmp;
        } else if (param == 27) {
            // Reverse off - swap back
            u32 tmp = fg_color;
            fg_color = bg_color;
            bg_color = tmp;
        }
        // Other SGR codes are ignored
    }
}

// =============================================================================
// CSI Command Handlers
// =============================================================================

static void csi_cursor_position(u32 p1, u32 p2) {
    u32 row = (p1 > 0) ? p1 - 1 : 0;
    u32 col = (p2 > 0) ? p2 - 1 : 0;
    if (row >= rows)
        row = rows - 1;
    if (col >= cols)
        col = cols - 1;
    cursor_y = row;
    cursor_x = col;
}

static void csi_erase_display(u32 mode) {
    if (mode == 0) {
        clear_to_end_of_screen();
    } else if (mode == 2 || mode == 3) {
        const auto &fb_info = ramfb::get_info();
        fill_rect(TEXT_INSET,
                  TEXT_INSET,
                  fb_info.width - 2 * TEXT_INSET,
                  fb_info.height - 2 * TEXT_INSET,
                  bg_color);
        cursor_x = 0;
        cursor_y = 0;
    }
}

static void csi_erase_line(u32 mode) {
    if (mode == 0) {
        clear_to_end_of_line();
    } else if (mode == 2) {
        u32 saved_x = cursor_x;
        cursor_x = 0;
        clear_to_end_of_line();
        cursor_x = saved_x;
    }
}

static void csi_cursor_up(u32 p1, u32 p2) {
    if (p2 == 2) {
        scroll_up();
    } else {
        u32 n = (p1 > 0) ? p1 : 1;
        cursor_y = (cursor_y >= n) ? cursor_y - n : 0;
    }
}

static void csi_cursor_down(u32 p1, u32 p2) {
    if (p2 == 2) {
        scroll_down();
    } else {
        u32 n = (p1 > 0) ? p1 : 1;
        cursor_y += n;
        if (cursor_y >= rows)
            cursor_y = rows - 1;
    }
}

static void csi_cursor_forward(u32 p1) {
    u32 n = (p1 > 0) ? p1 : 1;
    cursor_x += n;
    if (cursor_x >= cols)
        cursor_x = cols - 1;
}

static void csi_cursor_back(u32 p1) {
    u32 n = (p1 > 0) ? p1 : 1;
    cursor_x = (cursor_x >= n) ? cursor_x - n : 0;
}

static void csi_set_mode(u32 p1) {
    if (ansi_private_mode && p1 == 25) {
        cursor_visible = true;
        cursor_blink_state = true;
        draw_cursor_if_visible();
    }
}

static void csi_reset_mode(u32 p1) {
    if (ansi_private_mode && p1 == 25) {
        erase_cursor_if_drawn();
        cursor_visible = false;
        cursor_blink_state = false;
    }
}

static void csi_function_key(u32 p1) {
    if (p1 == 23)
        scroll_up();
    else if (p1 == 24)
        scroll_down();
}

// =============================================================================
// CSI Dispatcher
// =============================================================================

/**
 * @brief Handle CSI (Control Sequence Introducer) final character.
 *
 * @param final The final character of the sequence (e.g., 'H', 'J', 'K', 'm')
 */
void handle_csi(char final) {
    u32 p1 = (ansi_param_count > 0) ? ansi_params[0] : 0;
    u32 p2 = (ansi_param_count > 1) ? ansi_params[1] : 0;

    erase_cursor_if_drawn();

    switch (final) {
        case 'H':
        case 'f':
            csi_cursor_position(p1, p2);
            break;
        case 'J':
            csi_erase_display(p1);
            break;
        case 'K':
            csi_erase_line(p1);
            break;
        case 'm':
            handle_sgr();
            break;
        case 'A':
            csi_cursor_up(p1, p2);
            break;
        case 'B':
            csi_cursor_down(p1, p2);
            break;
        case 'C':
            csi_cursor_forward(p1);
            break;
        case 'D':
            csi_cursor_back(p1);
            break;
        case 'h':
            csi_set_mode(p1);
            break;
        case 'l':
            csi_reset_mode(p1);
            break;
        case '~':
            csi_function_key(p1);
            break;
        default:
            break;
    }

    draw_cursor_if_visible();
}

/**
 * @brief Process a character through the ANSI state machine.
 *
 * @param c Character to process.
 * @return true if character was consumed by escape sequence, false if it should be printed.
 */
bool ansi_process(char c) {
    switch (ansi_state) {
        case AnsiState::NORMAL:
            if (c == '\x1B') {
                ansi_state = AnsiState::ESC;
                return true;
            }
            return false;

        case AnsiState::ESC:
            if (c == '[') {
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
            if (c == '?' && ansi_state == AnsiState::CSI && !ansi_param_started) {
                // Private mode indicator (e.g., ESC[?25h)
                ansi_private_mode = true;
                return true;
            } else if (c >= '0' && c <= '9') {
                // Digit - accumulate parameter
                ansi_state = AnsiState::PARAM;
                ansi_current_param = ansi_current_param * 10 + (c - '0');
                ansi_param_started = true;
                return true;
            } else if (c == ';') {
                // Parameter separator
                ansi_finish_param();
                ansi_state = AnsiState::PARAM;
                return true;
            } else if (c >= 0x40 && c <= 0x7E) {
                // Final character - execute command
                if (ansi_param_started || ansi_param_count > 0) {
                    ansi_finish_param();
                }
                handle_csi(c);
                ansi_reset();
                return true;
            } else {
                // Unknown character - abort sequence
                ansi_reset();
                return false;
            }
    }

    return false;
}

} // namespace

/** @copydoc gcon::init */
bool init() {
    if (!ramfb::get_info().address) {
        return false; // No framebuffer available
    }

    const auto &fb = ramfb::get_info();

    // Calculate console dimensions accounting for border + padding (16px total reduction)
    // Text area is inset by TEXT_INSET (8px) on all sides
    cols = (fb.width - 2 * TEXT_INSET) / font::WIDTH;
    rows = (fb.height - 2 * TEXT_INSET) / font::HEIGHT;

    serial::puts("[gcon] Font: ");
    serial::put_dec(font::WIDTH);
    serial::puts("x");
    serial::put_dec(font::HEIGHT);
    serial::puts(", console: ");
    serial::put_dec(cols);
    serial::puts("x");
    serial::put_dec(rows);
    serial::puts("\n");

    // Set default colors (white on blue)
    fg_color = colors::WHITE;
    bg_color = colors::VIPER_BLUE;
    default_fg = colors::WHITE;
    default_bg = colors::VIPER_BLUE;

    // Draw border and fill inner area with background color
    draw_border();

    // Reset cursor
    cursor_x = 0;
    cursor_y = 0;

    // Reset ANSI parser
    ansi_reset();

    // Initialize scrollback buffer
    buffer_head = 0;
    buffer_count = 0;
    scroll_offset = 0;
    for (u32 i = 0; i < SCROLLBACK_LINES; i++) {
        clear_buffer_line(i);
    }
    // Pre-fill buffer with enough empty lines for visible rows
    for (u32 i = 0; i < rows && i < SCROLLBACK_LINES; i++) {
        buffer_new_line();
    }

    initialized = true;
    return true;
}

/** @copydoc gcon::is_available */
bool is_available() {
    return initialized;
}

/** @copydoc gcon::putc */
void putc(char c) {
    if (!initialized)
        return;

    // When GUI mode is active, skip gcon output entirely (displayd owns the framebuffer).
    // Serial output is handled by callers (sys_write, sys_debug_print).
    if (gui_mode_active) {
        return;
    }

    // Process through ANSI state machine first
    if (ansi_process(c)) {
        return; // Character was consumed by escape sequence
    }

    // Auto-scroll to bottom when new output arrives
    scroll_offset = 0;

    // Erase cursor before any operation that might affect its position
    erase_cursor_if_drawn();

    switch (c) {
        case '\n':
            newline();
            break;
        case '\r':
            cursor_x = 0;
            break;
        case '\t':
            // Align to next 8-column boundary
            do {
                buffer_put_char(' ', cursor_x, cursor_y);
                draw_char(cursor_x, cursor_y, ' ');
                advance_cursor();
            } while (cursor_x % 8 != 0 && cursor_x < cols);
            break;
        case '\b':
            // Backspace only moves cursor left, does NOT erase (VT100 behavior)
            // Applications that want destructive backspace use "\b \b"
            if (cursor_x > 0) {
                cursor_x--;
            }
            break;
        case '\x1B':
            // ESC character - handled by ansi_process, but in case we get here
            break;
        default:
            if (c >= 32 && c < 127) {
                buffer_put_char(c, cursor_x, cursor_y);
                draw_char(cursor_x, cursor_y, c);
                advance_cursor();
            }
            break;
    }

    // Redraw cursor at new position
    draw_cursor_if_visible();
}

/** @copydoc gcon::putc_force */
void putc_force(char c) {
    if (!initialized)
        return;

    // Note: Unlike putc(), this function ignores gui_mode_active
    // to allow kernel TTY output to render directly to framebuffer

    // Process through ANSI state machine first
    if (ansi_process(c)) {
        return; // Character was consumed by escape sequence
    }

    // Auto-scroll to bottom when new output arrives
    scroll_offset = 0;

    // Erase cursor before any operation that might affect its position
    erase_cursor_if_drawn();

    switch (c) {
        case '\n':
            newline();
            break;
        case '\r':
            cursor_x = 0;
            break;
        case '\t':
            // Align to next 8-column boundary
            do {
                buffer_put_char(' ', cursor_x, cursor_y);
                draw_char(cursor_x, cursor_y, ' ');
                advance_cursor();
            } while (cursor_x % 8 != 0 && cursor_x < cols);
            break;
        case '\b':
            // Backspace only moves cursor left, does NOT erase (VT100 behavior)
            // Applications that want destructive backspace use "\b \b"
            if (cursor_x > 0) {
                cursor_x--;
            }
            break;
        case '\x1B':
            // ESC character - handled by ansi_process, but in case we get here
            break;
        default:
            if (c >= 32 && c < 127) {
                buffer_put_char(c, cursor_x, cursor_y);
                draw_char(cursor_x, cursor_y, c);
                advance_cursor();
            }
            break;
    }

    // Redraw cursor at new position
    draw_cursor_if_visible();
}

/** @copydoc gcon::puts */
void puts(const char *s) {
    if (!initialized)
        return;

    // When GUI mode is active, serial output is already handled by callers
    // (e.g., sys_debug_print calls serial::puts then gcon::puts).
    // Skip to avoid double output.
    if (gui_mode_active)
        return;

    while (*s) {
        putc(*s++);
    }
}

/** @copydoc gcon::clear */
void clear() {
    if (!initialized)
        return;

    // Cursor will be erased by the clear operation
    cursor_drawn = false;

    // Only clear the inner text area, preserving the border
    const auto &fb = ramfb::get_info();
    fill_rect(
        TEXT_INSET, TEXT_INSET, fb.width - 2 * TEXT_INSET, fb.height - 2 * TEXT_INSET, bg_color);

    cursor_x = 0;
    cursor_y = 0;

    // Redraw cursor at new position
    draw_cursor_if_visible();
}

/** @copydoc gcon::set_colors */
void set_colors(u32 fg, u32 bg) {
    fg_color = fg;
    bg_color = bg;
    // Also update defaults so ANSI reset (\033[0m) restores to these colors
    default_fg = fg;
    default_bg = bg;
}

/** @copydoc gcon::get_cursor */
void get_cursor(u32 &x, u32 &y) {
    x = cursor_x;
    y = cursor_y;
}

/** @copydoc gcon::set_cursor */
void set_cursor(u32 x, u32 y) {
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
void get_size(u32 &c, u32 &r) {
    c = cols;
    r = rows;
}

/** @copydoc gcon::show_cursor */
void show_cursor() {
    if (!initialized)
        return;

    cursor_visible = true;
    cursor_blink_state = true; // Start in visible state
    draw_cursor_if_visible();
}

/** @copydoc gcon::hide_cursor */
void hide_cursor() {
    if (!initialized)
        return;

    erase_cursor_if_drawn();
    cursor_visible = false;
    cursor_blink_state = false;
}

/** @copydoc gcon::is_cursor_visible */
bool is_cursor_visible() {
    return cursor_visible;
}

/** @copydoc gcon::update_cursor_blink */
void update_cursor_blink(u64 current_time_ms) {
    if (!initialized || !cursor_visible)
        return;

    // Check if it's time to toggle the blink state
    if (current_time_ms - last_blink_time >= CURSOR_BLINK_MS) {
        last_blink_time = current_time_ms;

        // Toggle blink state
        if (cursor_blink_state) {
            // Currently on, turn off
            erase_cursor_if_drawn();
            cursor_blink_state = false;
        } else {
            // Currently off, turn on
            cursor_blink_state = true;
            draw_cursor_if_visible();
        }
    }
}

// =============================================================================
// Scrollback Functions
// =============================================================================

namespace {

/**
 * @brief Draw a cell from the buffer at a screen position.
 */
void draw_cell(u32 cx, u32 cy, const Cell &cell) {
    // Temporarily set colors from the cell
    u32 saved_fg = fg_color;
    u32 saved_bg = bg_color;
    fg_color = cell.fg;
    bg_color = cell.bg;

    draw_char(cx, cy, cell.ch);

    fg_color = saved_fg;
    bg_color = saved_bg;
}

/**
 * @brief Redraw the entire screen from the scrollback buffer.
 *
 * @param offset Number of lines scrolled back (0 = live view)
 */
void redraw_from_buffer(u32 offset) {
    if (rows == 0)
        return;

    // Calculate which buffer line corresponds to screen row 0
    // buffer_head points to the NEXT line to write (one past current bottom)
    // So current bottom line is at (buffer_head - 1) mod SCROLLBACK_LINES
    // And screen row (rows-1) shows that line when offset=0
    // With offset, we go back further

    for (u32 screen_row = 0; screen_row < rows; screen_row++) {
        // Which buffer line does this screen row show?
        // At offset=0: screen_row 0 shows buffer_head - rows
        //              screen_row (rows-1) shows buffer_head - 1
        // At offset=N: shift everything back by N lines
        u32 base = (buffer_head >= rows + offset)
                       ? (buffer_head - rows - offset)
                       : (SCROLLBACK_LINES + buffer_head - rows - offset);
        u32 line_idx = (base + screen_row) % SCROLLBACK_LINES;

        // Draw each cell in this line
        for (u32 col = 0; col < cols && col < SCROLLBACK_COLS; col++) {
            draw_cell(col, screen_row, scrollback[line_idx][col]);
        }
    }
}

/**
 * @brief Draw a minimal scroll indicator in the top-right corner.
 *
 * Shows something like "↑5" when scrolled back 5 lines.
 */
void draw_scroll_indicator(u32 offset) {
    if (offset == 0 || cols < 6)
        return;

    // Format: "^NNN" where NNN is the offset (up to 3 digits)
    char indicator[8];
    u32 pos = 0;
    indicator[pos++] = '^';

    // Convert offset to digits
    char digits[4];
    u32 num_digits = 0;
    u32 n = offset;
    do {
        digits[num_digits++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0 && num_digits < 3);

    // Reverse digits into indicator
    while (num_digits > 0) {
        indicator[pos++] = digits[--num_digits];
    }
    indicator[pos] = '\0';

    // Draw at top-right corner with inverse colors
    u32 start_col = cols - pos - 1;
    u32 saved_fg = fg_color;
    u32 saved_bg = bg_color;
    fg_color = colors::VIPER_BLUE;
    bg_color = colors::WHITE;

    for (u32 i = 0; i < pos; i++) {
        draw_char(start_col + i, 0, indicator[i]);
    }

    fg_color = saved_fg;
    bg_color = saved_bg;
}

} // namespace

/** @copydoc gcon::scroll_up */
bool scroll_up() {
    if (!initialized)
        return false;

    // Can't scroll beyond available history
    u32 max_offset = (buffer_count > rows) ? (buffer_count - rows) : 0;
    if (scroll_offset >= max_offset)
        return false;

    scroll_offset++;
    erase_cursor_if_drawn();
    redraw_from_buffer(scroll_offset);
    draw_scroll_indicator(scroll_offset);
    // Don't show cursor when scrolled back
    return true;
}

/** @copydoc gcon::scroll_down */
bool scroll_down() {
    if (!initialized)
        return false;

    if (scroll_offset == 0)
        return false;

    scroll_offset--;
    erase_cursor_if_drawn();
    redraw_from_buffer(scroll_offset);

    if (scroll_offset > 0) {
        draw_scroll_indicator(scroll_offset);
    } else {
        // Back at live view, show cursor
        draw_cursor_if_visible();
    }
    return true;
}

/** @copydoc gcon::get_scroll_offset */
u32 get_scroll_offset() {
    return scroll_offset;
}

/** @copydoc gcon::is_scrolled_back */
bool is_scrolled_back() {
    return scroll_offset > 0;
}

/** @copydoc gcon::set_gui_mode */
void set_gui_mode(bool active) {
    gui_mode_active = active;
    if (active) {
        serial::puts("[gcon] GUI mode enabled - framebuffer output disabled\n");
    } else {
        serial::puts("[gcon] GUI mode disabled - framebuffer output enabled\n");
    }
}

/** @copydoc gcon::is_gui_mode */
bool is_gui_mode() {
    return gui_mode_active;
}

} // namespace gcon
