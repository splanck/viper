// tui/include/tui/text/text_buffer.hpp
// @brief Piece table based text buffer with grouped undo/redo.
// @invariant Positions are byte offsets; inserts append to add buffer without copying existing
// text.
// @ownership TextBuffer owns original and add buffers, callers own returned strings.
#pragma once

#include <list>
#include <string>
#include <string_view>
#include <vector>

namespace viper::tui::text
{
/// @brief Text buffer using a piece table.
class TextBuffer
{
  public:
    /// @brief Load initial content, replacing current buffer.
    void load(std::string text);

    /// @brief Insert text at byte position.
    void insert(size_t pos, std::string_view text);

    /// @brief Erase len bytes starting at pos.
    void erase(size_t pos, size_t len);

    /// @brief Begin a transaction grouping subsequent edits.
    void beginTxn();

    /// @brief End current transaction and record for undo.
    void endTxn();

    /// @brief Undo last transaction.
    [[nodiscard]] bool undo();

    /// @brief Redo last undone transaction.
    [[nodiscard]] bool redo();

    /// @brief Get line content without trailing newline.
    [[nodiscard]] std::string getLine(size_t lineNo) const;

    /// @brief Get full buffer content.
    [[nodiscard]] std::string str() const;

    /// @brief Total byte length.
    [[nodiscard]] size_t size() const
    {
        return size_;
    }

  private:
    enum class BufferKind
    {
        Original,
        Add
    };

    struct Piece
    {
        BufferKind buf;
        size_t start;
        size_t length;
    };

    enum class OpType
    {
        Insert,
        Erase
    };

    struct Op
    {
        OpType type;
        size_t pos;
        std::string text; // inserted or removed text
    };

    using Txn = std::vector<Op>;

    // buffers and pieces
    std::list<Piece> pieces_{};
    std::string original_{};
    std::string add_{};
    size_t size_{};

    // line index: start offset of each line
    std::vector<size_t> line_starts_{};

    // undo/redo stacks
    std::vector<Txn> undo_stack_{};
    std::vector<Txn> redo_stack_{};
    bool in_txn_{};
    Txn txn_ops_{};

    // helpers
    void insertInternal(size_t pos, std::string_view text);
    void eraseInternal(size_t pos, size_t len);
    std::string getText(size_t pos, size_t len) const;
    std::list<Piece>::iterator findPiece(size_t pos, size_t &offset);
    void updateLinesInsert(size_t pos, std::string_view text);
    void updateLinesErase(size_t pos, size_t len);
};
} // namespace viper::tui::text
