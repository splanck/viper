//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_focus_hooks.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/ui/focus.hpp"
#include "tui/ui/widget.hpp"

#include <cassert>

using viper::tui::ui::FocusManager;
using viper::tui::ui::Widget;

struct HookWidget : Widget
{
    bool focused{false};
    int calls{0};

    bool wantsFocus() const override
    {
        return true;
    }

    void onFocusChanged(bool f) override
    {
        focused = f;
        ++calls;
    }
};

int main()
{
    FocusManager fm;
    HookWidget a{};
    HookWidget b{};
    fm.registerWidget(&a);
    fm.registerWidget(&b);

    (void)fm.next();
    assert(!a.focused);
    assert(b.focused);
    assert(a.calls == 1);
    assert(b.calls == 1);

    (void)fm.prev();
    assert(a.focused);
    assert(!b.focused);
    assert(a.calls == 2);
    assert(b.calls == 2);

    fm.unregisterWidget(&a);
    assert(!a.focused);
    assert(b.focused);
    assert(a.calls == 3);
    assert(b.calls == 3);
    assert(fm.current() == &b);
    return 0;
}
