// tui/include/tui/text/PieceTable.hpp
// @brief Piece table storage for text buffers emitting span change callbacks.
// @invariant Edits only mutate piece metadata while buffers remain stable.
// @ownership PieceTable owns original/add buffers and change payload copies.
#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <string_view>

namespace viper::tui::text
{
/// @brief Piece table implementation powering TextBuffer storage.
class PieceTable
{
  public:
    /// @brief Change payload returned from mutating operations.
    struct Change
    {
        /// @brief Callback signature receiving span position and text view.
        using Callback = std::function<void(std::size_t pos, std::string_view text)>;

        /// @brief Record inserted span metadata and payload.
        void recordInsert(std::size_t pos, std::string text);

        /// @brief Record erased span metadata and payload.
        void recordErase(std::size_t pos, std::string text);

        /// @brief Notify listener about inserted span, if any.
        void notifyInsert(const Callback &cb) const;

        /// @brief Notify listener about erased span, if any.
        void notifyErase(const Callback &cb) const;

        /// @brief True if an insert span is present.
        [[nodiscard]] bool hasInsert() const;

        /// @brief True if an erase span is present.
        [[nodiscard]] bool hasErase() const;

        /// @brief Position of inserted span (undefined if !hasInsert()).
        [[nodiscard]] std::size_t insertPos() const;

        /// @brief Position of erased span (undefined if !hasErase()).
        [[nodiscard]] std::size_t erasePos() const;

        /// @brief Inserted text view (empty if !hasInsert()).
        [[nodiscard]] std::string_view insertedText() const;

        /// @brief Erased text view (empty if !hasErase()).
        [[nodiscard]] std::string_view erasedText() const;

      private:
        struct Span
        {
            std::size_t pos{};
            std::string text{};
        };

        std::optional<Span> insert_span_{};
        std::optional<Span> erase_span_{};
    };

    /// @brief Load fresh content, replacing current buffers.
    Change load(std::string text);

    /// @brief Current byte size.
    [[nodiscard]] std::size_t size() const;

    /// @brief Extract text within [pos, pos + len).
    [[nodiscard]] std::string getText(std::size_t pos, std::size_t len) const;

    /// @brief Insert text at position returning span callbacks.
    Change insertInternal(std::size_t pos, std::string_view text);

    /// @brief Erase length bytes at position returning span callbacks.
    Change eraseInternal(std::size_t pos, std::size_t len);

  private:
    enum class BufferKind
    {
        Original,
        Add
    };

    struct Piece
    {
        BufferKind buf{};
        std::size_t start{};
        std::size_t length{};
    };

    std::list<Piece>::iterator findPiece(std::size_t pos, std::size_t &offset);
    std::list<Piece>::const_iterator findPiece(std::size_t pos, std::size_t &offset) const;

    std::list<Piece> pieces_{};
    std::string original_{};
    std::string add_{};
    std::size_t size_{};
};
} // namespace viper::tui::text
