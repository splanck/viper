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
    // Markers ---------------------------------------------------------------
    Unknown,   ///< Unrecognized or invalid token.
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
    KeywordLoop,
    KeywordFor,
    KeywordTo,
    KeywordStep,
    KeywordNext,
    KeywordOn,
    KeywordError,
    KeywordGoto,
    KeywordEnd,
    KeywordExit,
    KeywordInput,
    KeywordDim,
    KeywordDo,
    KeywordRedim,
    KeywordAs,
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
    KeywordResume,
    KeywordReturn,
    KeywordLbound,
    KeywordUbound,
    KeywordUntil,

    // Operators -------------------------------------------------------------
    Plus,         ///< '+'.
    Minus,        ///< '-'.
    Star,         ///< '*'.
    Slash,        ///< '/'.
    Backslash,    ///< '\\'.
    Caret,        ///< '^'.
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

    // Additional keywords and literals (appended to preserve existing values).
    KeywordBoolean, ///< 'BOOLEAN'.
    KeywordTrue,    ///< 'TRUE'.
    KeywordFalse,   ///< 'FALSE'.
    KeywordAndAlso, ///< 'ANDALSO'.
    KeywordOrElse,  ///< 'ORELSE'.

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
