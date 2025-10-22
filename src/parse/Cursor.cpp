//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/parse/Cursor.cpp
// Purpose: Provide out-of-line helpers for the parse::Cursor utility.
// Key invariants: Mirrors the behaviour expected by FunctionParser and OperandParser.
// Ownership/Lifetime: Operates on caller-owned string_view buffers.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the lightweight parsing cursor shared by IL parsers.

#include "viper/parse/Cursor.h"

#include <cctype>

namespace viper::parse
{

Cursor::Cursor(std::string_view text, SourcePos start) noexcept
    : text_(text), index_(0), start_(start), pos_(start)
{
}

char Cursor::peek() const noexcept
{
    return atEnd() ? '\0' : text_[index_];
}

void Cursor::applyAdvance(char ch) noexcept
{
    if (ch == '\n')
    {
        ++pos_.line;
        pos_.column = 0;
    }
    else
    {
        ++pos_.column;
    }
}

void Cursor::advance() noexcept
{
    if (atEnd())
        return;
    const char ch = text_[index_++];
    applyAdvance(ch);
}

void Cursor::skipWs() noexcept
{
    while (!atEnd() && std::isspace(static_cast<unsigned char>(peek())))
        advance();
}

bool Cursor::consume(char c) noexcept
{
    if (peek() != c)
        return false;
    advance();
    return true;
}

bool Cursor::consumeIf(char c) noexcept
{
    if (peek() == c)
    {
        advance();
        return true;
    }
    return false;
}

bool Cursor::consumeIdent(std::string_view &out) noexcept
{
    skipWs();
    if (atEnd())
        return false;

    auto isStart = [](unsigned char ch) { return std::isalpha(ch) || ch == '_' || ch == '.'; };
    auto isBody = [](unsigned char ch)
    { return std::isalnum(ch) || ch == '_' || ch == '.' || ch == '$'; };

    const unsigned char first = static_cast<unsigned char>(peek());
    if (!isStart(first))
        return false;

    const std::size_t begin = index_;
    advance();
    while (!atEnd() && isBody(static_cast<unsigned char>(peek())))
        advance();
    out = text_.substr(begin, index_ - begin);
    return true;
}

bool Cursor::consumeNumber(std::string_view &out) noexcept
{
    skipWs();
    if (atEnd())
        return false;

    const std::size_t begin = index_;
    if (peek() == '+' || peek() == '-')
        advance();

    const std::size_t digitsBegin = index_;
    while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())))
        advance();

    if (index_ == digitsBegin)
    {
        seek(begin);
        return false;
    }

    out = text_.substr(begin, index_ - begin);
    return true;
}

bool Cursor::consumeKeyword(std::string_view kw) noexcept
{
    skipWs();
    if (kw.empty())
        return false;
    if (text_.substr(index_, kw.size()) != kw)
        return false;
    seek(index_ + kw.size());
    return true;
}

void Cursor::seek(std::size_t offset) noexcept
{
    if (offset > text_.size())
        offset = text_.size();

    if (offset >= index_)
    {
        while (index_ < offset)
            advance();
        return;
    }

    index_ = 0;
    pos_ = start_;
    while (index_ < offset)
        advance();
}

} // namespace viper::parse
