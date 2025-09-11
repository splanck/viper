// File: src/frontends/basic/Token.hpp
// Purpose: Defines token types for BASIC lexer.
// Key invariants: Tokens carry source locations.
// Ownership/Lifetime: Tokens are value types.
// Links: docs/class-catalog.md
#pragma once

#include "support/source_manager.hpp"
#include <string>

namespace il::frontends::basic
{
/// @brief All token kinds recognized by the BASIC lexer.
/// @details Kinds are organized into markers, literals, identifiers,
/// keywords, operators, and punctuation to guide later parsing stages.
enum class TokenKind
{
    // Markers ---------------------------------------------------------------
    EndOfFile, ///< Reached end of the source buffer.
    EndOfLine, ///< End of a line.

    // Literals --------------------------------------------------------------
    Number, ///< Numeric literal.
    String, ///< String literal.

    // Identifiers -----------------------------------------------------------
    Identifier, ///< User-defined identifier.

    // Keywords --------------------------------------------------------------
    KeywordPrint,
    KeywordLet,
    KeywordIf,
    KeywordThen,
    KeywordElse,
    KeywordElseIf,
    KeywordWhile,
    KeywordWend,
    KeywordFor,
    KeywordTo,
    KeywordStep,
    KeywordNext,
    KeywordGoto,
    KeywordEnd,
    KeywordInput,
    KeywordDim,
    KeywordRandomize,
    KeywordAnd,
    KeywordOr,
    KeywordNot,
    KeywordMod,
    KeywordSqr,
    KeywordAbs,
    KeywordFloor,
    KeywordCeil,
    KeywordSin,
    KeywordCos,
    KeywordPow,
    KeywordRnd,
    KeywordFunction,
    KeywordSub,
    KeywordReturn,

    // Operators -------------------------------------------------------------
    Plus,         ///< '+'.
    Minus,        ///< '-'.
    Star,         ///< '*'.
    Slash,        ///< '/'.
    Backslash,    ///< '\\'.
    Equal,        ///< '='.
    NotEqual,     ///< '<>'.
    Less,         ///< '<'.
    LessEqual,    ///< '<='.
    Greater,      ///< '>'.
    GreaterEqual, ///< '>='.

    // Punctuation -----------------------------------------------------------
    LParen,    ///< '('.
    RParen,    ///< ')'.
    Comma,     ///< ','.
    Semicolon, ///< ';'.
    Colon,     ///< ':'.
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
