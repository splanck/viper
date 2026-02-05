//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_split_status.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/widgets/splitter.hpp"
#include "tui/widgets/status_bar.hpp"

#include "tests/TestHarness.hpp"

#include <memory>

using viper::tui::render::ScreenBuffer;
using viper::tui::style::Role;
using viper::tui::style::Theme;
using viper::tui::ui::Rect;
using viper::tui::ui::Widget;
using viper::tui::widgets::HSplitter;
using viper::tui::widgets::StatusBar;
using viper::tui::widgets::VSplitter;

struct Dummy : Widget
{
    void paint(ScreenBuffer &) override {}
};

TEST(TUI, SplitStatus)
{
    Theme theme;

    // HSplitter layout
    auto left = std::make_unique<Dummy>();
    auto right = std::make_unique<Dummy>();
    Dummy *lp = left.get();
    Dummy *rp = right.get();
    HSplitter hs(std::move(left), std::move(right), 0.5F);
    hs.layout({0, 0, 10, 4});
    ASSERT_EQ(lp->rect().w, 5);
    ASSERT_EQ(rp->rect().x, 5);
    hs.layout({0, 0, 8, 4});
    ASSERT_EQ(lp->rect().w, 4);
    ASSERT_EQ(rp->rect().x, 4);

    // VSplitter layout
    auto top = std::make_unique<Dummy>();
    auto bottom = std::make_unique<Dummy>();
    Dummy *tp = top.get();
    Dummy *bp = bottom.get();
    VSplitter vs(std::move(top), std::move(bottom), 0.25F);
    vs.layout({0, 0, 6, 8});
    ASSERT_EQ(tp->rect().h, 2);
    ASSERT_EQ(bp->rect().y, 2);

    // StatusBar paint
    StatusBar bar("LEFT", "RIGHT", theme);
    bar.layout({0, 0, 10, 3});
    ScreenBuffer sb;
    sb.resize(3, 10);
    sb.clear(theme.style(Role::Normal));
    bar.paint(sb);
    int y = 2; // bottom line
    ASSERT_EQ(sb.at(y, 0).ch, U'L');
    ASSERT_EQ(sb.at(y, 9).ch, U'T');
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
