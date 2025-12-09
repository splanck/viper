//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/LexerBase.hpp
// Purpose: Common lexer cursor management utilities.
//
// This header provides inline helper functions for lexer cursor management
// that are shared across language frontends. Instead of using inheritance,
// these are provided as inline utilities that can be composed into lexers.
//
// Key Invariants:
//   - Position tracking maintains 1-based line and column numbers
//   - EOF is indicated by returning '\0' from peek operations
//   - Newlines increment line and reset column to 1
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace il::frontends::common::lexer_base
{

/// @brief CRTP base class for lexer cursor management.
/// @details Provides peek(), get(), eof(), and location tracking.
/// Derived class must provide source() returning std::string_view.
///
/// Usage:
///   class MyLexer : public LexerCursor<MyLexer> {
///       std::string_view source() const { return src_; }
///   };
template <typename Derived>
class LexerCursor
{
  public:
    /// @brief Construct with initial file ID.
    explicit LexerCursor(uint32_t fileId) : fileId_(fileId) {}

    /// @brief Peek at the current character without consuming it.
    /// @return The current character, or '\0' if at end of source.
    [[nodiscard]] char peek() const
    {
        auto src = static_cast<const Derived *>(this)->source();
        return pos_ < src.size() ? src[pos_] : '\0';
    }

    /// @brief Peek at a character ahead of current position.
    /// @param offset Number of characters ahead to look.
    /// @return The character at offset, or '\0' if beyond end.
    [[nodiscard]] char peek(std::size_t offset) const
    {
        auto src = static_cast<const Derived *>(this)->source();
        std::size_t idx = pos_ + offset;
        return idx < src.size() ? src[idx] : '\0';
    }

    /// @brief Consume and return the current character.
    /// @return The consumed character, or '\0' if at end of source.
    char get()
    {
        auto src = static_cast<const Derived *>(this)->source();
        if (pos_ >= src.size())
            return '\0';
        char c = src[pos_++];
        if (c == '\n')
        {
            line_++;
            column_ = 1;
        }
        else
        {
            column_++;
        }
        return c;
    }

    /// @brief Check whether the lexer has reached the end of the source.
    /// @return True if no characters remain, otherwise false.
    [[nodiscard]] bool eof() const
    {
        return pos_ >= static_cast<const Derived *>(this)->source().size();
    }

    /// @brief Get the current position in the source.
    [[nodiscard]] std::size_t position() const noexcept { return pos_; }

    /// @brief Get the current line number (1-based).
    [[nodiscard]] uint32_t line() const noexcept { return line_; }

    /// @brief Get the current column number (1-based).
    [[nodiscard]] uint32_t column() const noexcept { return column_; }

    /// @brief Get the file ID.
    [[nodiscard]] uint32_t fileId() const noexcept { return fileId_; }

  protected:
    std::size_t pos_{0};   ///< Current position in source.
    uint32_t line_{1};     ///< 1-based line number.
    uint32_t column_{1};   ///< 1-based column number.
    uint32_t fileId_;      ///< File identifier.
};

/// @brief Skip whitespace characters (space, tab, CR).
/// @details Advances past horizontal whitespace, leaving newlines in place.
/// @tparam Lexer A lexer type with peek(), get(), eof() methods.
template <typename Lexer>
inline void skipHorizontalWhitespace(Lexer &lex)
{
    while (!lex.eof())
    {
        char c = lex.peek();
        if (c == ' ' || c == '\t' || c == '\r')
            lex.get();
        else
            break;
    }
}

/// @brief Skip all whitespace characters including newlines.
/// @tparam Lexer A lexer type with peek(), get(), eof() methods.
template <typename Lexer>
inline void skipAllWhitespace(Lexer &lex)
{
    while (!lex.eof())
    {
        char c = lex.peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            lex.get();
        else
            break;
    }
}

/// @brief Skip a line (until newline or EOF).
/// @details Consumes characters until a newline is seen (but does not consume the newline).
/// @tparam Lexer A lexer type with peek(), get(), eof() methods.
template <typename Lexer>
inline void skipToEndOfLine(Lexer &lex)
{
    while (!lex.eof() && lex.peek() != '\n')
        lex.get();
}

/// @brief Skip a line including the trailing newline.
/// @tparam Lexer A lexer type with peek(), get(), eof() methods.
template <typename Lexer>
inline void skipLine(Lexer &lex)
{
    while (!lex.eof() && lex.peek() != '\n')
        lex.get();
    if (!lex.eof() && lex.peek() == '\n')
        lex.get();
}

} // namespace il::frontends::common::lexer_base
