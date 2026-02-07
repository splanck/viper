//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the PieceTable class, which implements the piece table
// data structure for efficient text editing in Viper's TUI. The piece table
// maintains two buffers — an original buffer (immutable after load) and an
// append-only add buffer — plus a list of "pieces" that describe the document
// as a sequence of spans referencing one of the two buffers.
//
// Insertions append new text to the add buffer and split/insert pieces.
// Deletions split pieces and remove the deleted range. Neither operation
// modifies existing buffer content, making the piece table inherently
// suited for undo/redo via Change objects that capture span metadata.
//
// The Change struct returned from mutating operations records what was
// inserted and/or erased, enabling the EditHistory to replay operations
// in both directions for undo and redo.
//
// Key invariants:
//   - The original buffer is never modified after load().
//   - The add buffer is append-only; text is never removed from it.
//   - The sum of all piece lengths equals size().
//   - Piece boundaries are always consistent after each operation.
//
// Ownership: PieceTable owns both buffers and the piece list by value.
// Change objects returned from mutations own copies of affected text.
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
/// @brief Piece table implementation providing efficient insert/erase operations
///        for text editing in the TUI editor.
/// @details Uses an original buffer (set at load time, never modified) and an
///          append-only add buffer. The document is represented as an ordered list
///          of pieces, each referencing a contiguous span in one of the two buffers.
///          This structure provides O(n) insert/erase where n is the number of pieces,
///          which remains small for typical editing sessions.
class PieceTable
{
  public:
    /// @brief Captures the metadata of a single piece table mutation for undo/redo.
    /// @details Records both the inserted and erased spans (position + text) from a
    ///          single insert or erase operation. The EditHistory stores these changes
    ///          and replays them in reverse for undo or forward for redo.
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

    /// @brief Replace all content with fresh text, resetting both buffers.
    /// @details Clears the piece list, sets the original buffer to the given text,
    ///          empties the add buffer, and creates a single piece spanning the
    ///          entire original buffer. Returns a Change for undo tracking.
    /// @param text New content to load.
    /// @return Change describing the full replacement.
    Change load(std::string text);

    /// @brief Current byte size.
    [[nodiscard]] std::size_t size() const;

    /// @brief Extract a substring from the logical document.
    /// @details Walks the piece list to find pieces overlapping [pos, pos+len)
    ///          and copies the relevant byte ranges into a contiguous string.
    /// @param pos Starting byte offset in the logical document.
    /// @param len Number of bytes to extract.
    /// @return The extracted text as a contiguous string.
    [[nodiscard]] std::string getText(std::size_t pos, std::size_t len) const;

    /// @brief Visit contiguous buffer segments covering a byte range without copying.
    /// @details Iterates the piece list and invokes the visitor for each contiguous
    ///          string_view segment within [pos, pos+len). The visitor returns false
    ///          to stop early. This avoids string allocation for rendering.
    /// @tparam Fn Callable taking std::string_view, returning bool.
    /// @param pos Starting byte offset.
    /// @param len Number of bytes in the range.
    /// @param fn Visitor invoked for each segment.
    template <typename Fn> void forEachSegment(std::size_t pos, std::size_t len, Fn &&fn) const;

    /// @brief Insert text at the given byte position within the logical document.
    /// @details Appends the new text to the add buffer, then splits the piece list
    ///          at the insertion point and inserts a new piece referencing the
    ///          appended text. Returns a Change recording the insertion metadata.
    /// @param pos Byte offset where text will be inserted.
    /// @param text The text to insert.
    /// @return Change describing the insertion for undo/redo tracking.
    Change insertInternal(std::size_t pos, std::string_view text);

    /// @brief Erase a range of bytes from the logical document.
    /// @details Splits pieces at the erase boundaries and removes all pieces
    ///          (or partial pieces) within the erased range. The erased text is
    ///          captured in the returned Change for undo replay. Neither buffer
    ///          is modified; only the piece list is updated.
    /// @param pos Starting byte offset of the range to erase.
    /// @param len Number of bytes to erase.
    /// @return Change describing the erasure for undo/redo tracking.
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
        std::size_t take = (std::min)(it->length - start_in_piece, len);
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
