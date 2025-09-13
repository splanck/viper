// tui/tests/test_input_csi.cpp
// @brief Tests decoding of CSI/SS3 sequences for navigation and function keys.
// @invariant Decoder handles escape sequences and modifiers across feeds.
// @ownership InputDecoder owns its event queue only.

#include "tui/term/input.hpp"

#include <cassert>

using viper::tui::term::InputDecoder;
using viper::tui::term::KeyEvent;

int main()
{
    InputDecoder d;

    d.feed("\x1b[A");
    auto ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].code == KeyEvent::Code::Up);
    assert(ev[0].mods == 0);

    d.feed("\x1b[1;5C");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].code == KeyEvent::Code::Right);
    assert(ev[0].mods == KeyEvent::Ctrl);

    d.feed("\x1b[3~");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].code == KeyEvent::Code::Delete);

    d.feed("\x1bOP");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].code == KeyEvent::Code::F1);

    d.feed("\x1b[15~");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].code == KeyEvent::Code::F5);

    d.feed("\x1b[1;2H");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].code == KeyEvent::Code::Home);
    assert(ev[0].mods == KeyEvent::Shift);

    return 0;
}
