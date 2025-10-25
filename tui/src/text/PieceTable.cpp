// tui/src/text/PieceTable.cpp
// @brief PieceTable implementation providing span change callbacks.
// @invariant Piece metadata splits only when necessary; buffers hold actual bytes.
// @ownership Change instances own copied span payloads for safe callbacks.

#include "tui/text/PieceTable.hpp"

#include <algorithm>
#include <utility>

namespace viper::tui::text
{
void PieceTable::Change::recordInsert(std::size_t pos, std::string text)
{
    if (text.empty())
    {
        insert_span_.reset();
        return;
    }
    insert_span_ = Span{pos, std::move(text)};
}

void PieceTable::Change::recordErase(std::size_t pos, std::string text)
{
    if (text.empty())
    {
        erase_span_.reset();
        return;
    }
    erase_span_ = Span{pos, std::move(text)};
}

void PieceTable::Change::notifyInsert(const Callback &cb) const
{
    if (cb && insert_span_)
    {
        cb(insert_span_->pos, insert_span_->text);
    }
}

void PieceTable::Change::notifyErase(const Callback &cb) const
{
    if (cb && erase_span_)
    {
        cb(erase_span_->pos, erase_span_->text);
    }
}

bool PieceTable::Change::hasInsert() const
{
    return insert_span_.has_value();
}

bool PieceTable::Change::hasErase() const
{
    return erase_span_.has_value();
}

std::size_t PieceTable::Change::insertPos() const
{
    return insert_span_ ? insert_span_->pos : 0U;
}

std::size_t PieceTable::Change::erasePos() const
{
    return erase_span_ ? erase_span_->pos : 0U;
}

std::string_view PieceTable::Change::insertedText() const
{
    return insert_span_ ? std::string_view(insert_span_->text) : std::string_view{};
}

std::string_view PieceTable::Change::erasedText() const
{
    return erase_span_ ? std::string_view(erase_span_->text) : std::string_view{};
}

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

std::size_t PieceTable::size() const
{
    return size_;
}

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
