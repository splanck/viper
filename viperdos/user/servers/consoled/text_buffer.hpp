//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <gui.h> // Includes stdint.h with type definitions

namespace consoled {

// =============================================================================
// Constants
// =============================================================================

constexpr uint32_t FONT_SCALE = 3;                   // Scale: 2=1x, 3=1.5x, 4=2x
constexpr uint32_t FONT_WIDTH = 8 * FONT_SCALE / 2;  // 12 pixels at 1.5x
constexpr uint32_t FONT_HEIGHT = 8 * FONT_SCALE / 2; // 12 pixels at 1.5x
constexpr uint32_t PADDING = 8;

// =============================================================================
// Cell Structure
// =============================================================================

struct Cell {
    char ch;
    uint32_t fg;
    uint32_t bg;
};

// =============================================================================
// TextBuffer Class
// =============================================================================

class TextBuffer {
  public:
    TextBuffer() = default;
    ~TextBuffer();

    // Initialization
    bool init(gui_window_t *window,
              uint32_t cols,
              uint32_t rows,
              uint32_t default_fg,
              uint32_t default_bg);

    // Buffer access
    Cell &cell_at(uint32_t x, uint32_t y);
    const Cell &cell_at(uint32_t x, uint32_t y) const;

    // Cursor management
    uint32_t cursor_x() const {
        return m_cursor_x;
    }

    uint32_t cursor_y() const {
        return m_cursor_y;
    }

    void set_cursor(uint32_t x, uint32_t y);
    void move_cursor(int32_t dx, int32_t dy);
    void set_cursor_visible(bool visible);

    bool cursor_visible() const {
        return m_cursor_visible;
    }

    void save_cursor();
    void restore_cursor();

    // Colors
    void set_colors(uint32_t fg, uint32_t bg);
    void reset_colors();

    uint32_t fg_color() const {
        return m_fg_color;
    }

    uint32_t bg_color() const {
        return m_bg_color;
    }

    uint32_t default_fg() const {
        return m_default_fg;
    }

    uint32_t default_bg() const {
        return m_default_bg;
    }

    // Text output
    void putchar(char ch);
    void newline();
    void carriage_return();
    void tab();
    void backspace();

    // Clearing
    void clear();
    void clear_to_eol();
    void clear_to_bol();
    void clear_line();
    void clear_to_eos();
    void clear_to_bos();

    // Scrolling
    void scroll_up();

    // Rendering
    void draw_cell(uint32_t x, uint32_t y);
    void draw_cursor();
    void redraw_all();
    void present_cell(uint32_t x, uint32_t y);

    // Dimensions
    uint32_t cols() const {
        return m_cols;
    }

    uint32_t rows() const {
        return m_rows;
    }

    // Batch mode — suppresses cursor draw/erase during bulk writes
    void begin_batch();
    void end_batch();

    bool batch_mode() const {
        return m_batch_mode;
    }

    // Presentation tracking
    bool needs_present() const {
        return m_needs_present;
    }

    void set_needs_present(bool v) {
        m_needs_present = v;
    }

    void clear_needs_present() {
        m_needs_present = false;
    }

  private:
    gui_window_t *m_window = nullptr;
    Cell *m_buffer = nullptr;
    uint32_t m_cols = 0;
    uint32_t m_rows = 0;

    // Cursor
    uint32_t m_cursor_x = 0;
    uint32_t m_cursor_y = 0;
    bool m_cursor_visible = true;
    uint32_t m_saved_cursor_x = 0;
    uint32_t m_saved_cursor_y = 0;

    // Colors
    uint32_t m_fg_color = 0;
    uint32_t m_bg_color = 0;
    uint32_t m_default_fg = 0;
    uint32_t m_default_bg = 0;

    // Presentation
    bool m_needs_present = false;
    bool m_batch_mode = false;
};

} // namespace consoled
