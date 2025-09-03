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

enum class TokenKind
{
    EndOfFile,
    EndOfLine,
    Number,
    String,
    Identifier,
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
    KeywordAnd,
    KeywordOr,
    KeywordNot,
    KeywordMod,
    KeywordSqr,
    KeywordAbs,
    KeywordFloor,
    KeywordCeil,
    Plus,
    Minus,
    Star,
    Slash,
    Backslash,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LParen,
    RParen,
    Comma,
    Semicolon,
    Colon,
};

struct Token
{
    TokenKind kind;
    std::string lexeme;
    il::support::SourceLoc loc;
};

const char *tokenKindToString(TokenKind k);

} // namespace il::frontends::basic
