// tui/tests/test_screen_diff.cpp
// @brief Tests diff computation for ScreenBuffer.
// @invariant computeDiff returns minimal spans of changed cells.
// @ownership ScreenBuffer owns all buffers; tests take no ownership.

#include "tui/render/screen.hpp"

#include <cassert>
#include <vector>

using viper::tui::render::ScreenBuffer;
using viper::tui::render::Style;

int main()
{
    ScreenBuffer sb;
    sb.resize(2, 5);
    Style style{};
    sb.clear(style);
    sb.snapshotPrev();

    const char *line0 = "hello";
    const char *line1 = "world";
    for (int i = 0; i < 5; ++i)
    {
        sb.at(0, i).ch = static_cast<char32_t>(line0[i]);
        sb.at(1, i).ch = static_cast<char32_t>(line1[i]);
    }

    std::vector<ScreenBuffer::DiffSpan> spans;
    sb.computeDiff(spans);
    assert(spans.size() == 2);
    assert(spans[0].row == 0 && spans[0].x0 == 0 && spans[0].x1 == 5);
    assert(spans[1].row == 1 && spans[1].x0 == 0 && spans[1].x1 == 5);

    sb.snapshotPrev();
    spans.clear();
    sb.computeDiff(spans);
    assert(spans.empty());

    sb.at(0, 1).ch = U'a';
    sb.at(1, 0).ch = U'W';
    sb.at(1, 3).ch = U'L';

    sb.computeDiff(spans);
    assert(spans.size() == 3);
    assert(spans[0].row == 0 && spans[0].x0 == 1 && spans[0].x1 == 2);
    assert(spans[1].row == 1 && spans[1].x0 == 0 && spans[1].x1 == 1);
    assert(spans[2].row == 1 && spans[2].x0 == 3 && spans[2].x1 == 4);

    return 0;
}
