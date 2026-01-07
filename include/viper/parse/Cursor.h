//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/parse/Cursor.h
// Purpose: Declare a lightweight text cursor for IL parsers.
// Key invariants: Operates on a string_view without allocating or owning storage.
// Ownership/Lifetime: Views textual buffers owned by the caller; no allocations.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Defines a reusable cursor helper for IL text parsing.
/// @details The cursor provides zero-allocation scanning primitives shared by
///          the function and operand parsers.  It tracks a current source
///          position (line/column) and exposes consumption helpers for
///          whitespace, identifiers, numbers, and keywords without binding the
///          IL parsers to heavier tokenisation machinery.

#pragma once

#include <concepts>
#include <cstddef>
#include <string_view>

namespace viper::parse
{

template <class Predicate>
concept CursorPredicate = requires(Predicate pred, char ch) {
    { pred(ch) } -> std::convertible_to<bool>;
};

/// @brief Represents a line/column pair within a textual buffer.
struct SourcePos
{
    unsigned line = 0;      ///< 1-based line number for diagnostics.
    std::size_t column = 0; ///< 0-based column offset within the current line.
};

/// @brief Lightweight cursor for scanning IL text.
class Cursor
{
  public:
    /// @brief Construct a cursor over @p text starting at @p start.
    Cursor(std::string_view text, SourcePos start) noexcept;

    /// @brief Return the backing view observed by the cursor.
    [[nodiscard]] std::string_view view() const noexcept
    {
        return text_;
    }

    /// @brief View the unconsumed suffix.
    [[nodiscard]] std::string_view remaining() const noexcept
    {
        return text_.substr(index_);
    }

    /// @brief Query whether the cursor has reached the end of the buffer.
    [[nodiscard]] bool atEnd() const noexcept
    {
        return index_ >= text_.size();
    }

    /// @brief Inspect the current character without consuming it.
    [[nodiscard]] char peek() const noexcept;

    /// @brief Report the current line/column location.
    [[nodiscard]] SourcePos pos() const noexcept
    {
        return pos_;
    }

    /// @brief Retrieve the current line number (1-based).
    [[nodiscard]] unsigned line() const noexcept
    {
        return pos_.line;
    }

    /// @brief Retrieve the current column offset (0-based).
    [[nodiscard]] std::size_t column() const noexcept
    {
        return pos_.column;
    }

    /// @brief Retrieve the absolute byte offset within the buffer.
    [[nodiscard]] std::size_t offset() const noexcept
    {
        return index_;
    }

    /// @brief Skip leading whitespace characters.
    void skipWs() noexcept;

    /// @brief Consume the expected character.
    bool consume(char c) noexcept;

    /// @brief Consume @p c if present at the cursor.
    bool consumeIf(char c) noexcept;

    /// @brief Consume characters while @p pred returns true.
    template <CursorPredicate Predicate> std::string_view consumeWhile(Predicate pred) noexcept
    {
        const std::size_t begin = index_;
        while (!atEnd() && pred(peek()))
            advance();
        return text_.substr(begin, index_ - begin);
    }

    /// @brief Consume an identifier token.
    bool consumeIdent(std::string_view &out) noexcept;

    /// @brief Consume an integer-like token.
    bool consumeNumber(std::string_view &out) noexcept;

    /// @brief Consume a keyword literal.
    bool consumeKeyword(std::string_view kw) noexcept;

    /// @brief Advance by a single character if not already at end.
    void advance() noexcept;

    /// @brief Advance to @p offset within the buffer.
    void seek(std::size_t offset) noexcept;

    /// @brief Consume the remainder of the buffer.
    void consumeRest() noexcept
    {
        seek(text_.size());
    }

  private:
    void applyAdvance(char ch) noexcept;

    std::string_view text_;
    std::size_t index_ = 0;
    SourcePos start_{};
    SourcePos pos_{};
};

} // namespace viper::parse
