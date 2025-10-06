// tui/tests/test_text_buffer.cpp
// @brief Tests for piece table TextBuffer operations.
// @invariant Undo/redo restores buffer content and line index.
// @ownership Test owns buffer instance and verifies returned strings.

#include "tui/text/text_buffer.hpp"

#include <cassert>
#include <string>
#include <vector>

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

    {
        auto view = buf.lineView(0);
        std::vector<std::string> segments;
        view.forEachSegment([&](std::string_view segment) {
            segments.emplace_back(segment);
            return true;
        });
        assert(segments.size() == 1);
        assert(segments.front() == "hello");

        std::size_t calls = 0;
        view.forEachSegment([&](std::string_view) {
            ++calls;
            return false;
        });
        assert(calls == 1);
    }

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

    {
        auto view = buf.lineView(0);
        std::vector<std::string> segments;
        view.forEachSegment([&](std::string_view segment) {
            segments.emplace_back(segment);
            return true;
        });
        assert(segments.size() == 2);
        assert(segments[0] == "hello");
        assert(segments[1] == ", there");
    }

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
