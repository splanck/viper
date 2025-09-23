// tui/src/views/text_view.cpp
// @brief Implementation of TextView widget for editing buffers.
// @invariant Maintains cursor within line widths and buffer size.
// @ownership TextView borrows TextBuffer and Theme.

#include "tui/views/text_view.hpp"
#include "tui/render/screen.hpp"
#include "tui/syntax/rules.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>

using viper::tui::util::char_width;

namespace viper::tui::views
{
namespace
{
std::pair<char32_t, std::size_t> decodeChar(std::string_view s, std::size_t off)
{
    if (off >= s.size())
        return {U'\uFFFD', 1};
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

/// @brief Text views capture focus to process editing input.
bool TextView::wantsFocus() const
{
    return true;
}

/// @brief Retrieve the current cursor row (0-based line index).
std::size_t TextView::cursorRow() const
{
    return cursor_row_;
}

/// @brief Retrieve the current cursor column in display cells.
std::size_t TextView::cursorCol() const
{
    return cursor_col_;
}

std::pair<char32_t, std::size_t> TextView::decodeChar(std::string_view s, std::size_t off)
{
    return ::viper::tui::views::decodeChar(s, off);
}

std::size_t TextView::lineWidth(std::string_view line)
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

std::size_t TextView::columnToOffset(std::string_view line, std::size_t col)
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
    const std::size_t total = buf_.lineCount();
    if (total == 0)
    {
        return 0;
    }
    if (row >= total)
    {
        return buf_.size();
    }

    auto line = buf_.lineView(row);
    std::size_t byteOffset = 0;
    std::size_t currentCol = 0;
    line.forEachSegment([&](std::string_view segment) -> bool {
        std::size_t idx = 0;
        while (idx < segment.size())
        {
            auto [cp, len] = decodeChar(segment, idx);
            if (len == 0)
            {
                return false;
            }
            std::size_t width = static_cast<std::size_t>(char_width(cp));
            if (currentCol + width > col)
            {
                return false;
            }
            idx += len;
            byteOffset += len;
            currentCol += width;
        }
        return true;
    });
    return line.offset() + byteOffset;
}

std::size_t TextView::totalLines() const
{
    return buf_.lineCount();
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
    const std::size_t size = buf_.size();
    const std::size_t clamped = std::min(off, size);
    const std::size_t total = buf_.lineCount();

    std::size_t row = 0;
    std::size_t rowStart = 0;

    if (total > 0)
    {
        std::size_t low = 0;
        std::size_t high = total;
        while (low < high)
        {
            std::size_t mid = (low + high) / 2;
            std::size_t start = buf_.lineOffset(mid);
            if (start <= clamped)
            {
                row = mid;
                rowStart = start;
                low = mid + 1;
            }
            else
            {
                high = mid;
            }
        }
        rowStart = buf_.lineOffset(row);

        std::size_t length = buf_.lineLength(row);
        if (row + 1 < total && clamped == rowStart + length)
        {
            ++row;
            if (row >= total)
            {
                row = total - 1;
            }
            rowStart = buf_.lineOffset(row);
            length = buf_.lineLength(row);
        }

        std::size_t inLineOffset = clamped > rowStart ? clamped - rowStart : 0;
        if (inLineOffset > length)
        {
            inLineOffset = length;
        }

        std::size_t col = 0;
        std::size_t consumed = 0;
        auto line = buf_.lineView(row);
        line.forEachSegment([&](std::string_view segment) -> bool {
            std::size_t idx = 0;
            while (idx < segment.size() && consumed < inLineOffset)
            {
                auto [cp, len] = decodeChar(segment, idx);
                if (len == 0)
                {
                    return false;
                }
                std::size_t advance = std::min(len, inLineOffset - consumed);
                consumed += advance;
                col += static_cast<std::size_t>(char_width(cp));
                idx += len;
                if (consumed >= inLineOffset)
                {
                    break;
                }
            }
            return consumed < inLineOffset;
        });

        setCursor(row, col, false, true);
    }
    else
    {
        setCursor(0, 0, false, true);
    }

    if (cursor_row_ < top_row_)
        top_row_ = cursor_row_;
    if (cursor_row_ >= top_row_ + static_cast<std::size_t>(rect_.h))
        top_row_ = cursor_row_ - static_cast<std::size_t>(rect_.h) + 1;
}

void TextView::setSyntax(syntax::SyntaxRuleSet *syntax)
{
    syntax_ = syntax;
}

void TextView::paint(render::ScreenBuffer &sb)
{
    const auto &normal = theme_.style(style::Role::Normal);
    const auto &sel = theme_.style(style::Role::Selection);
    const auto &accent = theme_.style(style::Role::Accent);
    const std::size_t gutter = show_line_numbers_ ? 4 : 0;
    const auto saturatingAdd = [](std::size_t lhs, std::size_t rhs) {
        const std::size_t max = std::numeric_limits<std::size_t>::max();
        if (max - lhs < rhs)
            return max;
        return lhs + rhs;
    };
    const std::size_t selBegin = std::min(sel_start_, sel_end_);
    const std::size_t selFinish = std::max(sel_start_, sel_end_);
    const bool hasSelection = sel_start_ != sel_end_;

    for (int row = 0; row < rect_.h; ++row)
    {
        const std::size_t lineNo = top_row_ + static_cast<std::size_t>(row);
        const std::size_t lineStart = buf_.lineOffset(lineNo);
        const std::size_t lineLength = buf_.lineLength(lineNo);
        const std::size_t lineEnd = saturatingAdd(lineStart, lineLength);
        auto lineView = buf_.lineView(lineNo);
        const std::vector<syntax::Span> *spansPtr = nullptr;
        if (syntax_)
        {
            std::string scratch = buf_.getLine(lineNo);
            spansPtr = &syntax_->spans(lineNo, scratch);
        }

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

        const int availableWidth = rect_.w - static_cast<int>(gutter);
        const std::size_t availableCols = availableWidth > 0 ? static_cast<std::size_t>(availableWidth) : 0U;
        const bool lineHasSelection = hasSelection && lineStart < selFinish && lineEnd > selBegin;
        const bool lineHasHighlights = std::any_of(highlights_.begin(), highlights_.end(), [&](const auto &h) {
            if (h.second == 0)
                return false;
            const std::size_t highlightEnd = saturatingAdd(h.first, h.second);
            return lineStart < highlightEnd && lineEnd > h.first;
        });

        std::size_t lineByte = 0;
        std::size_t col = 0;
        lineView.forEachSegment([&](std::string_view segment) -> bool {
            std::size_t segOffset = 0;
            while (segOffset < segment.size())
            {
                if (col >= availableCols)
                    return false;

                auto [cp, len] = decodeChar(segment, segOffset);
                std::size_t w = static_cast<std::size_t>(char_width(cp));
                if (col + w > availableCols)
                    return false;

                const std::size_t charByte = lineByte;
                const std::size_t global = saturatingAdd(lineStart, charByte);
                const bool selected = lineHasSelection && global >= selBegin && global < selFinish;
                bool highlighted = false;
                if (lineHasHighlights)
                {
                    for (const auto &h : highlights_)
                    {
                        if (global < h.first)
                            continue;
                        const std::size_t offset = global - h.first;
                        if (offset < h.second)
                        {
                            highlighted = true;
                            break;
                        }
                    }
                }

                auto &cell = sb.at(rect_.y + row, rect_.x + static_cast<int>(gutter + col));
                cell.ch = cp;
                cell.width = static_cast<uint8_t>(w);
                render::Style syn = normal;
                if (spansPtr)
                {
                    for (const auto &sp : *spansPtr)
                    {
                        const std::size_t spanEnd = saturatingAdd(sp.start, sp.length);
                        if (charByte >= sp.start && charByte < spanEnd)
                        {
                            syn = sp.style;
                            break;
                        }
                    }
                }
                cell.style = selected ? sel : (highlighted ? accent : syn);

                segOffset += len;
                lineByte += len;
                col += w;
            }
            return true;
        });
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
