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

/// @brief Construct a cursor over the provided source buffer.
/// @details Initialises indices and current position so traversal starts at the
///          supplied @p start location.
/// @param text Source text to traverse.
/// @param start Source position representing the beginning of the buffer.
Cursor::Cursor(std::string_view text, SourcePos start) noexcept
    : text_(text), index_(0), start_(start), pos_(start)
{
}

/// @brief Inspect the current character without advancing.
/// @details Returns '\0' when the cursor is at the end to simplify callers that
///          expect a sentinel terminator.
/// @return Character at the current cursor position or '\0' at end.
char Cursor::peek() const noexcept
{
    return atEnd() ? '\0' : text_[index_];
}

/// @brief Update the tracked source position after consuming @p ch.
/// @details Handles newlines by incrementing the line counter and resetting the
///          column; other characters simply increment the column.
/// @param ch Character that was consumed.
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

/// @brief Consume the current character and update the position.
/// @details Safely returns when already at end-of-input.
void Cursor::advance() noexcept
{
    if (atEnd())
        return;
    const char ch = text_[index_++];
    applyAdvance(ch);
}

/// @brief Advance past ASCII whitespace characters.
/// @details Uses @ref std::isspace to recognise whitespace and stops at the
///          first non-whitespace character.
void Cursor::skipWs() noexcept
{
    while (!atEnd() && std::isspace(static_cast<unsigned char>(peek())))
        advance();
}

/// @brief Consume @p c when it matches the current character.
/// @details Returns @c false when the current character differs, leaving the
///          cursor untouched.
/// @param c Character to consume.
/// @return True when the character matched and was consumed.
bool Cursor::consume(char c) noexcept
{
    if (peek() != c)
        return false;
    advance();
    return true;
}

/// @brief Conditionally consume @p c and report success.
/// @details Similar to @ref consume but implemented without calling
///          @ref skipWs. The helper keeps the cursor untouched when the
///          character does not match.
/// @param c Character to consume.
/// @return True when @p c was consumed.
bool Cursor::consumeIf(char c) noexcept
{
    if (peek() == c)
    {
        advance();
        return true;
    }
    return false;
}

/// @brief Consume an identifier token from the stream.
/// @details Skips leading whitespace, then reads a leading alphabetic, '_', or
///          '.' character followed by alphanumeric, '_', '.', or '$'. On success
///          @p out references the identifier substring.
/// @param[out] out View that will reference the consumed identifier.
/// @return True when an identifier was consumed.
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

/// @brief Consume a signed integer literal.
/// @details Accepts an optional leading '+' or '-' followed by one or more
///          digits. On failure the cursor rewinds to its original position.
/// @param[out] out View that references the consumed number literal.
/// @return True when a number literal was consumed.
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

/// @brief Consume a fixed keyword string.
/// @details Skips leading whitespace and compares the next characters with @p kw.
///          On success the cursor advances past the keyword.
/// @param kw Keyword that should be matched.
/// @return True when the keyword was consumed.
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

/// @brief Move the cursor to @p offset within the source buffer.
/// @details Adjusts both the byte index and the tracked source position. When
///          seeking backwards the routine recomputes the position from the start
///          of the buffer to keep line/column data accurate.
/// @param offset Zero-based index into the source buffer.
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
