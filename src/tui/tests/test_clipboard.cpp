// tui/tests/test_clipboard.cpp
// @brief Tests for OSC 52 clipboard sequences and env guard.
// @invariant MockClipboard captures last sequence; StringTermIO captures writes.
// @ownership MockClipboard owns its buffer.

#include "tui/term/clipboard.hpp"
#include "tui/term/term_io.hpp"

#include <cassert>
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

int main()
{
    clear_disable();
    viper::tui::term::StringTermIO tio;
    viper::tui::term::Osc52Clipboard cb(tio);
    bool ok = cb.copy("hello");
    assert(ok);
    assert(tio.buffer() == "\x1b]52;c;aGVsbG8=\x07");

    set_disable();
    ok = cb.copy("world");
    assert(!ok);
    assert(tio.buffer() == "\x1b]52;c;aGVsbG8=\x07");

    clear_disable();
    viper::tui::term::MockClipboard mock;
    ok = mock.copy("test");
    assert(ok);
    assert(mock.last() == "\x1b]52;c;dGVzdA==\x07");
    assert(mock.paste() == "test");

    set_disable();
    ok = mock.copy("again");
    assert(!ok);
    assert(mock.last().empty());
    assert(mock.paste().empty());
    return 0;
}
