//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the TextBuffer class, the central text management
// abstraction for Viper's TUI editor. TextBuffer orchestrates three
// underlying data structures:
//   - PieceTable: efficient insert/erase operations on the text content
//   - LineIndex: tracks line boundaries for fast line-number lookups
//   - EditHistory: supports transactional undo/redo of edit operations
//
// TextBuffer provides a unified API for loading text, performing edits,
// querying line contents, and managing undo/redo transactions. The
// LineView inner class enables zero-copy iteration over line segments
// from the piece table without materializing full line strings.
//
// Key invariants:
//   - Line numbers are 0-based; lineCount() returns at least 1 for
//     an empty buffer (representing one empty line).
//   - Edit operations (insert/erase) automatically update both the
//     piece table and the line index atomically.
//   - Undo/redo replay edits in reverse/forward order via the history.
//
// Ownership: TextBuffer owns its PieceTable, LineIndex, and EditHistory
// by value. External references (e.g., from TextView) must not outlive
// the buffer.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tui/support/function_ref.hpp"
#include "tui/text/EditHistory.hpp"
#include "tui/text/LineIndex.hpp"
#include "tui/text/PieceTable.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace viper::tui::text
{
/// @brief High-level text buffer orchestrating piece table storage, line indexing,
///        and undo/redo history for the TUI text editor.
/// @details Provides the primary editing API used by views and widgets. Edits are
///          recorded into transactions that can be undone and redone. The buffer
///          maintains a line index that is incrementally updated on each edit,
///          enabling efficient line-based access without scanning the entire text.
class TextBuffer
{
  public:
    /// @brief Lightweight, non-owning view over a single line in the piece table.
    /// @details Provides segment-based iteration for rendering without copying the
    ///          entire line into a contiguous string. This is critical for performance
    ///          when rendering large files where lines span multiple piece table pieces.
    class LineView
    {
      public:
        /// @brief Callback accepting contiguous line segments.
        using SegmentVisitor = tui::FunctionRef<bool(std::string_view)>;

        LineView(const PieceTable &table, std::size_t offset, std::size_t length);

        /// @brief Starting byte offset of the line.
        [[nodiscard]] std::size_t offset() const;

        /// @brief Length in bytes excluding trailing newline.
        [[nodiscard]] std::size_t length() const;

        /// @brief Iterate contiguous string_view segments composing the line.
        void forEachSegment(SegmentVisitor fn) const;

      private:
        const PieceTable &table_;
        std::size_t offset_{};
        std::size_t length_{};
    };

    /// @brief Replace the entire buffer content with new text.
    /// @details Resets the piece table, rebuilds the line index, and clears the
    ///          edit history. Any existing undo/redo state is discarded.
    /// @param text The new content to load into the buffer.
    void load(std::string text);

    /// @brief Insert text at the specified byte position.
    /// @details Inserts into the piece table, updates the line index for any new
    ///          newline characters, and records the operation for undo if a
    ///          transaction is active.
    /// @param pos Byte offset where the text will be inserted (0-based).
    /// @param text The text to insert at the given position.
    void insert(std::size_t pos, std::string_view text);

    /// @brief Erase a range of bytes from the buffer.
    /// @details Removes bytes from the piece table, adjusts the line index for
    ///          any removed newline characters, and records the operation for undo.
    /// @param pos Starting byte offset of the range to erase.
    /// @param len Number of bytes to erase starting from pos.
    void erase(std::size_t pos, std::size_t len);

    /// @brief Begin a transaction grouping subsequent edits for atomic undo.
    /// @details All insert and erase operations performed between beginTxn() and
    ///          endTxn() are grouped into a single transaction. When the user
    ///          invokes undo(), all operations in the transaction are reversed
    ///          atomically. Must be paired with endTxn().
    void beginTxn();

    /// @brief End the current transaction and commit it to the undo stack.
    /// @details Finalizes the current transaction. If it contains operations, the
    ///          transaction is pushed onto the undo stack. Empty transactions are
    ///          silently discarded.
    void endTxn();

    /// @brief Undo the last committed transaction.
    /// @details Replays all operations in the most recent transaction in reverse
    ///          order, moving the transaction to the redo stack.
    /// @return True if a transaction was undone; false if the undo stack was empty.
    [[nodiscard]] bool undo();

    /// @brief Redo the last undone transaction.
    /// @details Replays all operations in the most recently undone transaction in
    ///          forward order, moving it back to the undo stack.
    /// @return True if a transaction was redone; false if the redo stack was empty.
    [[nodiscard]] bool redo();

    /// @brief Get line content without trailing newline.
    /// @details Extracts the text of the specified line by looking up start/end
    ///          offsets in the line index and reading from the piece table.
    ///          If lineNo is out of range (>= lineCount()), returns an empty string.
    /// @param lineNo Zero-based line number.
    /// @return The line text as a contiguous string, excluding trailing newline.
    [[nodiscard]] std::string getLine(std::size_t lineNo) const;

    /// @brief Visit each indexed line with a lightweight view.
    using LineVisitor = tui::FunctionRef<bool(std::size_t, const LineView &)>;
    void forEachLine(LineVisitor fn) const;

    /// @brief Number of indexed lines tracked by the line index.
    [[nodiscard]] std::size_t lineCount() const;

    /// @brief Starting byte offset for a line; returns buffer size when out of range.
    [[nodiscard]] std::size_t lineStart(std::size_t lineNo) const;

    /// @brief Exclusive ending byte offset excluding trailing newline; clamps to buffer size.
    [[nodiscard]] std::size_t lineEnd(std::size_t lineNo) const;

    /// @brief Retrieve starting offset for a line, clamped to buffer end.
    [[nodiscard]] std::size_t lineOffset(std::size_t lineNo) const;

    /// @brief Retrieve byte length of a line excluding trailing newline.
    [[nodiscard]] std::size_t lineLength(std::size_t lineNo) const;

    /// @brief Retrieve line metadata and segment iterator for a line.
    [[nodiscard]] LineView lineView(std::size_t lineNo) const;

    /// @brief Materialize the entire buffer content as a contiguous string.
    /// @details Concatenates all piece table segments into a single string.
    ///          Allocates memory proportional to the buffer size.
    /// @return The full text content of the buffer.
    [[nodiscard]] std::string str() const;

    /// @brief Get the total number of bytes in the buffer.
    /// @return Byte count of all content in the piece table.
    [[nodiscard]] std::size_t size() const;

  private:
    PieceTable table_{};
    LineIndex line_index_{};
    EditHistory history_{};
};
} // namespace viper::tui::text
