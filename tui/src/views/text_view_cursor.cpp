// tui/src/views/text_view_cursor.cpp
// @brief TextView cursor movement, selection, and helper routines.
// @invariant Cursor offsets remain within the text buffer bounds.
// @ownership TextView borrows TextBuffer and Theme from the caller.

#include "tui/views/text_view.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

using viper::tui::util::char_width;

namespace viper::tui::views
{
namespace
{
std::size_t clampAdd(std::size_t base, std::size_t delta)
{
    const std::size_t max = std::numeric_limits<std::size_t>::max();
    if (max - base < delta)
        return max;
    return base + delta;
}
} // namespace

TextView::TextView(text::TextBuffer &buf, const style::Theme &theme, bool showLineNumbers)
    : buf_(buf), theme_(theme), show_line_numbers_(showLineNumbers)
{
}

bool TextView::wantsFocus() const
{
    return true;
}

std::size_t TextView::cursorRow() const
{
    return cursor_row_;
}

std::size_t TextView::cursorCol() const
{
    return cursor_col_;
}

std::pair<char32_t, std::size_t> TextView::decodeChar(std::string_view s, std::size_t off)
{
    // UTF-8 decoding mirrors the terminal input pipeline: invalid sequences are
    // replaced with U+FFFD and treated as having a single cell of display width.
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
        return 0;
    if (row >= total)
        return buf_.size();

    auto line = buf_.lineView(row);
    std::size_t byteOffset = 0;
    std::size_t currentCol = 0;
    line.forEachSegment([&](std::string_view segment) -> bool {
        std::size_t idx = 0;
        while (idx < segment.size())
        {
            auto [cp, len] = decodeChar(segment, idx);
            if (len == 0)
                return false;
            std::size_t width = static_cast<std::size_t>(char_width(cp));
            if (currentCol + width > col)
                return false;
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
        if (row + 1 < total && clamped == clampAdd(rowStart, length))
        {
            ++row;
            if (row >= total)
                row = total - 1;
            rowStart = buf_.lineOffset(row);
            length = buf_.lineLength(row);
        }

        std::size_t inLineOffset = clamped > rowStart ? clamped - rowStart : 0;
        if (inLineOffset > length)
            inLineOffset = length;

        std::size_t col = 0;
        std::size_t consumed = 0;
        auto line = buf_.lineView(row);
        line.forEachSegment([&](std::string_view segment) -> bool {
            std::size_t idx = 0;
            while (idx < segment.size() && consumed < inLineOffset)
            {
                auto [cp, len] = decodeChar(segment, idx);
                if (len == 0)
                    return false;
                std::size_t advance = std::min(len, inLineOffset - consumed);
                consumed += advance;
                col += static_cast<std::size_t>(char_width(cp));
                idx += len;
                if (consumed >= inLineOffset)
                    break;
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

} // namespace viper::tui::views
