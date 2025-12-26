//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Token structure and TokenKind enumeration used by the
// BASIC frontend lexer.
//
// Tokens are the fundamental units produced by lexical analysis and consumed by
// the parser. Each token represents a classified lexeme (sequence of characters)
// from the source code, along with its source location for diagnostic purposes.
//
// Token Categories:
// - Markers: EOF, Unknown, Error
// - Literals: Integer, Float, String
// - Identifiers: Plain identifiers and those with type suffixes (%, &, !, #, $)
// - Keywords: Language keywords (IF, THEN, FOR, DIM, SUB, FUNCTION, etc.)
// - Operators: Arithmetic (+, -, *, /, ^), comparison (=, <, >, <=, >=, <>),
//   logical (AND, OR, NOT), string (&)
// - Punctuation: Parentheses, comma, colon, semicolon
// - Structure: Newline (significant in BASIC's line-oriented syntax)
//
// Design Notes:
// - Tokens are value types (copyable, movable) and own their lexeme string
// - Source locations are stored as SourceLoc references to the source manager
// - The TokenKind enumeration is generated from TokenKinds.def to enable
//   table-driven parsing and easy maintenance
// - Type suffixes (%, &, !, #, $) are part of identifier tokens, preserving
//   BASIC's implicit type declaration semantics
//
// Integration:
// - Produced by: Lexer::next()
// - Consumed by: Parser for syntax analysis and AST construction
// - Used throughout: Diagnostic messages for error reporting
//
//===----------------------------------------------------------------------===//
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
