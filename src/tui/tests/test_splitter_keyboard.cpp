//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_splitter_keyboard.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/widgets/splitter.hpp"

#include "tests/TestHarness.hpp"
#include <memory>

using zanna::tui::term::KeyEvent;
using zanna::tui::ui::Event;
using zanna::tui::widgets::HSplitter;
using zanna::tui::widgets::VSplitter;

struct StubWidget : zanna::tui::ui::Widget {
    void layout(const zanna::tui::ui::Rect &r) override {
        zanna::tui::ui::Widget::layout(r);
        last = r;
    }

    void paint(zanna::tui::render::ScreenBuffer &) override {}

    zanna::tui::ui::Rect last{};
};

TEST(TUI, SplitterKeyboard) {
    // Horizontal splitter ratio adjustments
    auto left = std::make_unique<StubWidget>();
    auto right = std::make_unique<StubWidget>();
    auto *lp = left.get();
    auto *rp = right.get();
    HSplitter hs(std::move(left), std::move(right), 0.5F);
    hs.layout({0, 0, 100, 10});

    KeyEvent k{};
    k.mods = KeyEvent::Ctrl;
    k.code = KeyEvent::Code::Left;
    ASSERT_TRUE(hs.onEvent(Event{k}));
    ASSERT_EQ(lp->last.w, 45);
    ASSERT_EQ(rp->last.w, 55);

    for (int i = 0; i < 20; ++i)
        hs.onEvent(Event{k});
    ASSERT_EQ(lp->last.w, 5);

    k.code = KeyEvent::Code::Right;
    ASSERT_TRUE(hs.onEvent(Event{k}));
    ASSERT_EQ(lp->last.w, 10);

    // Vertical splitter ratio adjustments
    auto top = std::make_unique<StubWidget>();
    auto bottom = std::make_unique<StubWidget>();
    auto *tp = top.get();
    auto *bp = bottom.get();
    VSplitter vs(std::move(top), std::move(bottom), 0.5F);
    vs.layout({0, 0, 10, 100});

    k.code = KeyEvent::Code::Up;
    ASSERT_TRUE(vs.onEvent(Event{k}));
    ASSERT_EQ(tp->last.h, 45);
    ASSERT_EQ(bp->last.h, 55);

    k.code = KeyEvent::Code::Down;
    for (int i = 0; i < 20; ++i)
        vs.onEvent(Event{k});
    ASSERT_EQ(tp->last.h, 95);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
