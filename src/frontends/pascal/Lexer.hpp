//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lexer.hpp
// Purpose: Declares the Pascal lexer for tokenizing Viper Pascal source code.
// Key invariants: Case-insensitive keywords/identifiers; line/column tracking.
// Ownership/Lifetime: Lexer borrows source buffer; DiagnosticEngine borrowed.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diagnostics.hpp"
#include "support/source_location.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace il::frontends::pascal
{

/// @brief All token kinds recognized by the Pascal lexer.
enum class TokenKind
{
    // Markers
    Eof,   ///< End of file
    Error, ///< Lexical error (invalid character, unterminated string, etc.)

    // Literals
    IntegerLiteral, ///< Integer literal (decimal or hex)
    RealLiteral,    ///< Floating-point literal
    StringLiteral,  ///< String literal in single quotes

    // Identifiers
    Identifier, ///< User identifier or predefined identifier

    // Keywords (reserved words)
    KwAnd,
    KwArray,
    KwBegin,
    KwBreak,
    KwCase,
    KwClass,
    KwConst,
    KwConstructor,
    KwContinue,
    KwDestructor,
    KwDiv,
    KwDo,
    KwDownto,
    KwElse,
    KwEnd,
    KwExit,
    KwExcept,
    KwFinally,
    KwFor,
    KwFunction,
    KwIf,
    KwImplementation,
    KwIn,
    KwIs,
    KwInherited,
    KwAbstract,
    KwInterface,
    KwMod,
    KwNil,
    KwNot,
    KwOf,
    KwOn,
    KwOr,
    KwOverride,
    KwPrivate,
    KwProcedure,
    KwProgram,
    KwPublic,
    KwRaise,
    KwRecord,
    KwRepeat,
    KwThen,
    KwTo,
    KwTry,
    KwType,
    KwUnit,
    KwUntil,
    KwUses,
    KwVar,
    KwVirtual,
    KwWeak,
    KwWhile,
    KwWith,
    KwSet,
    KwForward,
    KwInitialization,
    KwFinalization,
    KwProperty,

    // Operators
    Plus,         ///< +
    Minus,        ///< -
    Star,         ///< *
    Slash,        ///< /
    Equal,        ///< =
    NotEqual,     ///< <>
    Less,         ///< <
    Greater,      ///< >
    LessEqual,    ///< <=
    GreaterEqual, ///< >=
    Assign,       ///< :=
    NilCoalesce,  ///< ??
    Question,     ///< ? (optional type suffix)

    // Punctuation
    Dot,       ///< .
    Comma,     ///< ,
    Semicolon, ///< ;
    Colon,     ///< :
    LParen,    ///< (
    RParen,    ///< )
    LBracket,  ///< [
    RBracket,  ///< ]
    Caret,     ///< ^ (pointer dereference)
    At,        ///< @ (address-of)
    DotDot,    ///< .. (range)
};

/// @brief Convert TokenKind to human-readable string.
/// @param kind Token kind to convert.
/// @return String representation of the token kind.
const char *tokenKindToString(TokenKind kind);

/// @brief A lexical token produced by the Pascal lexer.
struct Token
{
    /// @brief Classification of this token.
    TokenKind kind{TokenKind::Eof};

    /// @brief Original spelling of the token in source.
    std::string text;

    /// @brief Case-folded (lowercase) form for case-insensitive comparison.
    std::string canonical;

    /// @brief Parsed integer value for IntegerLiteral tokens.
    int64_t intValue{0};

    /// @brief Parsed real value for RealLiteral tokens.
    double realValue{0.0};

    /// @brief True if this identifier is a predefined identifier (Self, Result, etc.)
    bool isPredefined{false};

    /// @brief Source location where the token begins.
    il::support::SourceLoc loc;
};

/// @brief Tokenizes Pascal source text into a stream of tokens.
/// @details Construct with source buffer, file ID, and diagnostic engine.
/// Call next() repeatedly to iterate through tokens until Eof is returned.
/// @invariant Source buffer must remain valid for lexer lifetime.
class Lexer
{
  public:
    /// @brief Create a lexer over the given source buffer.
    /// @param source Source text to tokenize.
    /// @param fileId Identifier of the source file for diagnostics.
    /// @param diag Diagnostic engine for reporting errors.
    Lexer(std::string source, uint32_t fileId, il::support::DiagnosticEngine &diag);

    /// @brief Produce the next token in the source.
    /// @return The next lexical token, or an Eof token when no characters remain.
    Token next();

    /// @brief Peek at the next token without consuming it.
    /// @return The next token (cached).
    const Token &peek();

  private:
    /// @brief Look at the current character without consuming it.
    /// @return The current character, or '\0' if at end of source.
    char peekChar() const;

    /// @brief Look at the character at current position + offset.
    /// @param offset Number of characters ahead to look.
    /// @return The character at offset, or '\0' if beyond end.
    char peekChar(size_t offset) const;

    /// @brief Consume and return the current character.
    /// @return The consumed character, or '\0' if at end of source.
    char getChar();

    /// @brief Check whether the lexer has reached the end of the source.
    /// @return True if no characters remain, otherwise false.
    bool eof() const;

    /// @brief Skip whitespace characters (space, tab, CR, LF).
    void skipWhitespace();

    /// @brief Skip a line comment starting with //.
    void skipLineComment();

    /// @brief Skip a block comment starting with { or (*.
    /// @param startChar Opening delimiter ('{' or '(').
    /// @return True if comment was properly terminated.
    bool skipBlockComment(char startChar);

    /// @brief Skip all whitespace and comments.
    void skipWhitespaceAndComments();

    /// @brief Create a source location at the current position.
    /// @return SourceLoc with current file, line, column.
    il::support::SourceLoc currentLoc() const;

    /// @brief Lex a numeric literal (integer, real, or hex).
    /// @return Token of kind IntegerLiteral or RealLiteral.
    Token lexNumber();

    /// @brief Lex a hexadecimal integer literal starting with $.
    /// @return Token of kind IntegerLiteral.
    Token lexHexNumber();

    /// @brief Lex an identifier or keyword.
    /// @return Token of appropriate kind.
    Token lexIdentifierOrKeyword();

    /// @brief Lex a string literal enclosed in single quotes.
    /// @return Token of kind StringLiteral or Error.
    Token lexString();

    /// @brief Report an error diagnostic.
    /// @param loc Source location of error.
    /// @param message Error message.
    void reportError(il::support::SourceLoc loc, const std::string &message);

    /// @brief Lookup identifier to determine if it's a keyword.
    /// @param canonical Case-folded identifier.
    /// @return TokenKind for keyword, or nullopt if not a keyword.
    static std::optional<TokenKind> lookupKeyword(const std::string &canonical);

    /// @brief Check if identifier is a predefined identifier.
    /// @param canonical Case-folded identifier.
    /// @return True if predefined (Self, Result, True, False, etc.)
    static bool isPredefinedIdentifier(const std::string &canonical);

    std::string source_;                  ///< Source code being tokenized.
    size_t pos_{0};                       ///< Current index into source.
    uint32_t fileId_;                     ///< File identifier for locations.
    uint32_t line_{1};                    ///< 1-based line number.
    uint32_t column_{1};                  ///< 1-based column number.
    il::support::DiagnosticEngine &diag_; ///< Diagnostic engine for errors.
    std::optional<Token> peeked_;         ///< Cached lookahead token.
};

} // namespace il::frontends::pascal
