//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/text/PieceTable.cpp
// Purpose: Provide the piece-table implementation that powers incremental text
//          editing in the Viper TUI.  The data structure stores immutable
//          "original" and append-only "add" buffers alongside a list of pieces
//          describing which slice is visible at each position.  Mutation
//          methods maintain the list and surface change summaries so dependent
//          components (line indexes, history) can stay in sync.
// Key invariants: Piece spans never overlap and always reference valid ranges
//                 of either the original or add buffer.  The list covers the
//                 document contiguously from offset zero up to the current size.
// Ownership/Lifetime: PieceTable owns both backing buffers and the piece list.
//                     Change objects copy affected text so callbacks observe
//                     stable data even if the table mutates again later.
// Links: docs/architecture.md#vipertui-text-buffer
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the @ref viper::tui::text::PieceTable text buffer core.
/// @details Functions in this translation unit keep the piece table consistent
///          after inserts and erases, surface change notifications, and expose
///          read helpers used by the higher-level @ref TextBuffer façade.

#include "tui/text/PieceTable.hpp"

#include <algorithm>
#include <utility>

namespace viper::tui::text
{
/// @brief Remember an inserted span for later callbacks.
/// @details Stores the provided @p text and offset so that
///          @ref notifyInsert can replay the change to interested listeners.
///          Empty strings clear the stored state because they carry no
///          meaningful payload for downstream components.
/// @param pos Offset where the insertion occurred.
/// @param text Newly inserted characters copied to preserve lifetime.
void PieceTable::Change::recordInsert(std::size_t pos, std::string text)
{
    if (text.empty())
    {
        insert_span_.reset();
        return;
    }
    insert_span_ = Span{pos, std::move(text)};
}

/// @brief Remember a removed span for later callbacks.
/// @details Copies the erased text so undo/redo bookkeeping can reapply it
///          after the piece table mutates further.  Passing an empty string
///          resets the stored state.
/// @param pos Offset where the erase began.
/// @param text Characters removed from the table.
void PieceTable::Change::recordErase(std::size_t pos, std::string text)
{
    if (text.empty())
    {
        erase_span_.reset();
        return;
    }
    erase_span_ = Span{pos, std::move(text)};
}

/// @brief Invoke an insertion callback if one was recorded.
/// @details When @ref recordInsert captured text, the callback is invoked with
///          the stored offset and string.  Missing callbacks or empty spans are
///          ignored so callers can opt out without extra checks.
/// @param cb Listener that consumes the recorded insertion.
void PieceTable::Change::notifyInsert(const Callback &cb) const
{
    if (cb && insert_span_)
    {
        cb(insert_span_->pos, insert_span_->text);
    }
}

/// @brief Invoke an erase callback if one was recorded.
/// @details Mirrors @ref notifyInsert but for removal events, supplying the
///          start offset and erased text to the listener when available.
/// @param cb Listener that consumes the recorded erase.
void PieceTable::Change::notifyErase(const Callback &cb) const
{
    if (cb && erase_span_)
    {
        cb(erase_span_->pos, erase_span_->text);
    }
}

/// @brief Determine whether an insertion span was captured.
/// @return True when @ref recordInsert stored a non-empty span.
bool PieceTable::Change::hasInsert() const
{
    return insert_span_.has_value();
}

/// @brief Determine whether an erase span was captured.
/// @return True when @ref recordErase stored a non-empty span.
bool PieceTable::Change::hasErase() const
{
    return erase_span_.has_value();
}

/// @brief Report the starting offset of the recorded insertion.
/// @details When no insertion was captured the method returns zero, mirroring
///          the behaviour of @ref insertedText by providing a harmless default.
/// @return Offset at which the insertion occurred.
std::size_t PieceTable::Change::insertPos() const
{
    return insert_span_ ? insert_span_->pos : 0U;
}

/// @brief Report the starting offset of the recorded erase.
/// @details Returns zero when no erase was stored so callers can branch on
///          @ref hasErase before using the value.
/// @return Offset at which the erase began.
std::size_t PieceTable::Change::erasePos() const
{
    return erase_span_ ? erase_span_->pos : 0U;
}

/// @brief Access the copied text associated with an insertion.
/// @details Provides a string view over the owned buffer so listeners can read
///          the payload without taking ownership.  When no insertion was
///          recorded an empty view is returned.
/// @return View of the inserted characters.
std::string_view PieceTable::Change::insertedText() const
{
    return insert_span_ ? std::string_view(insert_span_->text) : std::string_view{};
}

/// @brief Access the copied text associated with an erase.
/// @details Returns an empty view when @ref hasErase is @c false.
/// @return View of the removed characters.
std::string_view PieceTable::Change::erasedText() const
{
    return erase_span_ ? std::string_view(erase_span_->text) : std::string_view{};
}

/// @brief Replace the table contents with a fresh backing string.
/// @details Clears the add buffer and piece list before populating them with a
///          single span referencing the new @p text.  When the table previously
///          held content the erased text is reported via the returned
///          @ref Change so callers can update dependent structures.
/// @param text New document contents copied into the "original" buffer.
/// @return Change summarising the remove/insert operations performed.
PieceTable::Change PieceTable::load(std::string text)
{
    Change change;
    if (size_ > 0)
    {
        change.recordErase(0, getText(0, size_));
    }

    original_ = std::move(text);
    add_.clear();
    pieces_.clear();
    size_ = original_.size();

    if (!original_.empty())
    {
        pieces_.push_back(Piece{BufferKind::Original, 0, original_.size()});
        change.recordInsert(0, original_);
    }

    return change;
}

/// @brief Query the logical document length in bytes.
/// @return Number of characters visible through the piece table.
std::size_t PieceTable::size() const
{
    return size_;
}

/// @brief Insert text into the table without emitting line-index updates.
/// @details Appends @p text to the add buffer, splices a new piece into the list
///          at @p pos, and returns a @ref Change describing the mutation so the
///          caller can notify observers.  Offsets that land in the middle of an
///          existing piece cause it to split so the new span can be inserted
///          cleanly.
/// @param pos Byte offset where the insertion should occur.
/// @param text Characters to insert.
/// @return Change containing the inserted span information.
PieceTable::Change PieceTable::insertInternal(std::size_t pos, std::string_view text)
{
    Change change;
    if (text.empty())
    {
        return change;
    }

    std::size_t offset = 0;
    auto it = findPiece(pos, offset);
    Piece newPiece{BufferKind::Add, add_.size(), text.size()};
    add_.append(text);

    if (it == pieces_.end())
    {
        pieces_.push_back(newPiece);
    }
    else if (offset == 0)
    {
        pieces_.insert(it, newPiece);
    }
    else if (offset == it->length)
    {
        ++it;
        pieces_.insert(it, newPiece);
    }
    else
    {
        Piece tail = *it;
        tail.start += offset;
        tail.length -= offset;
        it->length = offset;
        ++it;
        pieces_.insert(it, newPiece);
        pieces_.insert(it, tail);
    }

    size_ += text.size();
    change.recordInsert(pos, std::string(text));
    return change;
}

/// @brief Erase a byte range from the table without notifying observers.
/// @details Locates the affected pieces, copies the removed text for undo
///          bookkeeping, and trims or removes pieces as required.  When the
///          requested range extends past the current document size the excess is
///          ignored, matching typical text-editor semantics.
/// @param pos Offset of the first byte to erase.
/// @param len Number of bytes to remove.
/// @return Change describing the erased span.
PieceTable::Change PieceTable::eraseInternal(std::size_t pos, std::size_t len)
{
    Change change;
    if (len == 0)
    {
        return change;
    }

    std::string removed = getText(pos, len);
    if (removed.empty())
    {
        return change;
    }

    std::size_t offset = 0;
    auto it = findPiece(pos, offset);
    if (it == pieces_.end())
    {
        return change;
    }

    std::size_t remaining = removed.size();

    if (offset > 0)
    {
        Piece tail = *it;
        tail.start += offset;
        tail.length -= offset;
        it->length = offset;
        ++it;
        if (tail.length > 0)
        {
            it = pieces_.insert(it, tail);
        }
    }

    while (it != pieces_.end() && remaining > 0)
    {
        if (remaining < it->length)
        {
            it->start += remaining;
            it->length -= remaining;
            remaining = 0;
            break;
        }
        else
        {
            remaining -= it->length;
            it = pieces_.erase(it);
        }
    }

    size_ -= removed.size();
    change.recordErase(pos, std::move(removed));
    return change;
}

/// @brief Materialise a substring from the logical document.
/// @details Walks the piece list, stitching slices from the original and add
///          buffers to produce the requested view.  The method reserves the
///          output buffer up front to minimise reallocations.
/// @param pos Starting offset inside the logical document.
/// @param len Number of bytes to copy.
/// @return Owning string containing the requested text (shortened if the range
///         reaches EOF).
std::string PieceTable::getText(std::size_t pos, std::size_t len) const
{
    std::string out;
    out.reserve(len);
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
        out.append(buf.substr(it->start + start_in_piece, take));
        pos += take;
        len -= take;
        idx += it->length;
    }
    return out;
}

