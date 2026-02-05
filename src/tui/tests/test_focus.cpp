//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_focus.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/app.hpp"
#include "tui/term/input.hpp"
#include "tui/term/term_io.hpp"
#include "tui/ui/container.hpp"

#include "tests/TestHarness.hpp"
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

TEST(TUI, Focus)
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
    ASSERT_TRUE(p1->flag);
    ASSERT_FALSE(p2->flag);

    KeyEvent tab{};
    tab.code = KeyEvent::Code::Tab;
    app.pushEvent({tab});
    app.tick();

    app.pushEvent({enter});
    app.tick();
    ASSERT_TRUE(p2->flag);

    KeyEvent shiftTab{};
    shiftTab.code = KeyEvent::Code::Tab;
    shiftTab.mods = KeyEvent::Shift;
    app.pushEvent({shiftTab});
    app.tick();

    app.pushEvent({enter});
    app.tick();
    ASSERT_FALSE(p1->flag);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
