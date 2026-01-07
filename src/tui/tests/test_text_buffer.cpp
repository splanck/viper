//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_text_buffer.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/text/text_buffer.hpp"

#include <cassert>
#include <string>
#include <string_view>

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

    std::size_t visited = 0;
    buf.forEachLine(
        [&](std::size_t lineNo, const TextBuffer::LineView &view)
        {
            std::string reconstructed;
            std::size_t segments = 0;
            view.forEachSegment(
                [&](std::string_view segment)
                {
                    reconstructed.append(segment);
                    ++segments;
                    return true;
                });

            if (lineNo == 0)
            {
                assert(reconstructed == "bye, there");
                assert(segments >= 2);
            }
            else if (lineNo == 1)
            {
                assert(reconstructed == "beautiful");
            }
            else if (lineNo == 2)
            {
                assert(reconstructed == "world");
            }

            ++visited;
            return true;
        });
    assert(visited == buf.lineCount());

    visited = 0;
    buf.forEachLine(
        [&](std::size_t lineNo, const TextBuffer::LineView &)
        {
            ++visited;
            return lineNo < 1;
        });
    assert(visited == 2);

    std::size_t segmentVisits = 0;
    buf.lineView(0).forEachSegment(
        [&](std::string_view)
        {
            ++segmentVisits;
            return false;
        });
    assert(segmentVisits == 1);

    return 0;
}
