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

/// @brief Concept constraining predicates accepted by Cursor::consumeWhile.
/// @details A CursorPredicate is any callable that accepts a single char and
///          returns a type convertible to bool.
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
    /// @param text The full text buffer to scan (not owned; must outlive the cursor).
    /// @param start Initial source position (line/column) for diagnostics.
    Cursor(std::string_view text, SourcePos start) noexcept;

    /// @brief Return the backing view observed by the cursor.
    /// @return The full text buffer originally provided to the constructor.
    [[nodiscard]] std::string_view view() const noexcept
    {
        return text_;
    }

    /// @brief View the unconsumed suffix.
    /// @return A string_view of the text from the current position to the end.
    [[nodiscard]] std::string_view remaining() const noexcept
    {
        return text_.substr(index_);
    }

    /// @brief Query whether the cursor has reached the end of the buffer.
    /// @return true if no characters remain to be consumed.
    [[nodiscard]] bool atEnd() const noexcept
    {
        return index_ >= text_.size();
    }

    /// @brief Inspect the current character without consuming it.
    /// @return The character at the current position, or '\0' if at end.
    [[nodiscard]] char peek() const noexcept;

    /// @brief Report the current line/column location.
    /// @return The current source position as a SourcePos struct.
    [[nodiscard]] SourcePos pos() const noexcept
    {
        return pos_;
    }

    /// @brief Retrieve the current line number (1-based).
    /// @return Current line number starting from 1.
    [[nodiscard]] unsigned line() const noexcept
    {
        return pos_.line;
    }

    /// @brief Retrieve the current column offset (0-based).
    /// @return Current column offset starting from 0.
    [[nodiscard]] std::size_t column() const noexcept
    {
        return pos_.column;
    }

    /// @brief Retrieve the absolute byte offset within the buffer.
    /// @return Zero-based byte offset from the start of the text buffer.
    [[nodiscard]] std::size_t offset() const noexcept
    {
        return index_;
    }

    /// @brief Skip leading whitespace characters.
    void skipWs() noexcept;

    /// @brief Consume the expected character, asserting its presence.
    /// @param c Character expected at the current position.
    /// @return true if @p c was found and consumed; false otherwise.
    bool consume(char c) noexcept;

    /// @brief Consume @p c if present at the cursor; no-op otherwise.
    /// @param c Character to conditionally consume.
    /// @return true if @p c was found and consumed; false if not present.
    bool consumeIf(char c) noexcept;

    /// @brief Consume characters while @p pred returns true.
    /// @tparam Predicate A callable satisfying CursorPredicate.
    /// @param pred Predicate invoked for each character; consumption stops when it returns false.
    /// @return A string_view spanning all consecutively consumed characters.
    template <CursorPredicate Predicate> std::string_view consumeWhile(Predicate pred) noexcept
    {
        const std::size_t begin = index_;
        while (!atEnd() && pred(peek()))
            advance();
        return text_.substr(begin, index_ - begin);
    }

    /// @brief Consume an identifier token.
    /// @param[out] out Set to the consumed identifier text on success.
    /// @return true if an identifier was consumed; false if the cursor does not
    ///         point at a valid identifier start character.
    bool consumeIdent(std::string_view &out) noexcept;

    /// @brief Consume an integer-like token (digits, optional leading sign).
    /// @param[out] out Set to the consumed numeric text on success.
    /// @return true if a number was consumed; false otherwise.
    bool consumeNumber(std::string_view &out) noexcept;

    /// @brief Consume a keyword literal if it matches at the current position.
    /// @param kw The keyword to match (exact, case-sensitive).
    /// @return true if @p kw was matched and consumed; false otherwise.
    bool consumeKeyword(std::string_view kw) noexcept;

    /// @brief Advance by a single character if not already at end.
    void advance() noexcept;

    /// @brief Advance to @p offset within the buffer.
    /// @param offset Absolute byte offset to seek to; clamped to buffer size.
    void seek(std::size_t offset) noexcept;

    /// @brief Consume the remainder of the buffer.
    void consumeRest() noexcept
    {
        seek(text_.size());
    }

  private:
    /// @brief Update line/column tracking after consuming character @p ch.
    /// @param ch The character just consumed (newline triggers line increment).
    void applyAdvance(char ch) noexcept;

    std::string_view text_;
    std::size_t index_ = 0;
    SourcePos start_{};
    SourcePos pos_{};
};

} // namespace viper::parse
