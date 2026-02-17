//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file text_buffer_stub.cpp
 * @brief Linker stubs for TextBuffer methods.
 *
 * The shell process links against embedded_shell.cpp which references
 * TextBuffer methods behind null guards (if (m_buffer) ...). These stubs
 * satisfy the linker — they are never called at runtime because m_buffer
 * is always nullptr in PTY mode.
 */
//===----------------------------------------------------------------------===//

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
#include "text_buffer.hpp"
#pragma clang diagnostic pop

namespace consoled {

TextBuffer::~TextBuffer() {}

bool TextBuffer::init(gui_window_t *, uint32_t, uint32_t, uint32_t, uint32_t) {
    return false;
}

Cell &TextBuffer::cell_at(uint32_t, uint32_t) {
    static Cell dummy{};
    return dummy;
}

const Cell &TextBuffer::cell_at(uint32_t, uint32_t) const {
    static Cell dummy{};
    return dummy;
}

void TextBuffer::set_cursor(uint32_t, uint32_t) {}
void TextBuffer::move_cursor(int32_t, int32_t) {}
void TextBuffer::set_cursor_visible(bool) {}
void TextBuffer::save_cursor() {}
void TextBuffer::restore_cursor() {}
void TextBuffer::set_colors(uint32_t, uint32_t) {}
void TextBuffer::reset_colors() {}
void TextBuffer::putchar(char) {}
void TextBuffer::newline() {}
void TextBuffer::carriage_return() {}
void TextBuffer::tab() {}
void TextBuffer::backspace() {}
void TextBuffer::clear() {}
void TextBuffer::clear_to_eol() {}
void TextBuffer::clear_to_bol() {}
void TextBuffer::clear_line() {}
void TextBuffer::clear_to_eos() {}
void TextBuffer::clear_to_bos() {}
void TextBuffer::scroll_up() {}
void TextBuffer::draw_cell(uint32_t, uint32_t) {}
void TextBuffer::draw_cursor() {}
void TextBuffer::redraw_all() {}
void TextBuffer::present_cell(uint32_t, uint32_t) {}
void TextBuffer::begin_batch() {}
void TextBuffer::end_batch() {}

} // namespace consoled
