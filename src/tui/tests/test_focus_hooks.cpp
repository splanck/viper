// tui/tests/test_focus_hooks.cpp
// @brief Verify focus change callbacks and unregister behavior.
// @invariant FocusManager notifies widgets on focus transitions and removal.
// @ownership Test owns widgets and FocusManager.

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
