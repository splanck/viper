// tui/src/views/text_view.cpp
// @brief Implementation of TextView widget for editing buffers.
// @invariant Maintains cursor within line widths and buffer size.
// @ownership TextView borrows TextBuffer and Theme.

#include "tui/views/text_view.hpp"

#include <algorithm>
#include <string>

using viper::tui::util::char_width;

namespace viper::tui::views
{
namespace
{
std::pair<char32_t, std::size_t> decodeChar(const std::string &s, std::size_t off)
{
    unsigned char c = static_cast<unsigned char>(s[off]);
    if (c < 0x80)
        return {c, 1};
    if ((c >> 5) == 0x6 && off + 1 < s.size())
        return {static_cast<char32_t>(((c & 0x1F) << 6) |
                                      (static_cast<unsigned char>(s[off + 1]) & 0x3F)),
                2};
    if ((c >> 4) == 0xE && off + 2 < s.size())
        return {static_cast<char32_t>(((c & 0x0F) << 12) |
                                      ((static_cast<unsigned char>(s[off + 1]) & 0x3F) << 6) |
                                      (static_cast<unsigned char>(s[off + 2]) & 0x3F)),
                3};
    if ((c >> 3) == 0x1E && off + 3 < s.size())
        return {static_cast<char32_t>(((c & 0x07) << 18) |
                                      ((static_cast<unsigned char>(s[off + 1]) & 0x3F) << 12) |
                                      ((static_cast<unsigned char>(s[off + 2]) & 0x3F) << 6) |
                                      (static_cast<unsigned char>(s[off + 3]) & 0x3F)),
                4};
    return {U'\uFFFD', 1};
}
} // namespace

TextView::TextView(text::TextBuffer &buf, const style::Theme &theme, bool showLineNumbers)
    : buf_(buf), theme_(theme), show_line_numbers_(showLineNumbers)
{
}

std::pair<char32_t, std::size_t> TextView::decodeChar(const std::string &s, std::size_t off)
{
    return ::viper::tui::views::decodeChar(s, off);
}

std::size_t TextView::lineWidth(const std::string &line)
{
    std::size_t i = 0;
    std::size_t c = 0;
    while (i < line.size())
    {
        auto [cp, len] = decodeChar(line, i);
        c += char_width(cp);
        i += len;
    }
    return c;
}

std::size_t TextView::columnToOffset(const std::string &line, std::size_t col)
{
    std::size_t i = 0;
    std::size_t c = 0;
    while (i < line.size())
    {
        auto [cp, len] = decodeChar(line, i);
        std::size_t w = static_cast<std::size_t>(char_width(cp));
        if (c + w > col)
            break;
        i += len;
        c += w;
    }
    return i;
}

std::size_t TextView::offsetFromRowCol(std::size_t row, std::size_t col) const
{
    std::string text = buf_.str();
    std::size_t off = 0;
    std::size_t r = 0;
    while (r < row)
    {
        std::size_t pos = text.find('\n', off);
        if (pos == std::string::npos)
            return text.size();
        off = pos + 1;
        ++r;
    }
    std::size_t lineEnd = text.find('\n', off);
    std::string line =
        lineEnd == std::string::npos ? text.substr(off) : text.substr(off, lineEnd - off);
    return off + columnToOffset(line, col);
}

std::size_t TextView::totalLines() const
{
    std::string text = buf_.str();
    return std::count(text.begin(), text.end(), '\n') + 1;
}

void TextView::setCursor(std::size_t row, std::size_t col, bool shift, bool updateTarget)
{
    cursor_row_ = row;
    cursor_col_ = col;
    if (updateTarget)
        target_col_ = col;
    cursor_offset_ = offsetFromRowCol(row, col);
    if (shift)
        sel_end_ = cursor_offset_;
    else
        sel_start_ = sel_end_ = cursor_offset_;
}

void TextView::setHighlights(std::vector<std::pair<std::size_t, std::size_t>> ranges)
{
    highlights_ = std::move(ranges);
}

void TextView::moveCursorToOffset(std::size_t off)
{
    std::string text = buf_.str();
    std::size_t row = 0;
    std::size_t col = 0;
    std::size_t pos = 0;
    while (pos < off && pos < text.size())
    {
        if (text[pos] == '\n')
        {
            ++row;
            col = 0;
            ++pos;
            continue;
        }
        auto [cp, len] = decodeChar(text, pos);
        col += static_cast<std::size_t>(char_width(cp));
        pos += len;
    }
    setCursor(row, col, false, true);
    if (cursor_row_ < top_row_)
        top_row_ = cursor_row_;
    if (cursor_row_ >= top_row_ + static_cast<std::size_t>(rect_.h))
        top_row_ = cursor_row_ - static_cast<std::size_t>(rect_.h) + 1;
}

void TextView::paint(render::ScreenBuffer &sb)
{
    const auto &normal = theme_.style(style::Role::Normal);
    const auto &sel = theme_.style(style::Role::Selection);
    const auto &accent = theme_.style(style::Role::Accent);
    std::size_t gutter = show_line_numbers_ ? 4 : 0;

    for (int row = 0; row < rect_.h; ++row)
    {
        std::size_t lineNo = top_row_ + static_cast<std::size_t>(row);
        std::string line = buf_.getLine(lineNo);
        std::size_t lineStart = offsetFromRowCol(lineNo, 0);

        if (show_line_numbers_)
        {
            std::string num = std::to_string(lineNo + 1);
            if (num.size() < gutter - 1)
                num = std::string(gutter - 1 - num.size(), ' ') + num;
            num.push_back(' ');
            for (std::size_t i = 0; i < gutter && i < num.size(); ++i)
            {
                auto &cell = sb.at(rect_.y + row, rect_.x + static_cast<int>(i));
                cell.ch = static_cast<char32_t>(num[i]);
                cell.style = normal;
            }
        }

        std::size_t byte = 0;
        std::size_t col = 0;
        while (byte < line.size() &&
               col < static_cast<std::size_t>(rect_.w - static_cast<int>(gutter)))
        {
            auto [cp, len] = decodeChar(line, byte);
            std::size_t w = static_cast<std::size_t>(char_width(cp));
            if (col + w > static_cast<std::size_t>(rect_.w - static_cast<int>(gutter)))
                break;
            std::size_t global = lineStart + byte;
            bool selected = sel_start_ != sel_end_ && global >= std::min(sel_start_, sel_end_) &&
                            global < std::max(sel_start_, sel_end_);
            bool highlighted = false;
            for (const auto &h : highlights_)
            {
                if (global >= h.first && global < h.first + h.second)
                {
                    highlighted = true;
                    break;
                }
            }
            auto &cell = sb.at(rect_.y + row, rect_.x + static_cast<int>(gutter + col));
            cell.ch = cp;
            cell.width = static_cast<uint8_t>(w);
            cell.style = selected ? sel : (highlighted ? accent : normal);
            byte += len;
            col += w;
        }
    }

    if (cursor_row_ >= top_row_ && cursor_row_ < top_row_ + static_cast<std::size_t>(rect_.h))
    {
        std::size_t gutter = show_line_numbers_ ? 4 : 0;
        if (cursor_col_ < static_cast<std::size_t>(rect_.w - static_cast<int>(gutter)))
        {
            auto &cell = sb.at(rect_.y + static_cast<int>(cursor_row_ - top_row_),
                               rect_.x + static_cast<int>(gutter + cursor_col_));
            cell.style = accent;
        }
    }
}

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
