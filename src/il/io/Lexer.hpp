// File: src/il/io/Lexer.hpp
// Purpose: Declares lexical helper utilities for IL text parsing.
// Key invariants: Functions operate on ASCII-compatible strings.
// Ownership/Lifetime: Returns new strings; does not own provided streams.
// Links: docs/il-spec.md
#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace il::io
{

/// @brief Helper providing string tokenisation utilities for the IL parser.
class Lexer
{
  public:
    /// @brief Remove leading and trailing whitespace from @p text.
    /// @param text Input text possibly containing whitespace padding.
    /// @return A trimmed copy of the input string.
    [[nodiscard]] static std::string trim(std::string_view text);

    /// @brief Extract the next token from a comma-delimited stream.
    /// @param stream Source stream advanced past the returned token.
    /// @return Token with trailing comma stripped if present.
    [[nodiscard]] static std::string nextToken(std::istringstream &stream);

    /// @brief Split comma-separated text into trimmed tokens.
    /// @param text Sequence containing comma delimiters.
    /// @return Vector of tokens in textual order with whitespace removed.
    [[nodiscard]] static std::vector<std::string> splitCommaSeparated(std::string_view text);
};

} // namespace il::io
