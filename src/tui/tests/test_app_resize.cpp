// tui/tests/test_app_resize.cpp
// @brief Verify App::resize updates layout dimensions.
// @invariant Root widget rect matches new size after resize.
// @ownership Test owns widget, app, and TermIO.

#include "tui/app.hpp"
#include "tui/render/screen.hpp"
#include "tui/term/term_io.hpp"
#include "tui/ui/widget.hpp"

#include <cassert>
#include <memory>

using viper::tui::App;
using viper::tui::render::ScreenBuffer;
using viper::tui::term::StringTermIO;
using viper::tui::ui::Widget;

struct CharWidget : Widget
{
    char ch;

    explicit CharWidget(char c) : ch(c) {}

    void paint(ScreenBuffer &sb) override
    {
        auto r = rect();
        for (int y = r.y; y < r.y + r.h; ++y)
        {
            for (int x = r.x; x < r.x + r.w; ++x)
            {
                sb.at(y, x).ch = ch;
            }
        }
    }
};

int main()
{
    auto root = std::make_unique<CharWidget>('X');
    CharWidget *wp = root.get();
    StringTermIO tio;
    App app(std::move(root), tio, 1, 1);
    app.tick();
    assert(wp->rect().h == 1);
    assert(wp->rect().w == 1);
    app.resize(2, 3);
    app.tick();
    assert(wp->rect().h == 2);
    assert(wp->rect().w == 3);
    return 0;
}
