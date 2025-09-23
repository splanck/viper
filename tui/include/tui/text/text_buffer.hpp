// tui/include/tui/text/text_buffer.hpp
// @brief Piece-table-backed text buffer composing line index and edit history.
// @invariant Byte offsets remain stable across helpers; helpers receive change callbacks.
// @ownership TextBuffer owns storage helpers and returns copies for callers.
#pragma once

#include "tui/text/EditHistory.hpp"
#include "tui/text/LineIndex.hpp"
#include "tui/text/PieceTable.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace viper::tui::text
{
/// @brief Text buffer orchestrating piece table, line index, and history.
class TextBuffer
{
  public:
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
