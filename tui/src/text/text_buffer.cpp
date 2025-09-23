// tui/src/text/text_buffer.cpp
// @brief TextBuffer orchestration of PieceTable, LineIndex, and EditHistory helpers.
// @invariant Helper state stays synchronized through span change callbacks.
// @ownership TextBuffer owns helpers and returns copied strings to callers.

#include "tui/text/text_buffer.hpp"

#include <utility>

namespace viper::tui::text
{
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
    change.notifyInsert([this](std::size_t changePos, std::string_view inserted) {
        line_index_.onInsert(changePos, inserted);
    });
    if (change.hasInsert())
    {
        history_.recordInsert(change.insertPos(), std::string(change.insertedText()));
    }
}

void TextBuffer::erase(std::size_t pos, std::size_t len)
{
    auto change = table_.eraseInternal(pos, len);
    change.notifyErase([this](std::size_t changePos, std::string_view removed) {
        line_index_.onErase(changePos, removed);
    });
    if (change.hasErase())
    {
        history_.recordErase(change.erasePos(), std::string(change.erasedText()));
    }
}

bool TextBuffer::undo()
{
    return history_.undo([this](const EditHistory::Op &op) {
        if (op.type == EditHistory::OpType::Insert)
        {
            auto change = table_.eraseInternal(op.pos, op.text.size());
            change.notifyErase([this](std::size_t changePos, std::string_view removed) {
                line_index_.onErase(changePos, removed);
            });
        }
        else
        {
            auto change = table_.insertInternal(op.pos, op.text);
            change.notifyInsert([this](std::size_t changePos, std::string_view inserted) {
                line_index_.onInsert(changePos, inserted);
            });
        }
    });
}

bool TextBuffer::redo()
{
    return history_.redo([this](const EditHistory::Op &op) {
        if (op.type == EditHistory::OpType::Insert)
        {
            auto change = table_.insertInternal(op.pos, op.text);
            change.notifyInsert([this](std::size_t changePos, std::string_view inserted) {
                line_index_.onInsert(changePos, inserted);
            });
        }
        else
        {
            auto change = table_.eraseInternal(op.pos, op.text.size());
            change.notifyErase([this](std::size_t changePos, std::string_view removed) {
                line_index_.onErase(changePos, removed);
            });
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
    std::size_t end = (lineNo + 1 < line_index_.count()) ? line_index_.start(lineNo + 1) - 1 : table_.size();
    if (end < start)
    {
        end = start;
    }
    return table_.getText(start, end - start);
}
} // namespace viper::tui::text
