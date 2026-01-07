//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_modal.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/app.hpp"
#include "tui/term/term_io.hpp"
#include "tui/ui/modal.hpp"

#include <cassert>
#include <memory>

using viper::tui::App;
using viper::tui::term::KeyEvent;
using viper::tui::term::StringTermIO;
using viper::tui::ui::Event;
using viper::tui::ui::ModalHost;
using viper::tui::ui::Popup;
using viper::tui::ui::Widget;

struct FlagWidget : Widget
{
    bool flag{false};

    bool onEvent(const Event &ev) override
    {
        if (ev.key.code == KeyEvent::Code::Enter)
        {
            flag = true;
            return true;
        }
        return false;
    }

    bool wantsFocus() const override
    {
        return true;
    }
};

int main()
{
    auto base = std::make_unique<FlagWidget>();
    FlagWidget *ptr = base.get();
    auto host = std::make_unique<ModalHost>(std::move(base));
    ModalHost *hptr = host.get();
    StringTermIO tio;
    App app(std::move(host), tio, 10, 10);
    app.focus().registerWidget(hptr);

    // Ensure base widget receives keys without popup
    Event ev{};
    ev.key.code = KeyEvent::Code::Enter;
    app.pushEvent(ev);
    app.tick();
    assert(ptr->flag);

    // Popup intercepts and dismisses on Enter
    ptr->flag = false;
    hptr->pushModal(std::make_unique<Popup>(4, 3));
    app.pushEvent(ev);
    app.tick();
    assert(!ptr->flag);
    app.pushEvent(ev);
    app.tick();
    assert(ptr->flag);

    // Popup intercepts and dismisses on Esc
    ptr->flag = false;
    hptr->pushModal(std::make_unique<Popup>(4, 3));
    ev.key.code = KeyEvent::Code::Esc;
    app.pushEvent(ev);
    app.tick();
    assert(!ptr->flag);
    ev.key.code = KeyEvent::Code::Enter;
    app.pushEvent(ev);
    app.tick();
    assert(ptr->flag);

    return 0;
}
