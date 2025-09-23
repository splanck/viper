// tui/include/tui/text/text_buffer.hpp
// @brief Piece-table-backed text buffer composing line index and edit history.
// @invariant Byte offsets remain stable across helpers; helpers receive change callbacks.
// @ownership TextBuffer owns storage helpers and returns copies for callers.
#pragma once

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
        LineView(const PieceTable &table, std::size_t offset, std::size_t length);

        /// @brief Starting byte offset of the line.
        [[nodiscard]] std::size_t offset() const;

        /// @brief Length in bytes excluding trailing newline.
        [[nodiscard]] std::size_t length() const;

        /// @brief Iterate contiguous string_view segments composing the line.
        template <typename Fn>
        void forEachSegment(Fn &&fn) const
        {
            table_.forEachSegment(offset_, length_, std::forward<Fn>(fn));
        }

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
    template <typename Fn>
    void forEachLine(Fn &&fn) const
    {
        const std::size_t lines = line_index_.count();
        for (std::size_t line = 0; line < lines; ++line)
        {
            LineView view(table_, lineOffset(line), lineLength(line));
            if (!std::invoke(fn, line, view))
            {
                break;
            }
        }
    }

    /// @brief Retrieve starting offset for a line, clamped to buffer end.
    [[nodiscard]] std::size_t lineOffset(std::size_t lineNo) const;

    /// @brief Retrieve byte length of a line excluding trailing newline.
    [[nodiscard]] std::size_t lineLength(std::size_t lineNo) const;

    /// @brief Retrieve line metadata and segment iterator for a line.
    [[nodiscard]] LineView lineView(std::size_t lineNo) const;

    /// @brief Number of indexed lines.
    [[nodiscard]] std::size_t lineCount() const;

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
