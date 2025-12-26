//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Parser.hpp
// Purpose: Declares the recursive descent parser for Viper Pascal.
// Key invariants: Precedence climbing for expressions; one-token lookahead.
// Ownership/Lifetime: Parser borrows Lexer and DiagnosticEngine.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "support/diagnostics.hpp"
#include <memory>
#include <string>
#include <utility>

namespace il::frontends::pascal
{

/// @brief Recursive descent parser for Viper Pascal.
/// @details Uses precedence climbing for expression parsing.
/// Operator precedence (highest to lowest):
///   1. not, unary -
///   2. *, /, div, mod, and
///   3. +, -, or
///   4. =, <>, <, >, <=, >=
///   5. ?? (lowest)
class Parser
{
  public:
    /// @brief Create a parser over the given lexer.
    /// @param lexer Lexer to read tokens from.
    /// @param diag Diagnostic engine for reporting errors.
    Parser(Lexer &lexer, il::support::DiagnosticEngine &diag);

    /// @brief Parse a complete Pascal program.
    /// @return The parsed Program AST, or nullptr on fatal error.
    std::unique_ptr<Program> parseProgram();

    /// @brief Parse a complete Pascal unit.
    /// @return The parsed Unit AST, or nullptr on fatal error.
    std::unique_ptr<Unit> parseUnit();

    /// @brief Parse either a program or unit based on first keyword.
    /// @return Program, Unit (via variant), or nullptr on error.
    /// @note Returns Program* in first, Unit* in second of pair.
    std::pair<std::unique_ptr<Program>, std::unique_ptr<Unit>> parse();

    /// @brief Parse a single expression (for testing).
    /// @return The parsed expression AST, or nullptr on error.
    std::unique_ptr<Expr> parseExpression();

    /// @brief Parse a single statement (for testing).
    /// @return The parsed statement AST, or nullptr on error.
    std::unique_ptr<Stmt> parseStatement();

    /// @brief Parse a type (for testing).
    /// @return The parsed type node, or nullptr on error.
    std::unique_ptr<TypeNode> parseType();

    /// @brief Check if any errors occurred during parsing.
    /// @return True if errors were reported.
    bool hasError() const
    {
        return hasError_;
    }

  private:
    //=========================================================================
    // Token Handling
    //=========================================================================

    /// @brief Peek at the current token without consuming it.
    /// @return Reference to the current token.
    const Token &peek() const;

    /// @brief Consume and return the current token.
    /// @return The consumed token.
    Token advance();

    /// @brief Check if current token matches the given kind.
    /// @param kind TokenKind to match.
    /// @return True if current token matches.
    bool check(TokenKind kind) const;

    /// @brief If current token matches, consume it and return true.
    /// @param kind TokenKind to match.
    /// @return True if matched and consumed.
    bool match(TokenKind kind);

    /// @brief Expect the current token to be of the given kind.
    /// @param kind Expected TokenKind.
    /// @param what Description of expected token for error messages.
    /// @return True if token matched, false if error was reported.
    bool expect(TokenKind kind, const char *what);

    /// @brief Skip tokens until reaching a synchronization point.
    /// @details Skips until: ; end else until EOF
    void resyncAfterError();

    //=========================================================================
    // Token Utilities
    //=========================================================================

    /// @brief Check if a token kind is a keyword.
    /// @param kind TokenKind to check.
    /// @return True if the token is a keyword (reserved word).
    static bool isKeyword(TokenKind kind);

    //=========================================================================
    // Error Handling
    //=========================================================================

    /// @brief Report a parser error at current location.
    /// @param message Error message.
    void error(const std::string &message);

    /// @brief Report a parser error at a specific location.
    /// @param loc Source location.
    /// @param message Error message.
    void errorAt(il::support::SourceLoc loc, const std::string &message);

    //=========================================================================
    // Expression Parsing (Precedence Climbing)
    //=========================================================================

    /// @brief Parse a coalesce expression (lowest precedence).
    /// @return Parsed expression or nullptr on error.
    std::unique_ptr<Expr> parseCoalesce();

