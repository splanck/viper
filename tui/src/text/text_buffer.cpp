// tui/src/text/text_buffer.cpp
// @brief TextBuffer orchestration of PieceTable, LineIndex, and EditHistory helpers.
// @invariant Helper state stays synchronized through span change callbacks.
// @ownership TextBuffer owns helpers and returns copied strings to callers.

#include "tui/text/text_buffer.hpp"

#include <utility>

namespace viper::tui::text
{
TextBuffer::LineView::LineView(const PieceTable &table, std::size_t offset, std::size_t length)
    : table_(table), offset_(offset), length_(length)
{
}

std::size_t TextBuffer::LineView::offset() const
{
    return offset_;
}

std::size_t TextBuffer::LineView::length() const
{
    return length_;
}

/// @copydoc viper::tui::text::TextBuffer::LineView::forEachSegment
void TextBuffer::LineView::forEachSegment(SegmentVisitor fn) const
{
    table_.forEachSegment(offset_, length_, fn);
}

void TextBuffer::load(std::string text)
{
    auto change = table_.load(std::move(text));
    line_index_.reset(change.insertedText());
    history_.clear();
}

std::size_t TextBuffer::size() const
{
    return table_.size();
}

std::size_t TextBuffer::lineCount() const
{
    return line_index_.count();
}

std::size_t TextBuffer::lineStart(std::size_t lineNo) const
{
    if (lineNo >= line_index_.count())
    {
        return table_.size();
    }
    return line_index_.start(lineNo);
}

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

std::size_t TextBuffer::lineOffset(std::size_t lineNo) const
{
    return lineStart(lineNo);
}

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

TextBuffer::LineView TextBuffer::lineView(std::size_t lineNo) const
{
    return LineView(table_, lineOffset(lineNo), lineLength(lineNo));
}

void TextBuffer::beginTxn()
{
    history_.beginTxn();
}

void TextBuffer::endTxn()
{
    history_.endTxn();
}

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

std::string TextBuffer::str() const
{
    return table_.getText(0, table_.size());
}

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
