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

#include "tests/TestHarness.hpp"

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

TEST(TUI, FocusHooks)
{
    FocusManager fm;
    HookWidget a{};
    HookWidget b{};
    fm.registerWidget(&a);
    fm.registerWidget(&b);

    (void)fm.next();
    ASSERT_FALSE(a.focused);
    ASSERT_TRUE(b.focused);
    ASSERT_EQ(a.calls, 1);
    ASSERT_EQ(b.calls, 1);

    (void)fm.prev();
    ASSERT_TRUE(a.focused);
    ASSERT_FALSE(b.focused);
    ASSERT_EQ(a.calls, 2);
    ASSERT_EQ(b.calls, 2);

    fm.unregisterWidget(&a);
    ASSERT_FALSE(a.focused);
    ASSERT_TRUE(b.focused);
    ASSERT_EQ(a.calls, 3);
    ASSERT_EQ(b.calls, 3);
    ASSERT_EQ(fm.current(), &b);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
