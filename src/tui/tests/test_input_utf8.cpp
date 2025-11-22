//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_input_utf8.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/term/input.hpp"

#include <cassert>

using viper::tui::term::InputDecoder;
using viper::tui::term::KeyEvent;

int main()
{
    InputDecoder d;

    d.feed("A");
    auto ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].codepoint == 'A');
    assert(ev[0].code == KeyEvent::Code::Unknown);

    d.feed("\xC3\xA9");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].codepoint == 0x00E9);

    d.feed("\xE4\xBD");
    ev = d.drain();
    assert(ev.empty());
    d.feed("\xA0");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].codepoint == 0x4F60);

    d.feed("\n\t\x1b\x7f\r");
    ev = d.drain();
    assert(ev.size() == 5);
    assert(ev[0].code == KeyEvent::Code::Enter);
    assert(ev[1].code == KeyEvent::Code::Tab);
    assert(ev[2].code == KeyEvent::Code::Esc);
    assert(ev[3].code == KeyEvent::Code::Backspace);
    assert(ev[4].code == KeyEvent::Code::Enter);

    d.feed("\xC0\xAF");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].code == KeyEvent::Code::Unknown);
    assert(ev[0].codepoint == 0);

    d.feed("\xED\xA0\x80");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].code == KeyEvent::Code::Unknown);
    assert(ev[0].codepoint == 0);

    d.feed("\xF4\x90\x80\x80");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].code == KeyEvent::Code::Unknown);
    assert(ev[0].codepoint == 0);

    d.feed("C");
    ev = d.drain();
    assert(ev.size() == 1);
    assert(ev[0].codepoint == 'C');

    return 0;
}
