//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/src/text/text_buffer.cpp
// Purpose: Orchestrate the piece table, line index, and edit history building
//          blocks that power the terminal UI text buffer abstraction.
// Key invariants: Helper structures stay in sync through change callbacks,
//                 undo/redo operations are transactional, and all external
//                 views operate on logical offsets rather than raw storage.
// Ownership/Lifetime: TextBuffer owns its helper instances and returns copied
//                     strings to callers to avoid exposing internal storage.
// Links: docs/architecture.md#vipertui-text-buffer
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements the high-level text buffer facade used by the terminal UI.
/// @details Provides line-based views, mutation APIs, and undo/redo plumbing on
///          top of the underlying piece table.  Keeping the coordination logic
///          out of the header shields clients from heavy includes while
///          documenting how edits propagate to helper structures.

#include "tui/text/text_buffer.hpp"

#include <utility>

namespace viper::tui::text
{
/// @brief Construct a lightweight view over a logical line within the piece table.
/// @details Line views store the originating piece table reference along with
///          the byte range describing a single logical line.  They allow callers
///          to iterate the underlying segments without copying text.
/// @param table Backing piece table providing the text storage.
/// @param offset Byte offset where the line begins.
/// @param length Number of bytes comprising the line (excluding newline terminator).
TextBuffer::LineView::LineView(const PieceTable &table, std::size_t offset, std::size_t length)
    : table_(table), offset_(offset), length_(length)
{
}

/// @brief Retrieve the starting offset of the line represented by this view.
/// @return Byte offset relative to the start of the buffer.
std::size_t TextBuffer::LineView::offset() const
{
    return offset_;
}

/// @brief Retrieve the length in bytes of the line represented by this view.
/// @return Number of bytes encompassed by the line (excluding newline terminator).
std::size_t TextBuffer::LineView::length() const
{
    return length_;
}

/// @brief Visit each contiguous segment making up the logical line.
/// @details The visitor receives spans that originate from either the original
///          or additive buffer.  Iteration stops early if the visitor returns
///          false.  This allows renderers to process lines without copying text.
/// @param fn Visitor invoked with segment offsets and payloads.
void TextBuffer::LineView::forEachSegment(SegmentVisitor fn) const
{
    table_.forEachSegment(offset_, length_, fn);
}

/// @brief Replace the entire buffer contents with @p text.
/// @details Delegates to @ref PieceTable::load to rebuild the storage and then
///          reinitialises the line index and edit history so they reflect the
///          fresh snapshot.  Undo history is cleared, matching typical editor
///          semantics after opening a new document.
/// @param text New document contents.
void TextBuffer::load(std::string text)
{
    auto change = table_.load(std::move(text));
    line_index_.reset(change.insertedText());
    history_.clear();
}

/// @brief Report the current buffer length in bytes.
/// @return Number of addressable bytes after all edits.
std::size_t TextBuffer::size() const
{
    return table_.size();
}

/// @brief Report how many logical lines are tracked in the buffer.
/// @return Count of newline-delimited lines according to the line index.
std::size_t TextBuffer::lineCount() const
{
    return line_index_.count();
}

/// @brief Resolve the starting offset of a logical line.
/// @details Returns the buffer size when @p lineNo exceeds the known line count
///          so callers can gracefully clamp to the end of the document.
/// @param lineNo Zero-based line number to resolve.
/// @return Byte offset where the line begins.
std::size_t TextBuffer::lineStart(std::size_t lineNo) const
{
    if (lineNo >= line_index_.count())
    {
        return table_.size();
    }
    return line_index_.start(lineNo);
}

/// @brief Resolve the offset of the newline terminating a line.
/// @details When the requested line is the last one, the end offset is defined
///          as the end of the buffer.  Empty lines report their start offset so
///          callers can compute zero lengths safely.
/// @param lineNo Zero-based line number to resolve.
/// @return Byte offset immediately following the line's contents.
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

/// @brief Alias for @ref lineStart retained for API symmetry.
/// @param lineNo Zero-based line number to resolve.
/// @return Byte offset where the line begins.
std::size_t TextBuffer::lineOffset(std::size_t lineNo) const
{
    return lineStart(lineNo);
}

/// @brief Compute the number of bytes contained within a logical line.
/// @details Subtracts the start offset from the computed end offset and guards
///          against underflow when the line is empty.
/// @param lineNo Zero-based line number to query.
/// @return Length of the line in bytes.
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

/// @brief Produce a lightweight view describing a specific line.
/// @param lineNo Zero-based line number to view.
/// @return @ref LineView capturing the line's byte range.
TextBuffer::LineView TextBuffer::lineView(std::size_t lineNo) const
{
    return LineView(table_, lineOffset(lineNo), lineLength(lineNo));
}

/// @brief Start grouping subsequent edits into a single undo transaction.
/// @details Forwards to the underlying edit history, which collapses nested
///          calls into a single transaction.
void TextBuffer::beginTxn()
{
    history_.beginTxn();
}

/// @brief Complete the current undo transaction if one is active.
/// @details Delegates to @ref EditHistory::endTxn, allowing higher-level code to
///          bracket batched edits.
void TextBuffer::endTxn()
{
    history_.endTxn();
}

/// @brief Insert text into the buffer at the specified offset.
/// @details Applies the change to the piece table, updates the line index, and
///          records the operation in the edit history so it can be undone.
/// @param pos Logical byte offset where @p text should be inserted.
/// @param text Text to insert.
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

/// @brief Remove a span of bytes from the buffer.
/// @details Updates the piece table, informs the line index of the removal, and
///          records the operation for undo support.
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

/// @brief Undo the most recent recorded edit, if any.
/// @details Replays the stored inverse operations through the piece table and
///          line index.  Returns false when no undoable operations remain.
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

/// @brief Reapply the most recently undone edit, if any.
/// @details Mirrors @ref undo by replaying the stored redo operations while
///          keeping the line index synchronised.
/// @return True when an edit was reapplied.
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

/// @brief Materialise the entire buffer into a std::string.
/// @details Used by tests and exporters needing a contiguous snapshot.
/// @return Copy of all buffer contents.
std::string TextBuffer::str() const
{
    return table_.getText(0, table_.size());
}

/// @brief Extract a specific line as a std::string.
/// @details Returns an empty string when @p lineNo falls outside the known
///          range.  The trailing newline is excluded from the result.
/// @param lineNo Zero-based line number to extract.
/// @return Copy of the requested line's contents.
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

/// @brief Iterate over every logical line and supply a @ref LineView.
/// @details Stops early if the visitor returns false.  Callers can use this to
///          render or search the buffer without copying text.
/// @param fn Visitor receiving the line index and view; return false to stop iteration.
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
