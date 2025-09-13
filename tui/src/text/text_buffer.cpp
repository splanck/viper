// tui/src/text/text_buffer.cpp
// @brief Piece table text buffer implementation with undo/redo and line index.
// @invariant Edits modify piece metadata only; original text is never copied.
// @ownership TextBuffer owns buffers; undo/redo stacks own their stored text.

#include "tui/text/text_buffer.hpp"

#include <algorithm>
#include <cassert>

namespace viper::tui::text
{
void TextBuffer::load(std::string text)
{
    original_ = std::move(text);
    add_.clear();
    pieces_.clear();
    if (!original_.empty())
    {
        pieces_.push_back(Piece{BufferKind::Original, 0, original_.size()});
        size_ = original_.size();
    }
    else
    {
        size_ = 0;
    }
    line_starts_.clear();
    line_starts_.push_back(0);
    for (size_t i = 0; i < original_.size(); ++i)
    {
        if (original_[i] == '\n')
        {
            line_starts_.push_back(i + 1);
        }
    }
    undo_stack_.clear();
    redo_stack_.clear();
}

void TextBuffer::beginTxn()
{
    in_txn_ = true;
    txn_ops_.clear();
}

void TextBuffer::endTxn()
{
    in_txn_ = false;
    if (!txn_ops_.empty())
    {
        undo_stack_.push_back(txn_ops_);
        redo_stack_.clear();
        txn_ops_.clear();
    }
}

std::list<TextBuffer::Piece>::iterator TextBuffer::findPiece(size_t pos, size_t &offset)
{
    size_t idx = 0;
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

void TextBuffer::insertInternal(size_t pos, std::string_view text)
{
    if (text.empty())
    {
        return;
    }
    size_t offset = 0;
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
    updateLinesInsert(pos, text);
}

void TextBuffer::insert(size_t pos, std::string_view text)
{
    insertInternal(pos, text);
    if (in_txn_)
    {
        txn_ops_.push_back(Op{OpType::Insert, pos, std::string(text)});
    }
    else
    {
        undo_stack_.push_back(Txn{Op{OpType::Insert, pos, std::string(text)}});
        redo_stack_.clear();
    }
}

std::string TextBuffer::getText(size_t pos, size_t len) const
{
    std::string out;
    out.reserve(len);
    size_t idx = 0;
    for (auto it = pieces_.begin(); it != pieces_.end() && len > 0; ++it)
    {
        if (pos >= idx + it->length)
        {
            idx += it->length;
            continue;
        }
        size_t start_in_piece = pos > idx ? pos - idx : 0;
        size_t take = std::min(it->length - start_in_piece, len);
        const std::string &buf = it->buf == BufferKind::Add ? add_ : original_;
        out.append(buf.substr(it->start + start_in_piece, take));
        pos += take;
        len -= take;
        idx += it->length;
    }
    return out;
}

void TextBuffer::eraseInternal(size_t pos, size_t len)
{
    if (len == 0)
    {
        return;
    }
    size_t offset = 0;
    auto it = findPiece(pos, offset);
    size_t remaining = len;

    if (it == pieces_.end())
    {
        return;
    }

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
    size_ -= len - remaining;
    updateLinesErase(pos, len - remaining);
}

void TextBuffer::erase(size_t pos, size_t len)
{
    if (len == 0)
    {
        return;
    }
    std::string removed = getText(pos, len);
    eraseInternal(pos, len);
    if (in_txn_)
    {
        txn_ops_.push_back(Op{OpType::Erase, pos, removed});
    }
    else
    {
        undo_stack_.push_back(Txn{Op{OpType::Erase, pos, removed}});
        redo_stack_.clear();
    }
}

bool TextBuffer::undo()
{
    if (undo_stack_.empty())
    {
        return false;
    }
    Txn txn = undo_stack_.back();
    undo_stack_.pop_back();
    for (auto it = txn.rbegin(); it != txn.rend(); ++it)
    {
        if (it->type == OpType::Insert)
        {
            eraseInternal(it->pos, it->text.size());
        }
        else
        {
            insertInternal(it->pos, it->text);
        }
    }
    redo_stack_.push_back(std::move(txn));
    return true;
}

bool TextBuffer::redo()
{
    if (redo_stack_.empty())
    {
        return false;
    }
    Txn txn = redo_stack_.back();
    redo_stack_.pop_back();
    for (const auto &op : txn)
    {
        if (op.type == OpType::Insert)
        {
            insertInternal(op.pos, op.text);
        }
        else
        {
            eraseInternal(op.pos, op.text.size());
        }
    }
    undo_stack_.push_back(std::move(txn));
    return true;
}

std::string TextBuffer::str() const
{
    return getText(0, size_);
}

std::string TextBuffer::getLine(size_t lineNo) const
{
    if (lineNo >= line_starts_.size())
    {
        return {};
    }
    size_t start = line_starts_[lineNo];
    size_t end = (lineNo + 1 < line_starts_.size()) ? line_starts_[lineNo + 1] - 1 : size_;
    if (end < start)
    {
        end = start;
    }
    return getText(start, end - start);
}

void TextBuffer::updateLinesInsert(size_t pos, std::string_view text)
{
    auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), pos);
    size_t idx = it - line_starts_.begin();
    for (size_t i = idx; i < line_starts_.size(); ++i)
    {
        line_starts_[i] += text.size();
    }
    size_t insert_idx = idx;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\n')
        {
            line_starts_.insert(line_starts_.begin() + insert_idx, pos + i + 1);
            ++insert_idx;
        }
    }
}

void TextBuffer::updateLinesErase(size_t pos, size_t len)
{
    auto start_it = std::lower_bound(line_starts_.begin() + 1, line_starts_.end(), pos);
    auto end_it = std::lower_bound(start_it, line_starts_.end(), pos + len);
    size_t start_idx = start_it - line_starts_.begin();
    size_t end_idx = end_it - line_starts_.begin();
    line_starts_.erase(line_starts_.begin() + start_idx, line_starts_.begin() + end_idx);
    for (size_t i = start_idx; i < line_starts_.size(); ++i)
    {
        line_starts_[i] -= len;
    }
}

} // namespace viper::tui::text
