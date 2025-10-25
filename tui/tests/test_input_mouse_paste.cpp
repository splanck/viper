// tui/tests/test_input_mouse_paste.cpp
// @brief Tests SGR mouse decoding and bracketed paste handling.
// @invariant Decoder handles mouse sequences and paste chunks across feeds.
// @ownership InputDecoder owns its event queues only.

#include "tui/term/input.hpp"

#include <cassert>

using viper::tui::term::InputDecoder;
using viper::tui::term::KeyEvent;
using viper::tui::term::MouseEvent;
using viper::tui::term::PasteEvent;

int main()
{
    InputDecoder d;

    d.feed("\x1b[<0;10;20M");
    auto me = d.drain_mouse();
    assert(me.size() == 1);
    assert(me[0].type == MouseEvent::Type::Down);
    assert(me[0].x == 9 && me[0].y == 19);
    assert(me[0].buttons == 1);

    d.feed("\x1b[<0;10;20m");
    me = d.drain_mouse();
    assert(me.size() == 1);
    assert(me[0].type == MouseEvent::Type::Up);

    d.feed("\x1b[<32;11;21M");
    me = d.drain_mouse();
    assert(me.size() == 1);
    assert(me[0].type == MouseEvent::Type::Move);
    assert(me[0].x == 10 && me[0].y == 20);

    d.feed("\x1b[<64;12;22M");
    me = d.drain_mouse();
    assert(me.size() == 1);
    assert(me[0].type == MouseEvent::Type::Wheel);
    assert(me[0].buttons == 1);

    d.feed("\x1b[200~hello\nworld\x1b[201~");
    auto pe = d.drain_paste();
    assert(pe.size() == 1);
    assert(pe[0].text == "hello\nworld");

    d.feed("\x1b[A");
    auto ke = d.drain();
    assert(ke.size() == 1);
    assert(ke[0].code == KeyEvent::Code::Up);
    me = d.drain_mouse();
    assert(me.empty());

    return 0;
}
