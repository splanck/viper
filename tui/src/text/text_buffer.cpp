//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/text/text_buffer.cpp
// Purpose: Implement the @ref viper::tui::text::TextBuffer façade that wires the
//          piece table, line index, and edit history components into a cohesive
//          editable text model.  The translation unit concentrates lifecycle
//          management and notification plumbing so higher-level widgets can rely
//          on a simple, value-oriented API.
// Key invariants: Helper structures remain in lock-step—every mutation updates
//                 the piece table, then notifies the line index and history
//                 trackers.  Line offsets are monotonically increasing and
//                 always reference valid byte positions.
// Ownership/Lifetime: TextBuffer owns its helpers and returns copied strings to
//                     callers, avoiding dangling references when edits occur.
// Links: docs/architecture.md#vipertui-text-buffer
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides the concrete implementation of the @ref TextBuffer API.
/// @details Functions here coordinate editing operations, track undo/redo
///          history, and expose convenient views over the underlying piece
///          table.

#include "tui/text/text_buffer.hpp"

#include <utility>

namespace viper::tui::text
{
/// @brief Construct a view over a contiguous slice of the piece table.
/// @details Stores a reference to the backing @p table along with the byte
///          offset and span length.  The view does not copy text; it simply
///          records metadata so @ref forEachSegment can later walk the table.
/// @param table Piece table that owns the text.
/// @param offset Starting byte offset of the line within the table.
/// @param length Number of bytes exposed by the view.
TextBuffer::LineView::LineView(const PieceTable &table, std::size_t offset, std::size_t length)
    : table_(table), offset_(offset), length_(length)
{
}

/// @brief Retrieve the starting offset tracked by the view.
/// @return Byte offset measured from the beginning of the buffer.
std::size_t TextBuffer::LineView::offset() const
{
    return offset_;
}

/// @brief Retrieve the number of bytes covered by the view.
/// @return Length of the view expressed in bytes.
std::size_t TextBuffer::LineView::length() const
{
    return length_;
}

/// @copydoc viper::tui::text::TextBuffer::LineView::forEachSegment
void TextBuffer::LineView::forEachSegment(SegmentVisitor fn) const
{
    table_.forEachSegment(offset_, length_, fn);
}

/// @brief Replace the entire document contents with @p text.
/// @details The helper rebuilds the underlying piece table, resets the line
///          index using the inserted span, and clears undo history to avoid
///          replaying edits from the previous document.
/// @param text New contents to populate the buffer with.
void TextBuffer::load(std::string text)
{
    auto change = table_.load(std::move(text));
    line_index_.reset(change.insertedText());
    history_.clear();
}

/// @brief Report the current document length.
/// @return Logical byte length exposed by the buffer.
std::size_t TextBuffer::size() const
{
    return table_.size();
}

/// @brief Report how many lines are indexed.
/// @return Number of lines tracked by the line index.
std::size_t TextBuffer::lineCount() const
{
    return line_index_.count();
}

/// @brief Compute the starting offset of a zero-based line number.
/// @details Out-of-range requests clamp to the buffer size so callers can use
///          the result as an append position without additional checks.
/// @param lineNo Line to query.
/// @return Byte offset of the first character on the requested line.
std::size_t TextBuffer::lineStart(std::size_t lineNo) const
{
    if (lineNo >= line_index_.count())
    {
        return table_.size();
    }
    return line_index_.start(lineNo);
}

/// @brief Determine the offset one past the last character in a line.
/// @details Uses the next line's start when available; otherwise returns the
///          buffer size.  Trailing newline characters are excluded from the
///          count so range computations remain intuitive.
/// @param lineNo Line to query.
/// @return Offset following the final visible character.
std::size_t TextBuffer::lineEnd(std::size_t lineNo) const
{
    if (lineNo >= line_index_.count())
    {
        return table_.size();
    }

    const std::size_t start = line_index_.start(lineNo);
    const std::size_t nextLine = lineNo + 1;
    if (nextLine < line_index_.count())
    {
        const std::size_t nextStart = line_index_.start(nextLine);
        if (nextStart > start)
        {
            return nextStart - 1;
        }
        return start;
    }
    return table_.size();
}

/// @brief Alias for @ref lineStart retained for semantic clarity.
/// @param lineNo Line to query.
/// @return Byte offset at the beginning of the requested line.
std::size_t TextBuffer::lineOffset(std::size_t lineNo) const
{
    return lineStart(lineNo);
}

/// @brief Compute the number of bytes that belong to a line.
/// @param lineNo Line to query.
/// @return Length of the line excluding the trailing newline.
std::size_t TextBuffer::lineLength(std::size_t lineNo) const
{
    const std::size_t start = lineStart(lineNo);
    const std::size_t end = lineEnd(lineNo);
    if (end <= start)
    {
        return 0;
    }
    return end - start;
}

/// @brief Produce a convenience view describing a line segment.
/// @param lineNo Line to inspect.
/// @return View over the specified line.
TextBuffer::LineView TextBuffer::lineView(std::size_t lineNo) const
{
    return LineView(table_, lineOffset(lineNo), lineLength(lineNo));
}

/// @brief Begin grouping subsequent edits into a single undoable transaction.
void TextBuffer::beginTxn()
{
    history_.beginTxn();
}

/// @brief Finish the active transaction so future edits start a new group.
void TextBuffer::endTxn()
{
    history_.endTxn();
}

/// @brief Insert text while keeping the line index and history synchronised.
/// @details Applies the insertion to the piece table, notifies the line index of
///          the affected range, and records the edit for undo when characters
///          were actually inserted.
/// @param pos Byte offset where the insertion should occur.
/// @param text Characters to add.
void TextBuffer::insert(std::size_t pos, std::string_view text)
{
    auto change = table_.insertInternal(pos, text);
    change.notifyInsert([this](std::size_t changePos, std::string_view inserted)
                        { line_index_.onInsert(changePos, inserted); });
    if (change.hasInsert())
    {
        history_.recordInsert(change.insertPos(), std::string(change.insertedText()));
    }
}

/// @brief Remove a byte range and propagate bookkeeping updates.
/// @param pos Offset of the first byte to erase.
/// @param len Number of bytes to remove.
void TextBuffer::erase(std::size_t pos, std::size_t len)
{
    auto change = table_.eraseInternal(pos, len);
    change.notifyErase([this](std::size_t changePos, std::string_view removed)
                       { line_index_.onErase(changePos, removed); });
    if (change.hasErase())
    {
        history_.recordErase(change.erasePos(), std::string(change.erasedText()));
    }
}

/// @brief Undo the most recent recorded edit.
/// @details Replays the inverse operation via the piece table and line index so
///          the document returns to its previous state.
/// @return True when an edit was undone.
bool TextBuffer::undo()
{
    return history_.undo(
        [this](const EditHistory::Op &op)
        {
            if (op.type == EditHistory::OpType::Insert)
            {
                auto change = table_.eraseInternal(op.pos, op.text.size());
                change.notifyErase([this](std::size_t changePos, std::string_view removed)
                                   { line_index_.onErase(changePos, removed); });
            }
            else
            {
                auto change = table_.insertInternal(op.pos, op.text);
                change.notifyInsert([this](std::size_t changePos, std::string_view inserted)
                                    { line_index_.onInsert(changePos, inserted); });
            }
        });
}

/// @brief Reapply the next redoable edit, if any.
/// @details Mirrors @ref undo but reapplies the stored operation.
/// @return True when an edit was redone.
bool TextBuffer::redo()
{
    return history_.redo(
        [this](const EditHistory::Op &op)
        {
            if (op.type == EditHistory::OpType::Insert)
            {
                auto change = table_.insertInternal(op.pos, op.text);
                change.notifyInsert([this](std::size_t changePos, std::string_view inserted)
                                    { line_index_.onInsert(changePos, inserted); });
            }
            else
            {
                auto change = table_.eraseInternal(op.pos, op.text.size());
                change.notifyErase([this](std::size_t changePos, std::string_view removed)
                                   { line_index_.onErase(changePos, removed); });
            }
        });
}

/// @brief Materialise the entire document into a string.
/// @return Copy of the buffer contents.
std::string TextBuffer::str() const
{
    return table_.getText(0, table_.size());
}

/// @brief Retrieve the contents of a single line.
/// @details Returns an empty string when the line index is out of range.  The
///          trailing newline character is omitted from the copy.
/// @param lineNo Line to read.
/// @return Copied characters for the requested line.
std::string TextBuffer::getLine(std::size_t lineNo) const
{
    if (lineNo >= line_index_.count())
    {
        return {};
    }
    std::size_t start = line_index_.start(lineNo);
    std::size_t end =
        (lineNo + 1 < line_index_.count()) ? line_index_.start(lineNo + 1) - 1 : table_.size();
    if (end < start)
    {
        end = start;
    }
    return table_.getText(start, end - start);
}

/// @copydoc viper::tui::text::TextBuffer::forEachLine
void TextBuffer::forEachLine(LineVisitor fn) const
{
    const std::size_t lines = line_index_.count();
    for (std::size_t line = 0; line < lines; ++line)
    {
        LineView view(table_, lineOffset(line), lineLength(line));
        if (!fn(line, view))
        {
            break;
        }
    }
}
} // namespace viper::tui::text
