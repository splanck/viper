//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/views/text_view_input.cpp
// Purpose: Process terminal input events for the TextView widget, translating
//          key presses into cursor navigation and selection updates.
// Key invariants: Navigation always keeps cursor offsets within the text buffer
//                 and maintains consistent selection anchors.
// Ownership/Lifetime: TextView borrows TextBuffer and Theme instances; this
//                     translation unit mutates only TextView state.
// Links: docs/architecture.md#vipertui-architecture
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements event handling for TextView navigation.
/// @details The logic converts terminal key events into cursor movement,
///          viewport scrolling, and selection updates. Keeping it separate from
///          rendering allows other components to reuse the view without dragging
///          in terminal-specific code.

#include "tui/views/text_view.hpp"

#include <algorithm>
#include <string>

using viper::tui::util::char_width;

namespace viper::tui::views
{

/// @brief Handle a terminal input event and update cursor/selection state.
/// @details Maps cursor keys, paging keys, and home/end navigation to the
///          corresponding TextView helpers while preserving the "sticky" target
///          column used during vertical motion. Shift modifiers extend the
///          active selection; otherwise it collapses to the new caret.
/// @param ev UI event describing the received key press.
/// @return True when the event was handled and should not propagate further.
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
