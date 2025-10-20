// tui/src/views/text_view_input.cpp
// @brief TextView input handling and cursor navigation logic.
// @invariant Navigation maintains cursor and selection invariants.
// @ownership TextView borrows TextBuffer and Theme from the caller.

#include "tui/views/text_view.hpp"

#include <algorithm>
#include <string>

using viper::tui::util::char_width;

namespace viper::tui::views
{

bool TextView::onEvent(const ui::Event &ev)
{
    using Code = term::KeyEvent::Code;
    using Mods = term::KeyEvent::Mods;
    bool shift = (ev.key.mods & Mods::Shift) != 0;
    switch (ev.key.code)
    {
        case Code::Left:
        {
            if (cursor_col_ == 0)
                return true;
            std::string line = buf_.getLine(cursor_row_);
            std::size_t curByte = columnToOffset(line, cursor_col_);
            std::size_t i = 0;
            std::size_t c = 0;
            std::size_t prevCol = 0;
            while (i < curByte)
            {
                auto [cp, len] = decodeChar(line, i);
                prevCol = c;
                c += static_cast<std::size_t>(char_width(cp));
                i += len;
            }
            setCursor(cursor_row_, prevCol, shift, true);
            if (cursor_row_ < top_row_)
                top_row_ = cursor_row_;
            return true;
        }
        case Code::Right:
        {
            std::string line = buf_.getLine(cursor_row_);
            std::size_t lineW = lineWidth(line);
            if (cursor_col_ >= lineW)
                return true;
            std::size_t curByte = columnToOffset(line, cursor_col_);
            auto [cp, len] = decodeChar(line, curByte);
            std::size_t newCol = cursor_col_ + static_cast<std::size_t>(char_width(cp));
            setCursor(cursor_row_, newCol, shift, true);
            return true;
        }
        case Code::Home:
        {
            setCursor(cursor_row_, 0, shift, true);
            if (cursor_row_ < top_row_)
                top_row_ = cursor_row_;
            return true;
        }
        case Code::End:
        {
            std::string line = buf_.getLine(cursor_row_);
            setCursor(cursor_row_, lineWidth(line), shift, true);
            return true;
        }
        case Code::Up:
        {
            if (cursor_row_ == 0)
                return true;
            std::size_t newRow = cursor_row_ - 1;
            std::string line = buf_.getLine(newRow);
            std::size_t lineW = lineWidth(line);
            std::size_t newCol = std::min(target_col_, lineW);
            setCursor(newRow, newCol, shift, false);
            if (cursor_row_ < top_row_)
                top_row_ = cursor_row_;
            return true;
        }
        case Code::Down:
        {
            std::size_t total = totalLines();
            if (cursor_row_ + 1 >= total)
                return true;
            std::size_t newRow = cursor_row_ + 1;
            std::string line = buf_.getLine(newRow);
            std::size_t lineW = lineWidth(line);
            std::size_t newCol = std::min(target_col_, lineW);
            setCursor(newRow, newCol, shift, false);
            if (cursor_row_ >= top_row_ + static_cast<std::size_t>(rect_.h))
                top_row_ = cursor_row_ - static_cast<std::size_t>(rect_.h) + 1;
            return true;
        }
        case Code::PageUp:
        {
            std::size_t page = rect_.h > 0 ? static_cast<std::size_t>(rect_.h) : 1;
            std::size_t newRow = cursor_row_ > page ? cursor_row_ - page : 0;
            std::string line = buf_.getLine(newRow);
            std::size_t lineW = lineWidth(line);
            std::size_t newCol = std::min(target_col_, lineW);
            setCursor(newRow, newCol, shift, false);
            top_row_ = newRow;
            return true;
        }
        case Code::PageDown:
        {
            std::size_t page = rect_.h > 0 ? static_cast<std::size_t>(rect_.h) : 1;
            std::size_t total = totalLines();
            std::size_t newRow = std::min(cursor_row_ + page, total > 0 ? total - 1 : 0);
            std::string line = buf_.getLine(newRow);
            std::size_t lineW = lineWidth(line);
            std::size_t newCol = std::min(target_col_, lineW);
            setCursor(newRow, newCol, shift, false);
            if (total > static_cast<std::size_t>(rect_.h))
                top_row_ = std::min(newRow, total - static_cast<std::size_t>(rect_.h));
            else
                top_row_ = 0;
            return true;
        }
        default:
            return false;
    }
}

} // namespace viper::tui::views
