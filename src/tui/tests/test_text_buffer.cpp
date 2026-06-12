//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tui/tests/test_text_buffer.cpp
// Purpose: Characterization coverage for the shared TUI text buffer and its C
//          ABI facade.
// Key invariants: Piece-table edits preserve line-index metadata, transaction
//                 undo/redo semantics, and embedded NUL bytes exposed through
//                 the shared C ABI.
// Ownership/Lifetime: Test-owned C ABI buffers and duplicated strings are
//                     released through viper_text_buffer_free and
//                     viper_text_buffer_free_string.
// Links: src/tui/include/tui/text/text_buffer.hpp,
//        src/common/text/viper_text_buffer.h
//
//===----------------------------------------------------------------------===//

#include "tui/text/text_buffer.hpp"
#include "viper_text_buffer.h"

#include "tests/TestHarness.hpp"

#include <cstring>
#include <string>
#include <string_view>

using viper::tui::text::TextBuffer;

TEST(TUI, TextBuffer) {
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
    buf.forEachLine([&](std::size_t lineNo, const TextBuffer::LineView &view) {
        std::string reconstructed;
        std::size_t segments = 0;
        view.forEachSegment([&](std::string_view segment) {
            reconstructed.append(segment);
            ++segments;
            return true;
        });

        if (lineNo == 0) {
            ASSERT_EQ(reconstructed, "bye, there");
            ASSERT_TRUE(segments >= 2);
        } else if (lineNo == 1) {
            ASSERT_EQ(reconstructed, "beautiful");
        } else if (lineNo == 2) {
            ASSERT_EQ(reconstructed, "world");
        }

        ++visited;
        return true;
    });
    ASSERT_EQ(visited, buf.lineCount());

    visited = 0;
    buf.forEachLine([&](std::size_t lineNo, const TextBuffer::LineView &) {
        ++visited;
        return lineNo < 1;
    });
    ASSERT_EQ(visited, 2);

    std::size_t segmentVisits = 0;
    buf.lineView(0).forEachSegment([&](std::string_view) {
        ++segmentVisits;
        return false;
    });
    ASSERT_EQ(segmentVisits, 1);
}

TEST(TUI, SharedTextBufferCAbi) {
    viper_text_buffer_t *buf = viper_text_buffer_new();
    ASSERT_TRUE(buf != nullptr);

    const char seed[] = {'a', '\0', 'b', '\n', 'c'};
    ASSERT_TRUE(viper_text_buffer_load_bytes(buf, seed, sizeof(seed)));
    ASSERT_EQ(viper_text_buffer_size(buf), sizeof(seed));
    ASSERT_EQ(viper_text_buffer_line_count(buf), 2);
    ASSERT_EQ(viper_text_buffer_line_start(buf, 1), 4);
    ASSERT_EQ(viper_text_buffer_line_length(buf, 0), 3);

    size_t len = 0;
    char *text = viper_text_buffer_text_dup(buf, &len);
    ASSERT_TRUE(text != nullptr);
    ASSERT_EQ(len, sizeof(seed));
    ASSERT_EQ(std::memcmp(text, seed, sizeof(seed)), 0);
    viper_text_buffer_free_string(text);

    viper_text_buffer_begin_transaction(buf);
    ASSERT_TRUE(viper_text_buffer_insert_bytes(buf, 1, "XX", 2));
    ASSERT_TRUE(viper_text_buffer_erase(buf, 3, 1));
    viper_text_buffer_end_transaction(buf);

    text = viper_text_buffer_text_dup(buf, &len);
    ASSERT_TRUE(text != nullptr);
    ASSERT_EQ(std::string(text, len), std::string("aXXb\nc", 6));
    viper_text_buffer_free_string(text);

    ASSERT_TRUE(viper_text_buffer_undo(buf));
    text = viper_text_buffer_text_dup(buf, &len);
    ASSERT_TRUE(text != nullptr);
    ASSERT_EQ(len, sizeof(seed));
    ASSERT_EQ(std::memcmp(text, seed, sizeof(seed)), 0);
    viper_text_buffer_free_string(text);

    ASSERT_TRUE(viper_text_buffer_redo(buf));
    text = viper_text_buffer_text_dup(buf, &len);
    ASSERT_TRUE(text != nullptr);
    ASSERT_EQ(std::string(text, len), std::string("aXXb\nc", 6));
    viper_text_buffer_free_string(text);

    char *line = viper_text_buffer_line_dup(buf, 1, &len);
    ASSERT_TRUE(line != nullptr);
    ASSERT_EQ(std::string(line, len), "c");
    viper_text_buffer_free_string(line);

    viper_text_buffer_free(buf);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
