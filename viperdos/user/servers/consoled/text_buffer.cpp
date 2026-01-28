//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "text_buffer.hpp"
#include "../../include/viper_colors.h"

namespace consoled {

// Cursor color (Amiga orange)
static constexpr uint32_t CURSOR_COLOR = 0xFFFF8800;

TextBuffer::~TextBuffer() {
    delete[] m_buffer;
}

bool TextBuffer::init(
    gui_window_t *window, uint32_t cols, uint32_t rows, uint32_t default_fg, uint32_t default_bg) {
    m_window = window;
    m_cols = cols;
    m_rows = rows;
    m_default_fg = default_fg;
    m_default_bg = default_bg;
    m_fg_color = default_fg;
    m_bg_color = default_bg;

    m_buffer = new Cell[cols * rows];
    if (!m_buffer) {
        return false;
    }

    clear();
    return true;
}

Cell &TextBuffer::cell_at(uint32_t x, uint32_t y) {
    return m_buffer[y * m_cols + x];
}

const Cell &TextBuffer::cell_at(uint32_t x, uint32_t y) const {
    return m_buffer[y * m_cols + x];
}

void TextBuffer::set_cursor(uint32_t x, uint32_t y) {
    if (m_cursor_visible) {
        draw_cell(m_cursor_x, m_cursor_y);
    }

    m_cursor_x = (x < m_cols) ? x : m_cols - 1;
    m_cursor_y = (y < m_rows) ? y : m_rows - 1;

    if (m_cursor_visible) {
        draw_cursor();
    }
    m_needs_present = true;
}

void TextBuffer::move_cursor(int32_t dx, int32_t dy) {
    if (m_cursor_visible) {
        draw_cell(m_cursor_x, m_cursor_y);
    }

    int32_t new_x = static_cast<int32_t>(m_cursor_x) + dx;
    int32_t new_y = static_cast<int32_t>(m_cursor_y) + dy;

    if (new_x < 0)
        new_x = 0;
    if (new_x >= static_cast<int32_t>(m_cols))
        new_x = m_cols - 1;
    if (new_y < 0)
        new_y = 0;
    if (new_y >= static_cast<int32_t>(m_rows))
        new_y = m_rows - 1;

    m_cursor_x = static_cast<uint32_t>(new_x);
    m_cursor_y = static_cast<uint32_t>(new_y);

    if (m_cursor_visible) {
        draw_cursor();
    }
    m_needs_present = true;
}

void TextBuffer::set_cursor_visible(bool visible) {
    if (visible == m_cursor_visible)
        return;

    if (visible) {
        m_cursor_visible = true;
        draw_cursor();
    } else {
        draw_cell(m_cursor_x, m_cursor_y);
        m_cursor_visible = false;
    }
    m_needs_present = true;
}

void TextBuffer::save_cursor() {
    m_saved_cursor_x = m_cursor_x;
    m_saved_cursor_y = m_cursor_y;
}

void TextBuffer::restore_cursor() {
    set_cursor(m_saved_cursor_x, m_saved_cursor_y);
}

void TextBuffer::set_colors(uint32_t fg, uint32_t bg) {
    m_fg_color = fg;
    m_bg_color = bg;
}

void TextBuffer::reset_colors() {
    m_fg_color = m_default_fg;
    m_bg_color = m_default_bg;
}

void TextBuffer::putchar(char ch) {
    // Erase old cursor
    if (m_cursor_visible) {
        draw_cell(m_cursor_x, m_cursor_y);
    }

    // Update buffer and draw
    Cell &c = cell_at(m_cursor_x, m_cursor_y);
    c.ch = ch;
    c.fg = m_fg_color;
    c.bg = m_bg_color;
    draw_cell(m_cursor_x, m_cursor_y);
    m_needs_present = true;

    // Advance cursor
    m_cursor_x++;
    if (m_cursor_x >= m_cols) {
        m_cursor_x = 0;
        m_cursor_y++;
        if (m_cursor_y >= m_rows) {
            m_cursor_y = m_rows - 1;
            scroll_up();
        }
    }

    // Draw new cursor
    if (m_cursor_visible) {
        draw_cursor();
    }
}

void TextBuffer::newline() {
    if (m_cursor_visible) {
        draw_cell(m_cursor_x, m_cursor_y);
    }

    m_cursor_x = 0;
    m_cursor_y++;

    if (m_cursor_y >= m_rows) {
        m_cursor_y = m_rows - 1;
        scroll_up();
    }

    if (m_cursor_visible) {
        draw_cursor();
    }
    m_needs_present = true;
}

void TextBuffer::carriage_return() {
    if (m_cursor_visible) {
        draw_cell(m_cursor_x, m_cursor_y);
    }
    m_cursor_x = 0;
    if (m_cursor_visible) {
        draw_cursor();
    }
    m_needs_present = true;
}

void TextBuffer::tab() {
    uint32_t next_tab = (m_cursor_x + 8) & ~7u;
    if (next_tab > m_cols)
        next_tab = m_cols;
    while (m_cursor_x < next_tab) {
        putchar(' ');
    }
}

void TextBuffer::backspace() {
    if (m_cursor_x > 0) {
        if (m_cursor_visible) {
            draw_cell(m_cursor_x, m_cursor_y);
        }
        m_cursor_x--;
        Cell &c = cell_at(m_cursor_x, m_cursor_y);
        c.ch = ' ';
        draw_cell(m_cursor_x, m_cursor_y);
        if (m_cursor_visible) {
            draw_cursor();
        }
        m_needs_present = true;
    }
}

void TextBuffer::clear() {
    for (uint32_t i = 0; i < m_cols * m_rows; i++) {
        m_buffer[i].ch = ' ';
        m_buffer[i].fg = m_fg_color;
        m_buffer[i].bg = m_bg_color;
    }
}

void TextBuffer::clear_to_eol() {
    for (uint32_t x = m_cursor_x; x < m_cols; x++) {
        Cell &c = cell_at(x, m_cursor_y);
        c.ch = ' ';
        c.fg = m_fg_color;
        c.bg = m_bg_color;
        draw_cell(x, m_cursor_y);
    }
    m_needs_present = true;
}

void TextBuffer::clear_to_bol() {
    for (uint32_t x = 0; x <= m_cursor_x && x < m_cols; x++) {
        Cell &c = cell_at(x, m_cursor_y);
        c.ch = ' ';
        c.fg = m_fg_color;
        c.bg = m_bg_color;
        draw_cell(x, m_cursor_y);
    }
    m_needs_present = true;
}

void TextBuffer::clear_line() {
    for (uint32_t x = 0; x < m_cols; x++) {
        Cell &c = cell_at(x, m_cursor_y);
        c.ch = ' ';
        c.fg = m_fg_color;
        c.bg = m_bg_color;
        draw_cell(x, m_cursor_y);
    }
    m_needs_present = true;
}

void TextBuffer::clear_to_eos() {
    clear_to_eol();
    for (uint32_t y = m_cursor_y + 1; y < m_rows; y++) {
        for (uint32_t x = 0; x < m_cols; x++) {
            Cell &c = cell_at(x, y);
            c.ch = ' ';
            c.fg = m_fg_color;
            c.bg = m_bg_color;
            draw_cell(x, y);
        }
    }
    m_needs_present = true;
}

void TextBuffer::clear_to_bos() {
    for (uint32_t y = 0; y < m_cursor_y; y++) {
        for (uint32_t x = 0; x < m_cols; x++) {
            Cell &c = cell_at(x, y);
            c.ch = ' ';
            c.fg = m_fg_color;
            c.bg = m_bg_color;
            draw_cell(x, y);
        }
    }
    clear_to_bol();
    m_needs_present = true;
}

void TextBuffer::scroll_up() {
    // Shift buffer up by one row
    for (uint32_t y = 0; y < m_rows - 1; y++) {
        for (uint32_t x = 0; x < m_cols; x++) {
            cell_at(x, y) = cell_at(x, y + 1);
        }
    }

    // Clear bottom row
    for (uint32_t x = 0; x < m_cols; x++) {
        Cell &c = cell_at(x, m_rows - 1);
        c.ch = ' ';
        c.fg = m_fg_color;
        c.bg = m_bg_color;
    }

    redraw_all();
    m_needs_present = true;
}

void TextBuffer::draw_cell(uint32_t cx, uint32_t cy) {
    Cell &c = cell_at(cx, cy);
    uint32_t px = PADDING + cx * FONT_WIDTH;
    uint32_t py = PADDING + cy * FONT_HEIGHT;
    gui_draw_char_scaled(m_window, px, py, c.ch, c.fg, c.bg, FONT_SCALE);
}

void TextBuffer::draw_cursor() {
    if (!m_cursor_visible)
        return;

    uint32_t px = PADDING + m_cursor_x * FONT_WIDTH;
    uint32_t py = PADDING + m_cursor_y * FONT_HEIGHT;

    Cell &c = cell_at(m_cursor_x, m_cursor_y);
    gui_draw_char_scaled(m_window, px, py, c.ch, c.bg, CURSOR_COLOR, FONT_SCALE);
}

void TextBuffer::redraw_all() {
    for (uint32_t y = 0; y < m_rows; y++) {
        for (uint32_t x = 0; x < m_cols; x++) {
            draw_cell(x, y);
        }
    }
    draw_cursor();
}

void TextBuffer::present_cell(uint32_t cx, uint32_t cy) {
    uint32_t px = PADDING + cx * FONT_WIDTH;
    uint32_t py = PADDING + cy * FONT_HEIGHT;
    gui_present_region(m_window, px, py, FONT_WIDTH, FONT_HEIGHT);
}

} // namespace consoled
