// tui/tests/test_focus.cpp
// @brief Verify key routing to focused widget and focus cycling with Tab.
// @invariant Enter toggles only currently focused widget; Tab cycles focus order.
// @ownership Test owns widgets, app, and TermIO.

#include "tui/app.hpp"
#include "tui/term/term_io.hpp"
#include "tui/ui/container.hpp"
#include "tui/ui/widget.hpp"

#include <cassert>
#include <memory>

using tui::term::StringTermIO;
using viper::tui::App;
using viper::tui::ui::Event;
using viper::tui::ui::Key;
using viper::tui::ui::KeyEvent;
using viper::tui::ui::VStack;
using viper::tui::ui::Widget;

struct ToggleWidget : Widget
{
    bool state{false};

    bool wantsFocus() const override
    {
        return true;
    }

    bool onEvent(const Event &ev) override
    {
        if (ev.key.key == Key::Enter)
        {
            state = !state;
            return true;
        }
        return false;
    }
};

int main()
{
    auto root = std::make_unique<VStack>();
    auto a = std::make_unique<ToggleWidget>();
    auto b = std::make_unique<ToggleWidget>();
    ToggleWidget *ap = a.get();
    ToggleWidget *bp = b.get();
    root->addChild(std::move(a));
    root->addChild(std::move(b));

    StringTermIO tio;
    App app(std::move(root), tio, 2, 2);
    app.focus().registerWidget(ap);
    app.focus().registerWidget(bp);

    // Initial focus on first widget
    app.pushEvent(Event{KeyEvent{Key::Enter, false}});
    app.tick();
    assert(ap->state);
    assert(!bp->state);

    // Tab moves focus to second widget, Enter toggles second
    app.pushEvent(Event{KeyEvent{Key::Tab, false}});
    app.pushEvent(Event{KeyEvent{Key::Enter, false}});
    app.tick();
    assert(bp->state);
    assert(ap->state);

    return 0;
}
