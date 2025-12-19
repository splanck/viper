//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Token.hpp
// Purpose: Token kinds and token structure for the ViperLang lexer.
// Key invariants: Each token has a kind, location, and optional text/value.
// Ownership/Lifetime: Tokens own their string data (text field).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diagnostics.hpp"
#include <cstdint>
#include <string>

namespace il::frontends::viperlang
{

/// @brief Token kinds for ViperLang.
/// @details Organized into sections: special, keywords, operators, punctuation.
enum class TokenKind
{
    // Special tokens
    Eof,
    Error,

    // Literals
    IntegerLiteral, // 42, 0xFF, 0b1010
    NumberLiteral,  // 3.14, 6.02e23
    StringLiteral,  // "hello"
    Identifier,     // user-defined names

    // Keywords - Types (3)
    KwValue,     // value
    KwEntity,    // entity
    KwInterface, // interface

    // Keywords - Modifiers (5)
    KwFinal,    // final
    KwExpose,   // expose
    KwHide,     // hide
    KwOverride, // override
    KwWeak,     // weak

    // Keywords - Declarations (6)
    KwModule, // module
    KwImport, // import
    KwFunc,   // func
    KwReturn, // return
    KwVar,    // var
    KwNew,    // new

    // Keywords - Control Flow (11)
    KwIf,       // if
    KwElse,     // else
    KwLet,      // let
    KwMatch,    // match
    KwWhile,    // while
    KwFor,      // for
    KwIn,       // in
    KwIs,       // is
    KwGuard,    // guard
    KwBreak,    // break
    KwContinue, // continue

    // Keywords - Inheritance (4)
    KwExtends,    // extends
    KwImplements, // implements
    KwSelf,       // self
    KwSuper,      // super
    KwAs,         // as

    // Keywords - Literals (3)
    KwTrue,  // true
    KwFalse, // false
    KwNull,  // null

    // Operators
    Plus,             // +
    Minus,            // -
    Star,             // *
    Slash,            // /
    Percent,          // %
    Ampersand,        // &
    Pipe,             // |
    Caret,            // ^
    Tilde,            // ~
    Bang,             // !
    Equal,            // =
    EqualEqual,       // ==
    NotEqual,         // !=
    Less,             // <
    LessEqual,        // <=
    Greater,          // >
    GreaterEqual,     // >=
    AmpAmp,           // &&
    PipePipe,         // ||
    Arrow,            // ->
    FatArrow,         // =>
    Question,         // ?
    QuestionQuestion, // ??
    QuestionDot,      // ?.
    Dot,              // .
    DotDot,           // ..
    DotDotEqual,      // ..=
    Colon,            // :
    Semicolon,        // ;
    Comma,            // ,
    At,               // @

    // Brackets
    LParen,   // (
    RParen,   // )
    LBracket, // [
    RBracket, // ]
    LBrace,   // {
    RBrace,   // }
};

/// @brief Convert TokenKind to string for debugging.
const char *tokenKindToString(TokenKind kind);

/// @brief Token structure holding kind, location, and value.
struct Token
{
    TokenKind kind = TokenKind::Eof;
    il::support::SourceLoc loc{};
    std::string text; // Original source text

    // Literal values
    int64_t intValue = 0;
    double floatValue = 0.0;
    std::string stringValue; // Unescaped string content

    /// @brief Check if this token is of the given kind.
    bool is(TokenKind k) const
    {
        return kind == k;
    }

    /// @brief Check if this token is one of the given kinds.
    template <typename... Kinds> bool isOneOf(Kinds... kinds) const
    {
        return (is(kinds) || ...);
    }

    /// @brief Check if this token is a keyword.
    bool isKeyword() const;
};

} // namespace il::frontends::viperlang
