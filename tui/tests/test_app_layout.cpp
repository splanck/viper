// tui/tests/test_app_layout.cpp
// @brief Verify App layouts and paints stacked widgets correctly.
// @invariant Top widget occupies first row, bottom widget second row.
// @ownership Test owns widgets, app, and TermIO.

#include "tui/app.hpp"
#include "tui/render/screen.hpp"
#include "tui/term/term_io.hpp"
#include "tui/ui/container.hpp"

#include <cassert>
#include <memory>
#include <string>

using viper::tui::App;
using viper::tui::render::ScreenBuffer;
using viper::tui::term::StringTermIO;
using viper::tui::ui::VStack;
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
    auto root = std::make_unique<VStack>();
    auto a = std::make_unique<CharWidget>('A');
    auto b = std::make_unique<CharWidget>('B');
    CharWidget *ap = a.get();
    CharWidget *bp = b.get();
    root->addChild(std::move(a));
    root->addChild(std::move(b));
    StringTermIO tio;
    App app(std::move(root), tio, 2, 2);
    app.tick();
    assert(ap->rect().h == 1);
    assert(bp->rect().y == 1);
    const std::string &out = tio.buffer();
    assert(out.find('A') != std::string::npos);
    assert(out.find('B') != std::string::npos);
    return 0;
}
