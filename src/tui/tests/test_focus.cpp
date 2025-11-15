// tui/tests/test_focus.cpp
// @brief Verify focus cycling and key routing between widgets.
// @invariant Tab and Shift-Tab change focused widget; Enter dispatches to current.
// @ownership Test owns widgets, app, and TermIO.

#include "tui/app.hpp"
#include "tui/term/input.hpp"
#include "tui/term/term_io.hpp"
#include "tui/ui/container.hpp"

#include <cassert>
#include <memory>

using viper::tui::App;
using viper::tui::term::KeyEvent;
using viper::tui::term::StringTermIO;
using viper::tui::ui::Event;
using viper::tui::ui::VStack;
using viper::tui::ui::Widget;

struct FocusWidget : Widget
{
    bool flag{false};

    bool wantsFocus() const override
    {
        return true;
    }

    bool onEvent(const Event &ev) override
    {
        if (ev.key.code == KeyEvent::Code::Enter)
        {
            flag = !flag;
            return true;
        }
        return false;
    }
};

int main()
{
    auto root = std::make_unique<VStack>();
    auto w1 = std::make_unique<FocusWidget>();
    auto w2 = std::make_unique<FocusWidget>();
    FocusWidget *p1 = w1.get();
    FocusWidget *p2 = w2.get();
    root->addChild(std::move(w1));
    root->addChild(std::move(w2));
    StringTermIO tio;
    App app(std::move(root), tio, 1, 1);
    app.focus().registerWidget(p1);
    app.focus().registerWidget(p2);

    KeyEvent enter{};
    enter.code = KeyEvent::Code::Enter;
    app.pushEvent({enter});
    app.tick();
    assert(p1->flag);
    assert(!p2->flag);

    KeyEvent tab{};
    tab.code = KeyEvent::Code::Tab;
    app.pushEvent({tab});
    app.tick();

    app.pushEvent({enter});
    app.tick();
    assert(p2->flag);

    KeyEvent shiftTab{};
    shiftTab.code = KeyEvent::Code::Tab;
    shiftTab.mods = KeyEvent::Shift;
    app.pushEvent({shiftTab});
    app.tick();

    app.pushEvent({enter});
    app.tick();
    assert(!p1->flag);
    return 0;
}