/// @brief Locate the piece that covers a logical position.
/// @details Traverses the piece list accumulating length until @p pos falls
///          within the current piece.  The byte offset relative to that piece is
///          written to @p offset so callers can split or trim the piece.
/// @param pos Document offset to resolve.
/// @param offset Updated with the byte position inside the returned piece.
/// @return Iterator to the covering piece or @c end() when @p pos is past EOF.
std::list<PieceTable::Piece>::iterator PieceTable::findPiece(std::size_t pos, std::size_t &offset)
{
    std::size_t idx = 0;
    for (auto it = pieces_.begin(); it != pieces_.end(); ++it)
    {
        if (pos <= idx + it->length)
        {
            offset = pos - idx;
            return it;
        }
        idx += it->length;
    }
    offset = 0;
    return pieces_.end();
}

/// @brief Const overload of @ref findPiece that does not permit mutation.
/// @details Shares the same traversal logic as the non-const overload but
///          returns a @c const_iterator for read-only callers.
/// @param pos Document offset to resolve.
/// @param offset Updated with the byte position inside the returned piece.
/// @return Iterator to the covering piece or @c cend() when @p pos is past EOF.
std::list<PieceTable::Piece>::const_iterator PieceTable::findPiece(std::size_t pos,
                                                                   std::size_t &offset) const
{
    std::size_t idx = 0;
    for (auto it = pieces_.cbegin(); it != pieces_.cend(); ++it)
    {
        if (pos <= idx + it->length)
        {
            offset = pos - idx;
            return it;
        }
        idx += it->length;
    }
    offset = 0;
    return pieces_.cend();
}
} // namespace viper::tui::text
