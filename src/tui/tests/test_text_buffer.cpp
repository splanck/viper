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

#include "tests/TestHarness.hpp"

#include <string>
#include <string_view>

using viper::tui::text::TextBuffer;

TEST(TUI, TextBuffer)
{
    TextBuffer buf;
    buf.load("hello\nworld");
    ASSERT_EQ(buf.getLine(0), "hello");
    ASSERT_EQ(buf.getLine(1), "world");
    ASSERT_EQ(buf.lineCount(), 2);
    ASSERT_EQ(buf.lineStart(0), 0);
    ASSERT_EQ(buf.lineEnd(0), buf.lineStart(0) + buf.getLine(0).size());
    ASSERT_EQ(buf.lineStart(1), 6);
    ASSERT_EQ(buf.lineEnd(1), buf.size());
    ASSERT_EQ(buf.lineStart(5), buf.size());
    ASSERT_EQ(buf.lineEnd(5), buf.size());

    buf.insert(5, ", there\nbeautiful");
    ASSERT_EQ(buf.getLine(0), "hello, there");
    ASSERT_EQ(buf.getLine(1), "beautiful");
    ASSERT_EQ(buf.getLine(2), "world");
    ASSERT_EQ(buf.lineCount(), 3);
    ASSERT_EQ(buf.lineEnd(0), buf.lineStart(0) + buf.getLine(0).size());
    ASSERT_EQ(buf.lineEnd(1), buf.lineStart(1) + buf.getLine(1).size());
    ASSERT_EQ(buf.lineEnd(2), buf.lineStart(2) + buf.getLine(2).size());
    ASSERT_EQ(buf.lineStart(99), buf.size());
    ASSERT_EQ(buf.lineEnd(99), buf.size());

    buf.beginTxn();
    buf.erase(0, 5); // remove 'hello'
    buf.insert(0, "bye");
    buf.endTxn();
    ASSERT_EQ(buf.getLine(0), "bye, there");

    bool ok = buf.undo();
    ASSERT_TRUE(ok);
    ASSERT_EQ(buf.getLine(0), "hello, there");

    ok = buf.redo();
    ASSERT_TRUE(ok);
    ASSERT_EQ(buf.getLine(0), "bye, there");

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
                ASSERT_EQ(reconstructed, "bye, there");
                ASSERT_TRUE(segments >= 2);
            }
            else if (lineNo == 1)
            {
                ASSERT_EQ(reconstructed, "beautiful");
            }
            else if (lineNo == 2)
            {
                ASSERT_EQ(reconstructed, "world");
            }

            ++visited;
            return true;
        });
    ASSERT_EQ(visited, buf.lineCount());

    visited = 0;
    buf.forEachLine(
        [&](std::size_t lineNo, const TextBuffer::LineView &)
        {
            ++visited;
            return lineNo < 1;
        });
    ASSERT_EQ(visited, 2);

    std::size_t segmentVisits = 0;
    buf.lineView(0).forEachSegment(
        [&](std::string_view)
        {
            ++segmentVisits;
            return false;
        });
    ASSERT_EQ(segmentVisits, 1);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
