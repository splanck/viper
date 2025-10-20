// tui/src/views/text_view_render.cpp
// @brief TextView rendering logic for buffer contents and decorations.
// @invariant Rendering respects viewport bounds and selection state.
// @ownership TextView borrows TextBuffer and Theme from the caller.

#include "tui/views/text_view.hpp"

#include "tui/syntax/rules.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

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

void TextView::paint(render::ScreenBuffer &sb)
{
    const auto &normal = theme_.style(style::Role::Normal);
    const auto &sel = theme_.style(style::Role::Selection);
    const auto &accent = theme_.style(style::Role::Accent);
    const std::size_t gutter = show_line_numbers_ ? 4 : 0;
    const std::size_t selBegin = std::min(sel_start_, sel_end_);
    const std::size_t selFinish = std::max(sel_start_, sel_end_);
    const bool hasSelection = sel_start_ != sel_end_;

    for (int row = 0; row < rect_.h; ++row)
    {
        const std::size_t lineNo = top_row_ + static_cast<std::size_t>(row);
        const std::size_t lineStart = buf_.lineOffset(lineNo);
        const std::size_t lineLength = buf_.lineLength(lineNo);
        const std::size_t lineEnd = clampAdd(lineStart, lineLength);
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
            const std::size_t highlightEnd = clampAdd(h.first, h.second);
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
                const std::size_t global = clampAdd(lineStart, charByte);
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
                        const std::size_t spanEnd = clampAdd(sp.start, sp.length);
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
        const std::size_t localRow = cursor_row_ - top_row_;
        std::size_t gutterWidth = show_line_numbers_ ? 4 : 0;
        if (cursor_col_ < static_cast<std::size_t>(rect_.w - static_cast<int>(gutterWidth)))
        {
            auto &cell = sb.at(rect_.y + static_cast<int>(localRow),
                               rect_.x + static_cast<int>(gutterWidth + cursor_col_));
            cell.style = accent;
        }
    }
}

} // namespace viper::tui::views
