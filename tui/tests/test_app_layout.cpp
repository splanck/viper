// tui/tests/test_app_layout.cpp
// @brief Verify App layouts containers and renders painted output.
// @invariant Fake widgets fill their assigned rectangles without overlap.
// @ownership App owns widgets; test observes layout via raw pointers.

#include "tui/app.hpp"
#include "tui/term/term_io.hpp"
#include "tui/ui/container.hpp"

#include <cassert>
#include <memory>
#include <string>

using tui::term::StringTermIO;
using viper::tui::App;
using viper::tui::render::ScreenBuffer;
using viper::tui::ui::Event;
using viper::tui::ui::Rect;
using viper::tui::ui::VStack;
using viper::tui::ui::Widget;

struct FakeWidget : Widget
{
    char ch;
    Rect last{};

    explicit FakeWidget(char c) : ch(c) {}

    void layout(Rect r) override
    {
        last = r;
        Widget::layout(r);
    }

    void paint(ScreenBuffer &sb) override
    {
        for (int y = last.y; y < last.y + last.h; ++y)
            for (int x = last.x; x < last.x + last.w; ++x)
                sb.at(y, x).ch = ch;
    }
};

int main()
{
    StringTermIO tio;
    auto root = std::make_unique<VStack>();
    auto fw1 = std::make_unique<FakeWidget>('X');
    auto fw2 = std::make_unique<FakeWidget>('O');
    FakeWidget *p1 = fw1.get();
    FakeWidget *p2 = fw2.get();
    root->addChild(std::move(fw1));
    root->addChild(std::move(fw2));
    App app(std::move(root), tio);
    app.resize(2, 2);
    app.tick();
    assert(p1->last.x == 0 && p1->last.y == 0 && p1->last.w == 2 && p1->last.h == 1);
    assert(p2->last.x == 0 && p2->last.y == 1 && p2->last.w == 2 && p2->last.h == 1);
    const std::string &out = tio.buffer();
    assert(out.find('X') != std::string::npos);
    assert(out.find('O') != std::string::npos);
    return 0;
}
