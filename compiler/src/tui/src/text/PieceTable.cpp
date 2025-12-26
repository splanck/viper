//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/text/PieceTable.cpp
// Purpose: Implement the gapless piece-table buffer that backs the TUI
//          text-editing primitives.
// Key invariants: The piece list always describes the buffer contents in
//                 document order, spans never reference storage outside the
//                 owned backing strings, and mutations surface detailed change
//                 callbacks so auxiliary structures (line index, undo stack)
//                 stay synchronised.
// Ownership/Lifetime: `PieceTable` owns both the original and "add" buffers as
//                     contiguous strings.  Change objects capture copies of the
//                     affected text to guarantee that callback receivers observe
//                     stable payloads even if the table mutates again.
// Links: docs/architecture.md#vipertui-architecture
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Concrete implementation of the @ref viper::tui::text::PieceTable data
///        structure.
/// @details The piece table stores document text as a list of spans referencing
///          either the original file contents or an append-only "add" buffer.
///          Mutating operations update the span list and return a @ref Change
///          object that records the inserted or erased slice.  Downstream
///          collaborators such as @ref LineIndex consume those callbacks to keep
///          derived state in sync without performing their own diffing.

#include "tui/text/PieceTable.hpp"

#include <algorithm>
#include <utility>

namespace viper::tui::text
{
/// @brief Remember an inserted span so observers can be notified later.
/// @details Stores a copy of the inserted text along with the byte offset at
///          which it appeared.  Empty strings clear the tracked span to signal
///          that no insertion took place, ensuring downstream observers do not
///          receive misleading callbacks.
/// @param pos Byte offset where the insertion occurred.
/// @param text Newly inserted UTF-8 slice copied for later notification.
void PieceTable::Change::recordInsert(std::size_t pos, std::string text)
{
    if (text.empty())
    {
        insert_span_.reset();
        return;
    }
    insert_span_ = Span{pos, std::move(text)};
}

/// @brief Remember an erased span so observers can be notified later.
/// @details Persists the removed text so clients that track undo history or
///          syntax highlights can reinstate the original bytes on demand.  Empty
///          removals clear the cached span and therefore suppress callbacks.
/// @param pos Byte offset where the erasure began.
/// @param text Removed UTF-8 slice copied for later notification.
void PieceTable::Change::recordErase(std::size_t pos, std::string text)
{
    if (text.empty())
    {
        erase_span_.reset();
        return;
    }
    erase_span_ = Span{pos, std::move(text)};
}

/// @brief Deliver the cached insertion span to the provided callback.
/// @details Invokes @p cb when both the callback and recorded span are present.
///          The helper keeps notification optional so callers can react only to
///          the change types they care about.
/// @param cb Observer that receives the inserted range.
void PieceTable::Change::notifyInsert(const Callback &cb) const
{
    if (cb && insert_span_)
    {
        cb(insert_span_->pos, insert_span_->text);
    }
}

/// @brief Deliver the cached erasure span to the provided callback.
/// @details Mirrors @ref notifyInsert by invoking @p cb when the recorded
///          erasure span exists.  Observers typically update auxiliary indices or
///          undo stacks using the supplied payload.
/// @param cb Observer that receives the erased range.
void PieceTable::Change::notifyErase(const Callback &cb) const
{
    if (cb && erase_span_)
    {
        cb(erase_span_->pos, erase_span_->text);
    }
}

/// @brief Determine whether an insertion was captured for this change.
/// @return True when @ref recordInsert stored a non-empty span.
bool PieceTable::Change::hasInsert() const
{
    return insert_span_.has_value();
}

/// @brief Determine whether an erasure was captured for this change.
/// @return True when @ref recordErase stored a non-empty span.
bool PieceTable::Change::hasErase() const
{
    return erase_span_.has_value();
}

/// @brief Byte offset associated with the recorded insertion.
/// @return Offset previously supplied to @ref recordInsert, or zero when no
///         insertion was tracked.
std::size_t PieceTable::Change::insertPos() const
{
    return insert_span_ ? insert_span_->pos : 0U;
}

/// @brief Byte offset associated with the recorded erasure.
/// @return Offset previously supplied to @ref recordErase, or zero when no
///         erasure was tracked.
std::size_t PieceTable::Change::erasePos() const
{
    return erase_span_ ? erase_span_->pos : 0U;
}

/// @brief View of the text that was inserted during the change.
/// @return Read-only string view referencing the cached insertion payload.
std::string_view PieceTable::Change::insertedText() const
{
    return insert_span_ ? std::string_view(insert_span_->text) : std::string_view{};
}

/// @brief View of the text that was erased during the change.
/// @return Read-only string view referencing the cached erasure payload.
std::string_view PieceTable::Change::erasedText() const
{
    return erase_span_ ? std::string_view(erase_span_->text) : std::string_view{};
}

/// @brief Replace the entire table contents with @p text.
/// @details Discards prior spans, rebuilds the "original" buffer from the new
///          text, and emits a change that models the full replacement.  When the
///          previous document was non-empty the change reports an initial erase
///          followed by a full insert so observers can reset their state.
/// @param text New document contents.
/// @return Change describing the replacement operation.
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

/// @brief Report the number of bytes currently represented by the table.
/// @return Total length of the logical document in bytes.
std::size_t PieceTable::size() const
{
    return size_;
}

/// @brief Insert @p text at @p pos and update the piece list accordingly.
/// @details Locates the piece containing the insertion point, splits it if
///          necessary, appends @p text to the add buffer, and splices a new piece
///          referencing the appended region.  The method records the insertion in
///          the returned @ref Change so observers can update derived structures
///          without re-scanning the document.
/// @param pos Byte offset where the insertion occurs.
/// @param text UTF-8 data to splice into the document.
/// @return Change object describing the inserted span.
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

/// @brief Remove @p len bytes beginning at @p pos.
/// @details Traverses the piece list, carving out spans that overlap the removal
///          range and adjusting lengths and start offsets as necessary.  The
///          removed text is gathered via @ref getText so callers (undo stacks,
///          syntax highlighters) can reinstate the original contents later.  The
///          returned @ref Change reports both the removal position and payload.
/// @param pos Starting byte offset of the removal.
/// @param len Number of bytes to erase.
/// @return Change describing the erased span (empty when nothing was removed).
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

/// @brief Extract a slice of the document as a freshly allocated string.
/// @details Iterates across the piece list, copying the relevant fragments from
///          the backing buffers into a contiguous @c std::string.  The helper is
///          central to read operations and to change recording for undo/redo.
/// @param pos Starting byte offset of the slice.
/// @param len Number of bytes to copy.
/// @return Newly allocated string containing the requested text.
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

/// @brief Locate the mutable piece that covers @p pos.
/// @details Walks the piece list accumulating lengths until it finds the span
///          that encloses @p pos.  The byte offset within the selected piece is
///          returned via @p offset so callers can decide whether the piece needs
///          to be split.
/// @param pos Target byte offset within the document.
/// @param offset Receives the relative position within the located piece.
/// @return Iterator to the piece containing @p pos, or @ref pieces_.end() when
///         the position is past the end of the document.
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

/// @brief Locate the read-only piece that covers @p pos.
/// @details Const overload of @ref findPiece that allows callers to inspect the
///          piece list without mutating it.  The semantics mirror the mutable
///          variant, including the @p offset result.
/// @param pos Target byte offset within the document.
/// @param offset Receives the relative position within the located piece.
/// @return Const iterator to the piece containing @p pos, or @ref pieces_.cend()
///         when the position lies past the end of the document.
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
