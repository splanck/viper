//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_screen_diff.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/render/screen.hpp"

#include "tests/TestHarness.hpp"
#include <vector>

using viper::tui::render::ScreenBuffer;
using viper::tui::render::Style;

TEST(TUI, ScreenDiff)
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
    ASSERT_EQ(spans.size(), 2);
    ASSERT_TRUE(spans[0].row == 0 && spans[0].x0 == 0 && spans[0].x1 == 5);
    ASSERT_TRUE(spans[1].row == 1 && spans[1].x0 == 0 && spans[1].x1 == 5);

    sb.snapshotPrev();
    spans.clear();
    sb.computeDiff(spans);
    ASSERT_TRUE(spans.empty());

    sb.at(0, 1).ch = U'a';
    sb.at(1, 0).ch = U'W';
    sb.at(1, 3).ch = U'L';

    sb.computeDiff(spans);
    ASSERT_EQ(spans.size(), 3);
    ASSERT_TRUE(spans[0].row == 0 && spans[0].x0 == 1 && spans[0].x1 == 2);
    ASSERT_TRUE(spans[1].row == 1 && spans[1].x0 == 0 && spans[1].x1 == 1);
    ASSERT_TRUE(spans[2].row == 1 && spans[2].x0 == 3 && spans[2].x1 == 4);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
