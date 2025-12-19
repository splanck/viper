//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/Parser.hpp
// Purpose: Recursive descent parser for ViperLang.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/viperlang/AST.hpp"
#include "frontends/viperlang/Lexer.hpp"
#include "support/diagnostics.hpp"
#include <memory>
#include <string>

namespace il::frontends::viperlang
{

/// @brief Recursive descent parser for ViperLang.
/// @details Uses precedence climbing for expression parsing.
/// Operator precedence (highest to lowest):
///   1. Primary (literals, identifiers, parentheses)
///   2. Postfix (call, index, field access)
///   3. Unary (!, -, ~)
///   4. Multiplicative (*, /, %)
///   5. Additive (+, -)
///   6. Comparison (<, >, <=, >=)
///   7. Equality (==, !=)
///   8. Logical AND (&&)
///   9. Logical OR (||)
///  10. Null coalesce (??)
///  11. Range (.., ..=)
///  12. Ternary (? :)
class Parser
{
  public:
    /// @brief Create a parser over the given lexer.
    Parser(Lexer &lexer, il::support::DiagnosticEngine &diag);

    /// @brief Parse a complete module.
    /// @return The parsed ModuleDecl, or nullptr on fatal error.
    std::unique_ptr<ModuleDecl> parseModule();

    /// @brief Parse a single expression (for testing).
    ExprPtr parseExpression();

    /// @brief Parse a single statement (for testing).
    StmtPtr parseStatement();

    /// @brief Check if any errors occurred during parsing.
    bool hasError() const { return hasError_; }

  private:
    //=========================================================================
    // Token Handling
    //=========================================================================

    const Token &peek() const;
    Token advance();
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    bool expect(TokenKind kind, const char *what);
    void resyncAfterError();

    //=========================================================================
    // Error Handling
    //=========================================================================

    void error(const std::string &message);
    void errorAt(SourceLoc loc, const std::string &message);

    //=========================================================================
    // Expression Parsing (Precedence Climbing)
    //=========================================================================

    ExprPtr parsePrimary();
    ExprPtr parsePostfix();
    ExprPtr parseUnary();
    ExprPtr parseMultiplicative();
    ExprPtr parseAdditive();
    ExprPtr parseComparison();
    ExprPtr parseEquality();
    ExprPtr parseLogicalAnd();
    ExprPtr parseLogicalOr();
    ExprPtr parseCoalesce();
    ExprPtr parseRange();
    ExprPtr parseTernary();
    ExprPtr parseAssignment();

    ExprPtr parseListLiteral();
    ExprPtr parseMapOrSetLiteral();
    std::vector<CallArg> parseCallArgs();

    //=========================================================================
    // Statement Parsing
    //=========================================================================

    StmtPtr parseBlock();
    StmtPtr parseVarDecl();
    StmtPtr parseIfStmt();
    StmtPtr parseWhileStmt();
    StmtPtr parseForStmt();
    StmtPtr parseReturnStmt();
    StmtPtr parseGuardStmt();
    StmtPtr parseMatchStmt();

    //=========================================================================
    // Type Parsing
    //=========================================================================

    TypePtr parseType();
    TypePtr parseBaseType();

    //=========================================================================
    // Declaration Parsing
    //=========================================================================

    DeclPtr parseDeclaration();
    DeclPtr parseFunctionDecl();
    DeclPtr parseValueDecl();
    DeclPtr parseEntityDecl();
    DeclPtr parseInterfaceDecl();
    ImportDecl parseImportDecl();

    std::vector<Param> parseParameters();
    std::vector<std::string> parseGenericParams();

    //=========================================================================
    // Member Variables
    //=========================================================================

    Lexer &lexer_;
    il::support::DiagnosticEngine &diag_;
    Token current_;
    bool hasError_{false};
};

} // namespace il::frontends::viperlang
