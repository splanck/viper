// tui/include/tui/text/text_buffer.hpp
// @brief Piece-table-backed text buffer composing line index and edit history.
// @invariant Byte offsets remain stable across helpers; helpers receive change callbacks.
// @ownership TextBuffer owns storage helpers and returns copies for callers.
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
/// @brief Text buffer orchestrating piece table, line index, and history.
class TextBuffer
{
  public:
    /// @brief Lightweight view over a single line.
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

    /// @brief Load initial content, replacing current buffer.
    void load(std::string text);

    /// @brief Insert text at byte position.
    void insert(std::size_t pos, std::string_view text);

    /// @brief Erase len bytes starting at pos.
    void erase(std::size_t pos, std::size_t len);

    /// @brief Begin a transaction grouping subsequent edits.
    void beginTxn();

    /// @brief End current transaction and record for undo.
    void endTxn();

    /// @brief Undo last transaction.
    [[nodiscard]] bool undo();

    /// @brief Redo last undone transaction.
    [[nodiscard]] bool redo();

    /// @brief Get line content without trailing newline.
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

    /// @brief Get full buffer content.
    [[nodiscard]] std::string str() const;

    /// @brief Total byte length.
    [[nodiscard]] std::size_t size() const;

  private:
    PieceTable table_{};
    LineIndex line_index_{};
    EditHistory history_{};
};
} // namespace viper::tui::text
