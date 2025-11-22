//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/text/PieceTable.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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

    /// @brief Iterate contiguous segments covering [pos, pos + len).
    template <typename Fn> void forEachSegment(std::size_t pos, std::size_t len, Fn &&fn) const;

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

template <typename Fn>
void viper::tui::text::PieceTable::forEachSegment(std::size_t pos, std::size_t len, Fn &&fn) const
{
    std::size_t idx = 0;
    for (auto it = pieces_.cbegin(); it != pieces_.cend() && len > 0; ++it)
    {
        if (pos >= idx + it->length)
        {
            idx += it->length;
            continue;
        }

        std::size_t start_in_piece = pos > idx ? pos - idx : 0U;
        std::size_t take = std::min(it->length - start_in_piece, len);
        const std::string &buf = it->buf == BufferKind::Add ? add_ : original_;
        std::string_view view(buf.data() + it->start + start_in_piece, take);
        if (!std::invoke(std::forward<Fn>(fn), view))
        {
            break;
        }

        pos += take;
        len -= take;
        idx += it->length;
    }
}
