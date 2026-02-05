//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_input_mouse_paste.cpp
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
using viper::tui::term::MouseEvent;
using viper::tui::term::PasteEvent;

TEST(TUI, InputMousePaste)
{
    InputDecoder d;

    d.feed("\x1b[<0;10;20M");
    auto me = d.drain_mouse();
    ASSERT_EQ(me.size(), 1);
    ASSERT_EQ(me[0].type, MouseEvent::Type::Down);
    ASSERT_TRUE(me[0].x == 9 && me[0].y == 19);
    ASSERT_EQ(me[0].buttons, 1);

    d.feed("\x1b[<0;10;20m");
    me = d.drain_mouse();
    ASSERT_EQ(me.size(), 1);
    ASSERT_EQ(me[0].type, MouseEvent::Type::Up);

    d.feed("\x1b[<32;11;21M");
    me = d.drain_mouse();
    ASSERT_EQ(me.size(), 1);
    ASSERT_EQ(me[0].type, MouseEvent::Type::Move);
    ASSERT_TRUE(me[0].x == 10 && me[0].y == 20);

    d.feed("\x1b[<64;12;22M");
    me = d.drain_mouse();
    ASSERT_EQ(me.size(), 1);
    ASSERT_EQ(me[0].type, MouseEvent::Type::Wheel);
    ASSERT_EQ(me[0].buttons, 1);

    d.feed("\x1b[200~hello\nworld\x1b[201~");
    auto pe = d.drain_paste();
    ASSERT_EQ(pe.size(), 1);
    ASSERT_EQ(pe[0].text, "hello\nworld");

    d.feed("\x1b[A");
    auto ke = d.drain();
    ASSERT_EQ(ke.size(), 1);
    ASSERT_EQ(ke[0].code, KeyEvent::Code::Up);
    me = d.drain_mouse();
    ASSERT_TRUE(me.empty());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
