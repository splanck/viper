//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/views/test_text_view_large_buffer.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/style/theme.hpp"
#include "tui/views/text_view.hpp"

#include <cassert>
#include <string>

using viper::tui::style::Theme;
using viper::tui::text::TextBuffer;
using viper::tui::views::TextView;

namespace
{
std::string makeLargeBuffer(std::size_t lines, std::size_t width)
{
    std::string text;
    text.reserve(lines * (width + 1));
    for (std::size_t i = 0; i < lines; ++i)
    {
        text.append(width, static_cast<char>('a' + static_cast<char>(i % 26))); // ASCII payload
        if (i + 1 < lines)
        {
            text.push_back('\n');
        }
    }
    return text;
}
} // namespace

int main()
{
    constexpr std::size_t kLines = 2048;
    constexpr std::size_t kWidth = 96;

    TextBuffer buf;
    buf.load(makeLargeBuffer(kLines, kWidth));

    assert(buf.lineCount() == kLines);
    const std::size_t sample = kLines / 2;
    assert(buf.lineOffset(sample) == sample * (kWidth + 1));
    assert(buf.lineLength(sample) == kWidth);
    assert(buf.lineStart(sample) == buf.lineOffset(sample));
    assert(buf.lineEnd(sample) == buf.lineStart(sample) + kWidth);
    assert(buf.lineOffset(kLines - 1) == (kLines - 1) * (kWidth + 1));
    assert(buf.lineLength(kLines - 1) == kWidth);
    assert(buf.lineEnd(kLines - 1) == buf.size());
    assert(buf.lineStart(kLines) == buf.size());
    assert(buf.lineEnd(kLines) == buf.size());

    Theme theme;
    TextView view(buf, theme, false);
    view.layout({0, 0, 80, 24});

    const std::size_t targetLine = kLines - 5;
    const std::size_t targetStart = buf.lineOffset(targetLine);

    view.moveCursorToOffset(targetStart);
    assert(view.cursorRow() == targetLine);
    assert(view.cursorCol() == 0);

    const std::size_t midOffset = targetStart + kWidth / 2;
    view.moveCursorToOffset(midOffset);
    assert(view.cursorRow() == targetLine);
    assert(view.cursorCol() == kWidth / 2);

    view.moveCursorToOffset(targetStart + kWidth);
    assert(view.cursorRow() == targetLine + 1);
    assert(view.cursorCol() == 0);

    view.moveCursorToOffset(buf.size());
    assert(view.cursorRow() == kLines - 1);
    assert(view.cursorCol() == kWidth);

    return 0;
}
