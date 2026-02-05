//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_clipboard.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/term/clipboard.hpp"
#include "tui/term/term_io.hpp"

#include "tests/TestHarness.hpp"
#include <cstdlib>

static void clear_disable()
{
#if defined(_WIN32)
    _putenv_s("VIPERTUI_DISABLE_OSC52", "");
#else
    unsetenv("VIPERTUI_DISABLE_OSC52");
#endif
}

static void set_disable()
{
#if defined(_WIN32)
    _putenv_s("VIPERTUI_DISABLE_OSC52", "1");
#else
    setenv("VIPERTUI_DISABLE_OSC52", "1", 1);
#endif
}

TEST(TUI, Clipboard)
{
    clear_disable();
    viper::tui::term::StringTermIO tio;
    viper::tui::term::Osc52Clipboard cb(tio);
    [[maybe_unused]] bool ok = cb.copy("hello");
    ASSERT_TRUE(ok);
    ASSERT_EQ(tio.buffer(), "\x1b]52;c;aGVsbG8=\x07");

    set_disable();
    ok = cb.copy("world");
    ASSERT_FALSE(ok);
    ASSERT_EQ(tio.buffer(), "\x1b]52;c;aGVsbG8=\x07");

    clear_disable();
    viper::tui::term::MockClipboard mock;
    ok = mock.copy("test");
    ASSERT_TRUE(ok);
    ASSERT_EQ(mock.last(), "\x1b]52;c;dGVzdA==\x07");
    ASSERT_EQ(mock.paste(), "test");

    set_disable();
    ok = mock.copy("again");
    ASSERT_FALSE(ok);
    ASSERT_TRUE(mock.last().empty());
    ASSERT_TRUE(mock.paste().empty());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
