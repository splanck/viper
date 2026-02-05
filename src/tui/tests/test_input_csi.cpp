//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_input_csi.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/term/input.hpp"

#include "tests/TestHarness.hpp"

using viper::tui::term::InputDecoder;
using viper::tui::term::KeyEvent;

TEST(TUI, InputCsi)
{
    InputDecoder d;

    d.feed("\x1b[A");
    auto ev = d.drain();
    ASSERT_EQ(ev.size(), 1);
    ASSERT_EQ(ev[0].code, KeyEvent::Code::Up);
    ASSERT_EQ(ev[0].mods, 0);

    d.feed("\x1b[1;5C");
    ev = d.drain();
    ASSERT_EQ(ev.size(), 1);
    ASSERT_EQ(ev[0].code, KeyEvent::Code::Right);
    ASSERT_EQ(ev[0].mods, KeyEvent::Ctrl);

    d.feed("\x1b[3~");
    ev = d.drain();
    ASSERT_EQ(ev.size(), 1);
    ASSERT_EQ(ev[0].code, KeyEvent::Code::Delete);

    d.feed("\x1bOP");
    ev = d.drain();
    ASSERT_EQ(ev.size(), 1);
    ASSERT_EQ(ev[0].code, KeyEvent::Code::F1);

    d.feed("\x1b[15~");
    ev = d.drain();
    ASSERT_EQ(ev.size(), 1);
    ASSERT_EQ(ev[0].code, KeyEvent::Code::F5);

    d.feed("\x1b[1;2H");
    ev = d.drain();
    ASSERT_EQ(ev.size(), 1);
    ASSERT_EQ(ev[0].code, KeyEvent::Code::Home);
    ASSERT_EQ(ev[0].mods, KeyEvent::Shift);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
