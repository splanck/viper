// File: src/frontends/basic/Token.hpp
// Purpose: Defines token types for BASIC lexer.
// Key invariants: Tokens carry source locations.
// Ownership/Lifetime: Tokens are value types.
// Links: docs/codemap.md
#pragma once

#include "support/source_location.hpp"
#include <string>

namespace il::frontends::basic
{
/// @brief All token kinds recognized by the BASIC lexer.
/// @details Kinds are organized into markers, literals, identifiers,
/// keywords, operators, and punctuation to guide later parsing stages.
enum class TokenKind
{
#define TOKEN(K, S) K,
#include "frontends/basic/TokenKinds.def"
#undef TOKEN

    Count, ///< Total number of token kinds (sentinel, not a real token).
};

/// @brief A lexical token produced by the BASIC lexer.
/// @details Tokens are value types that own their lexeme and track the
/// starting source location.
struct Token
{
    /// @brief Classification of this token.
    TokenKind kind;

    /// @brief Exact character sequence for this token.
    /// @ownership Owned by this token.
    std::string lexeme;

    /// @brief Source location where the token begins.
    /// @ownership Borrowed from the source manager.
    il::support::SourceLoc loc;
};

const char *tokenKindToString(TokenKind k);

} // namespace il::frontends::basic