    /// @brief Parse a relational expression.
    /// @return Parsed expression or nullptr on error.
    std::unique_ptr<Expr> parseRelation();

    /// @brief Parse a simple expression (additive).
    /// @return Parsed expression or nullptr on error.
    std::unique_ptr<Expr> parseSimple();

    /// @brief Parse a term (multiplicative).
    /// @return Parsed expression or nullptr on error.
    std::unique_ptr<Expr> parseTerm();

    /// @brief Parse a factor (highest precedence).
    /// @return Parsed expression or nullptr on error.
    std::unique_ptr<Expr> parseFactor();

    /// @brief Parse a primary expression (literals, names, parens).
    /// @return Parsed expression or nullptr on error.
    std::unique_ptr<Expr> parsePrimary();

    /// @brief Parse a designator (name with optional suffixes).
    /// @return Parsed expression or nullptr on error.
    std::unique_ptr<Expr> parseDesignator();

    /// @brief Parse designator suffixes (field, index, call).
    /// @param base Base expression to extend.
    /// @return Extended expression with suffixes.
    std::unique_ptr<Expr> parseDesignatorSuffix(std::unique_ptr<Expr> base);

    /// @brief Parse a comma-separated list of expressions.
    /// @return Vector of parsed expressions.
    std::vector<std::unique_ptr<Expr>> parseExprList();

    //=========================================================================
    // Statement Parsing
    //=========================================================================

    /// @brief Parse an if statement.
    /// @return Parsed IfStmt or nullptr on error.
    std::unique_ptr<Stmt> parseIf();

    /// @brief Parse a while statement.
    /// @return Parsed WhileStmt or nullptr on error.
    std::unique_ptr<Stmt> parseWhile();

    /// @brief Parse a repeat-until statement.
    /// @return Parsed RepeatStmt or nullptr on error.
    std::unique_ptr<Stmt> parseRepeat();

    /// @brief Parse a for or for-in statement.
    /// @return Parsed ForStmt or ForInStmt or nullptr on error.
    std::unique_ptr<Stmt> parseFor();

    /// @brief Parse a begin-end block.
    /// @return Parsed BlockStmt or nullptr on error.
    std::unique_ptr<BlockStmt> parseBlock();

    /// @brief Parse a statement list (separated by semicolons).
    /// @return Vector of parsed statements.
    std::vector<std::unique_ptr<Stmt>> parseStatementList();

    /// @brief Parse a raise statement.
    /// @return Parsed RaiseStmt or nullptr on error.
    std::unique_ptr<Stmt> parseRaise();

    /// @brief Parse a try-except or try-finally statement.
    /// @return Parsed TryExceptStmt or TryFinallyStmt or nullptr on error.
    std::unique_ptr<Stmt> parseTry();

    /// @brief Parse case statement (switch).
    /// @return Parsed CaseStmt or nullptr on error.
    std::unique_ptr<Stmt> parseCase();

    /// @brief Parse with statement.
    /// @return Parsed WithStmt or nullptr on error.
    std::unique_ptr<Stmt> parseWith();

    //=========================================================================
    // Type Parsing
    //=========================================================================

    /// @brief Parse a base type (before optional suffix).
    /// @return Parsed type node or nullptr on error.
    std::unique_ptr<TypeNode> parseBaseType();

    /// @brief Parse an array type.
    /// @return Parsed ArrayTypeNode or nullptr on error.
    std::unique_ptr<TypeNode> parseArrayType();

    /// @brief Parse a record type.
    /// @return Parsed RecordTypeNode or nullptr on error.
    std::unique_ptr<TypeNode> parseRecordType();

    /// @brief Parse an enumeration type.
    /// @return Parsed EnumTypeNode or nullptr on error.
    std::unique_ptr<TypeNode> parseEnumType();

    /// @brief Parse a pointer type.
    /// @return Parsed PointerTypeNode or nullptr on error.
    std::unique_ptr<TypeNode> parsePointerType();

    /// @brief Parse a set type.
    /// @return Parsed SetTypeNode or nullptr on error.
    std::unique_ptr<TypeNode> parseSetType();

