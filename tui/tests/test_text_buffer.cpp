// tui/tests/test_text_buffer.cpp
// @brief Tests for piece table TextBuffer operations.
// @invariant Undo/redo restores buffer content and line index.
// @ownership Test owns buffer instance and verifies returned strings.

#include "tui/text/text_buffer.hpp"

#include <cassert>
#include <string>

using viper::tui::text::TextBuffer;

int main()
{
    TextBuffer buf;
    buf.load("hello\nworld");
    assert(buf.getLine(0) == "hello");
    assert(buf.getLine(1) == "world");
    assert(buf.lineCount() == 2);
    assert(buf.lineStart(0) == 0);
    assert(buf.lineEnd(0) == buf.lineStart(0) + buf.getLine(0).size());
    assert(buf.lineStart(1) == 6);
    assert(buf.lineEnd(1) == buf.size());
    assert(buf.lineStart(5) == buf.size());
    assert(buf.lineEnd(5) == buf.size());

    buf.insert(5, ", there\nbeautiful");
    assert(buf.getLine(0) == "hello, there");
    assert(buf.getLine(1) == "beautiful");
    assert(buf.getLine(2) == "world");
    assert(buf.lineCount() == 3);
    assert(buf.lineEnd(0) == buf.lineStart(0) + buf.getLine(0).size());
    assert(buf.lineEnd(1) == buf.lineStart(1) + buf.getLine(1).size());
    assert(buf.lineEnd(2) == buf.lineStart(2) + buf.getLine(2).size());
    assert(buf.lineStart(99) == buf.size());
    assert(buf.lineEnd(99) == buf.size());

    buf.beginTxn();
    buf.erase(0, 5); // remove 'hello'
    buf.insert(0, "bye");
    buf.endTxn();
    assert(buf.getLine(0) == "bye, there");

    bool ok = buf.undo();
    assert(ok);
    assert(buf.getLine(0) == "hello, there");

    ok = buf.redo();
    assert(ok);
    assert(buf.getLine(0) == "bye, there");

    return 0;
}
