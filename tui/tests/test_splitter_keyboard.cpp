// tui/tests/test_splitter_keyboard.cpp
// @brief Verify splitters adjust ratio via Ctrl+Arrow keys.
// @invariant Ratios clamp to bounds and trigger layout updates.
// @ownership Splitters own stub child widgets used for measurement.

#include "tui/widgets/splitter.hpp"

#include <cassert>
#include <memory>

using viper::tui::term::KeyEvent;
using viper::tui::widgets::HSplitter;
using viper::tui::widgets::VSplitter;

struct StubWidget : viper::tui::ui::Widget
{
    void layout(const viper::tui::ui::Rect &r) override
    {
        viper::tui::ui::Widget::layout(r);
        last = r;
    }

    void paint(viper::tui::render::ScreenBuffer &) override {}

    viper::tui::ui::Rect last{};
};

int main()
{
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
    assert(hs.onEvent(k));
    assert(lp->last.w == 45);
    assert(rp->last.w == 55);

    for (int i = 0; i < 20; ++i)
        hs.onEvent(k);
    assert(lp->last.w == 5);

    k.code = KeyEvent::Code::Right;
    assert(hs.onEvent(k));
    assert(lp->last.w == 10);

    // Vertical splitter ratio adjustments
    auto top = std::make_unique<StubWidget>();
    auto bottom = std::make_unique<StubWidget>();
    auto *tp = top.get();
    auto *bp = bottom.get();
    VSplitter vs(std::move(top), std::move(bottom), 0.5F);
    vs.layout({0, 0, 10, 100});

    k.code = KeyEvent::Code::Up;
    assert(vs.onEvent(k));
    assert(tp->last.h == 45);
    assert(bp->last.h == 55);

    k.code = KeyEvent::Code::Down;
    for (int i = 0; i < 20; ++i)
        vs.onEvent(k);
    assert(tp->last.h == 95);

    return 0;
}
