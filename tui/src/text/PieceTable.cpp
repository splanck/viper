//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/text/PieceTable.cpp
// Purpose: Implement the piece-table text storage backing the terminal UI
//          editor and expose change-tracking callbacks for dependent helpers.
// Key invariants: Piece metadata splits only when necessary, spans always
//                 reference immutable buffers, and aggregate size bookkeeping
//                 stays consistent after every edit.
// Ownership/Lifetime: The table owns two buffers (original and add) while
//                     changes record copied spans to keep callbacks safe after
//                     the table mutates again.
// Links: docs/architecture.md#vipertui-text-buffer
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implementation of the mutable piece table used by the TUI text buffer.
/// @details The translation unit houses edit operations plus the nested
///          `Change` utility that captures inserted/erased spans.  Keeping the
///          logic out of the header shields callers from `<list>` and
///          `<string>` dependencies while documenting how callbacks receive
///          stable snapshots of edited text.

#include "tui/text/PieceTable.hpp"

#include <algorithm>
#include <utility>

namespace viper::tui::text
{
/// @brief Capture details about an insertion performed on the piece table.
/// @details Stores the provided span as an owning string so that callbacks can
///          observe the inserted bytes even if the caller mutates the table
///          again before notifications fire.  Empty inserts clear any previously
///          recorded span so downstream consumers know no change occurred.
/// @param pos Logical buffer offset where text was inserted.
/// @param text Copy of the inserted text payload.
void PieceTable::Change::recordInsert(std::size_t pos, std::string text)
{
    if (text.empty())
    {
        insert_span_.reset();
        return;
    }
    insert_span_ = Span{pos, std::move(text)};
}

/// @brief Capture details about an erase performed on the piece table.
/// @details Copies the removed bytes to preserve them for undo/redo history and
///          line-index updates.  Passing an empty string clears any previous
///          erase record, signalling that no removal occurred for this change.
/// @param pos Logical buffer offset where text was removed.
/// @param text Copy of the erased text payload.
void PieceTable::Change::recordErase(std::size_t pos, std::string text)
{
    if (text.empty())
    {
        erase_span_.reset();
        return;
    }
    erase_span_ = Span{pos, std::move(text)};
}

/// @brief Invoke a callback with the recorded insertion span, if any.
/// @details The helper checks that both a callback and an insertion span are
///          present before invoking the callback with the stored offset and
///          payload.  This guards against spurious notifications when callers
///          ignore insertion tracking.
/// @param cb Callback to receive the insertion span.
void PieceTable::Change::notifyInsert(const Callback &cb) const
{
    if (cb && insert_span_)
    {
        cb(insert_span_->pos, insert_span_->text);
    }
}

/// @brief Invoke a callback with the recorded erasure span, if any.
/// @details Mirrors @ref notifyInsert by emitting the stored removal offset and
///          payload only when both exist.  Undo/redo machinery uses this to
///          update line indexes without duplicating bookkeeping logic.
/// @param cb Callback to receive the erased span.
void PieceTable::Change::notifyErase(const Callback &cb) const
{
    if (cb && erase_span_)
    {
        cb(erase_span_->pos, erase_span_->text);
    }
}

/// @brief Determine whether the change recorded an insertion span.
/// @return True when @ref recordInsert captured a non-empty insertion.
bool PieceTable::Change::hasInsert() const
{
    return insert_span_.has_value();
}

/// @brief Determine whether the change recorded an erasure span.
/// @return True when @ref recordErase captured a non-empty removal.
bool PieceTable::Change::hasErase() const
{
    return erase_span_.has_value();
}

/// @brief Retrieve the insertion offset recorded for this change.
/// @return Offset supplied to @ref recordInsert, or zero when no insertion was recorded.
std::size_t PieceTable::Change::insertPos() const
{
    return insert_span_ ? insert_span_->pos : 0U;
}

/// @brief Retrieve the erasure offset recorded for this change.
/// @return Offset supplied to @ref recordErase, or zero when no erasure was recorded.
std::size_t PieceTable::Change::erasePos() const
{
    return erase_span_ ? erase_span_->pos : 0U;
}

/// @brief Access the inserted text payload, if any, without transferring ownership.
/// @return View over the copied insertion text or an empty view when no insertion occurred.
std::string_view PieceTable::Change::insertedText() const
{
    return insert_span_ ? std::string_view(insert_span_->text) : std::string_view{};
}

/// @brief Access the erased text payload, if any, without transferring ownership.
/// @return View over the copied removal text or an empty view when no erasure occurred.
std::string_view PieceTable::Change::erasedText() const
{
    return erase_span_ ? std::string_view(erase_span_->text) : std::string_view{};
}

/// @brief Replace the table contents with a fresh text snapshot.
/// @details Clears the existing piece metadata, resets the add buffer, and
///          seeds the table with a single piece referencing the new original
///          buffer.  The returned change records any removal of prior contents
///          plus the inserted snapshot so observers (line index, history) can
///          resynchronise.
/// @param text New baseline document to load into the table.
/// @return Change describing the removal of the old document and insertion of the new one.
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

/// @brief Report the total number of bytes addressable through the table.
/// @return Aggregate length of all pieces after the most recent edits.
std::size_t PieceTable::size() const
{
    return size_;
}

/// @brief Insert text into the piece table without notifying observers.
/// @details Locates the piece containing @p pos, splitting it if necessary,
///          and splices in a new piece referencing the additive buffer.  The
///          additive buffer owns a copy of the inserted bytes to keep spans
///          stable after subsequent edits.  The returned change captures the
///          insertion so callers can trigger callbacks once ancillary data
///          structures are ready.
/// @param pos Logical byte offset where the insertion should occur.
/// @param text Text to insert at @p pos.
/// @return Change describing the inserted span (or empty if @p text was empty).
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

/// @brief Remove a span of text from the table without notifying observers.
/// @details Walks the piece list to trim or erase segments overlapping the
///          requested range.  When a piece straddles the removal boundary it is
///          split so the unaffected portion remains.  The removed text is
///          materialised via @ref getText to preserve it for undo/redo history
///          and change notifications.
/// @param pos Offset of the first byte to remove.
/// @param len Number of bytes to erase.
/// @return Change describing the erased span (or empty if nothing was removed).
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

/// @brief Materialise a substring of the logical buffer into a std::string.
/// @details Iterates pieces, copying runs from either the original or additive
///          buffer as needed until @p len bytes have been collected.  The
///          routine is used by undo/redo logic and callers needing temporary
///          contiguous snapshots of buffer data.
/// @param pos Starting offset of the desired substring.
/// @param len Number of bytes to copy.
/// @return Newly allocated string containing the requested substring (possibly shorter at EOF).
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

/// @brief Locate the mutable piece covering a logical offset.
/// @details Advances through the piece list accumulating lengths until the
///          supplied offset falls within a piece.  The function returns the
///          iterator plus the byte offset within that piece, or `pieces_.end()`
///          if the position lies past the current document end.
/// @param pos Logical buffer offset to resolve.
/// @param offset Output parameter receiving the offset within the returned piece.
/// @return Iterator pointing at the containing piece or @c pieces_.end().
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

/// @brief Locate the const piece covering a logical offset.
/// @details Const-qualified variant of @ref findPiece that allows callers to
///          resolve offsets without mutating the piece list.
/// @param pos Logical buffer offset to resolve.
/// @param offset Output parameter receiving the offset within the returned piece.
/// @return Const iterator pointing at the containing piece or @c pieces_.cend().
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