    /// @brief Parse a procedure type.
    /// @return Parsed ProcedureTypeNode or nullptr on error.
    std::unique_ptr<TypeNode> parseProcedureType();

    /// @brief Parse a function type.
    /// @return Parsed FunctionTypeNode or nullptr on error.
    std::unique_ptr<TypeNode> parseFunctionType();

    //=========================================================================
    // Declaration Parsing
    //=========================================================================

    /// @brief Parse declaration sections (const, type, var, proc, func).
    /// @return Vector of parsed declarations.
    std::vector<std::unique_ptr<Decl>> parseDeclarations();

    /// @brief Parse a const section.
    /// @return Vector of ConstDecl.
    std::vector<std::unique_ptr<Decl>> parseConstSection();

    /// @brief Parse a type section.
    /// @return Vector of TypeDecl.
    std::vector<std::unique_ptr<Decl>> parseTypeSection();

    /// @brief Parse a var section.
    /// @return Vector of VarDecl.
    std::vector<std::unique_ptr<Decl>> parseVarSection();

    /// @brief Parse a procedure declaration.
    /// @return Parsed ProcedureDecl or nullptr on error.
    std::unique_ptr<Decl> parseProcedure();

    /// @brief Parse a function declaration.
    /// @return Parsed FunctionDecl or nullptr on error.
    std::unique_ptr<Decl> parseFunction();

    /// @brief Parse function/procedure parameters.
    /// @return Vector of ParamDecl.
    std::vector<ParamDecl> parseParameters();

    /// @brief Parse a single parameter group (var x, y: Integer).
    /// @return Vector of ParamDecl (one per name).
    std::vector<ParamDecl> parseParamGroup();

    /// @brief Parse a class declaration.
    /// @return Parsed ClassDecl or nullptr on error.
    std::unique_ptr<Decl> parseClass(const std::string &name, il::support::SourceLoc loc);

    /// @brief Parse an interface declaration.
    /// @return Parsed InterfaceDecl or nullptr on error.
    std::unique_ptr<Decl> parseInterface(const std::string &name, il::support::SourceLoc loc);

    /// @brief Parse class member declarations.
    /// @param currentVisibility Current visibility scope.
    /// @return Vector of parsed ClassMembers (multiple for comma-separated fields).
    std::vector<ClassMember> parseClassMembers(Visibility currentVisibility);

    /// @brief Parse a method signature (procedure/function without body).
    /// Used for class member methods which are declared without bodies.
    /// @param isFunction True for function, false for procedure.
    /// @return Parsed ProcedureDecl or FunctionDecl or nullptr on error.
    std::unique_ptr<Decl> parseMethodSignature(bool isFunction);

    /// @brief Parse a constructor declaration (full, with body).
    /// @return Parsed ConstructorDecl or nullptr on error.
    std::unique_ptr<Decl> parseConstructor();

    /// @brief Parse a constructor signature (no body, for class members).
    /// @return Parsed ConstructorDecl or nullptr on error.
    std::unique_ptr<Decl> parseConstructorSignature();

    /// @brief Parse a destructor declaration (full, with body).
    /// @return Parsed DestructorDecl or nullptr on error.
    std::unique_ptr<Decl> parseDestructor();

    /// @brief Parse a destructor signature (no body, for class members).
    /// @return Parsed DestructorDecl or nullptr on error.
    std::unique_ptr<Decl> parseDestructorSignature();

    /// @brief Parse a comma-separated list of identifiers.
    /// @return Vector of identifier strings.
    std::vector<std::string> parseIdentList();

    //=========================================================================
    // Program/Unit Parsing
    //=========================================================================

    /// @brief Parse a uses clause.
    /// @return Vector of unit names.
    std::vector<std::string> parseUses();

    //=========================================================================
    // Member Variables
    //=========================================================================

    Lexer &lexer_;                        ///< Token source.
    il::support::DiagnosticEngine &diag_; ///< Diagnostic engine.
    Token current_;                       ///< Current token.
    bool hasError_{false};                ///< Error flag.
};

} // namespace il::frontends::pascal
