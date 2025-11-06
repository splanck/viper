//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Parser_Stmt_Core.cpp
// Purpose: Implement parsing routines for core BASIC statements such as LET and
//          procedure declarations.
// Key invariants: Maintains the parser's registry of known procedures so CALL
//                 statements without parentheses can still be resolved and
//                 ensures assignment targets honour BASIC's typing conventions.
// Ownership/Lifetime: Parser allocates AST nodes with std::unique_ptr and
//                     transfers ownership to the caller.
// Links: docs/codemap.md, docs/basic-language.md#statements
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements statement-level parsing helpers for the BASIC front end.
/// @details These helpers cover assignment, CALL statements, and procedure
///          declarations.  They operate on the token stream owned by @ref Parser,
///          emit diagnostics via @ref emitter_, and keep the procedure registry
///          up to date so later passes can recognise identifiers in CALL syntax
///          that omits parentheses.

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

/// @brief Record that a procedure declaration introduced a name.
/// @details The parser tracks procedure identifiers so later CALL statements can
///          recognise bare identifiers that omit parentheses.  The registry is
///          also consulted when emitting diagnostics for ambiguous identifiers.
/// @param name Procedure identifier to register.
void Parser::noteProcedureName(std::string name)
{
    knownProcedures_.insert(std::move(name));
}

/// @brief Query whether a procedure name has been seen previously.
/// @details Looks up @p name in the registry populated by
///          @ref noteProcedureName.  Names are compared using the canonical
///          casing produced by the lexer, making the check deterministic across
///          dialects.
/// @param name Identifier being queried.
/// @return True when @p name already appears in the procedure registry.
bool Parser::isKnownProcedureName(const std::string &name) const
{
    return knownProcedures_.find(name) != knownProcedures_.end();
}

/// @brief Attempt to parse a `LET` assignment statement.
/// @details Recognises the `LET` keyword (which some dialects make optional) and
///          delegates to @ref parseLetStatement to build the AST node.  When no
///          keyword is present the token stream is left untouched so other
///          statement parsers can attempt a match.
/// @return Populated @ref StmtResult when a `LET` statement is recognised;
///         @c std::nullopt otherwise.
Parser::StmtResult Parser::parseLet()
{
    if (!at(TokenKind::KeywordLet))
        return std::nullopt;
    auto stmt = parseLetStatement();
    return StmtResult(std::move(stmt));
}

/// @brief Parse a procedure or method call statement when the lookahead matches.
/// @details BASIC allows calling procedures without parentheses.  The parser
///          therefore checks whether the identifier corresponds to a known
///          procedure and, if the following token is not an opening parenthesis,
///          emits a diagnostic before recovering.  Method-call syntax using the
///          dotted form is delegated to the expression parser so object-oriented
///          lowering can handle it uniformly.  When the pattern does not match,
///          the helper leaves the token stream untouched so other statement
///          parsers can attempt to consume the input.
/// @param line Source line hint supplied by the driver; the parser uses token
///             locations directly so the parameter is ignored.
/// @return Successful call statement wrapped in @ref StmtResult, @c std::nullopt
///         when no call is present, or an error result when recovery diagnostics
///         were emitted.
Parser::StmtResult Parser::parseCall(int line)
{
    static_cast<void>(line);
    if (!at(TokenKind::Identifier))
        return std::nullopt;
    const Token identTok = peek();
    const Token nextTok = peek(1);
    if (nextTok.kind == TokenKind::Dot)
    {
        const Token t2 = peek(2);
        const Token t3 = peek(3);
        if (t2.kind == TokenKind::Identifier && t3.kind == TokenKind::LParen)
        {
            auto expr = parseExpression(/*min_prec=*/0);
            if (expr && dynamic_cast<MethodCallExpr *>(expr.get()) != nullptr)
            {
                auto stmt = std::make_unique<CallStmt>();
                stmt->loc = identTok.loc;
                stmt->call = std::move(expr);
                return StmtResult(std::move(stmt));
            }
            reportUnknownStatement(identTok);
            resyncAfterError();
            return StmtResult(StmtPtr{});
        }
        return std::nullopt;
    }
    if (nextTok.kind != TokenKind::LParen)
    {
        if (isKnownProcedureName(identTok.lexeme))
        {
            reportMissingCallParenthesis(identTok, nextTok);
            resyncAfterError();
            return StmtResult(StmtPtr{});
        }
        return std::nullopt;
    }

    auto expr = parseArrayOrVar();
    if (expr && dynamic_cast<CallExpr *>(expr.get()) != nullptr)
    {
        auto stmt = std::make_unique<CallStmt>();
        stmt->loc = identTok.loc;
        stmt->call = std::move(expr);
        return StmtResult(std::move(stmt));
    }
    reportInvalidCallExpression(identTok);
    resyncAfterError();
    return StmtResult(StmtPtr{});
}

/// @brief Emit a diagnostic when a procedure call omits its opening parenthesis.
/// @details Highlights either the token that replaced the parenthesis or, when
///          such a token is absent, the identifier itself.  Diagnostics prefer
///          the configured @ref emitter_ but fall back to stderr so command-line
///          tools still provide feedback.
/// @param identTok Identifier naming the procedure being invoked.
/// @param nextTok Token that followed the identifier in the input stream.
void Parser::reportMissingCallParenthesis(const Token &identTok, const Token &nextTok)
{
    auto diagLoc = nextTok.loc.hasLine() ? nextTok.loc : identTok.loc;
    uint32_t length = nextTok.lexeme.empty() ? 1 : static_cast<uint32_t>(nextTok.lexeme.size());
    if (emitter_)
    {
        std::string message = "expected '(' after procedure name '" + identTok.lexeme + "' in procedure call statement";
        emitter_->emit(il::support::Severity::Error, "B0001", diagLoc, length, std::move(message));
    }
    else
    {
        std::fprintf(stderr, "expected '(' after procedure name '%s' in procedure call statement\n", identTok.lexeme.c_str());
    }
}

/// @brief Emit a diagnostic when an identifier cannot be parsed as a call.
/// @details Triggered after the parser determines that an identifier-led
///          statement is neither a method call nor a registered procedure.  The
///          message mirrors historical front-end behaviour to keep existing
///          tests stable.
/// @param identTok Identifier token that initiated the failed parse.
void Parser::reportInvalidCallExpression(const Token &identTok)
{
    if (emitter_)
    {
        std::string message = "expected procedure call after identifier '" + identTok.lexeme + "'";
        emitter_->emit(il::support::Severity::Error,
                       "B0001",
                       identTok.loc,
                       static_cast<uint32_t>(identTok.lexeme.size()),
                       std::move(message));
    }
    else
    {
        std::fprintf(stderr, "expected procedure call after identifier '%s'\n", identTok.lexeme.c_str());
    }
}

/// @brief Parse a `LET` assignment statement and build the AST node.
/// @details Consumes the keyword, parses the assignment target (which may be a
///          variable or array element), verifies the presence of the equals sign,
///          and then parses the right-hand expression.  The resulting
///          @ref LetStmt retains the originating source location for later
///          diagnostics.
/// @return Newly constructed @ref LetStmt instance.
StmtPtr Parser::parseLetStatement()
{
    auto loc = peek().loc;
    consume();
    auto target = parsePrimary();
    expect(TokenKind::Equal);
    auto e = parseExpression();
    auto stmt = std::make_unique<LetStmt>();
    stmt->loc = loc;
    stmt->target = std::move(target);
    stmt->expr = std::move(e);
    return stmt;
}

/// @brief Translate a BASIC identifier suffix into a concrete type.
/// @details BASIC supports single-character suffixes to denote the expected
///          storage type (for example `%` for integers and `$` for strings).
///          When no suffix is present the helper returns the language's default
///          numeric type.
/// @param name Identifier lexeme to inspect.
/// @return Corresponding @ref Type derived from the suffix, defaulting to
///         @ref Type::I64.
Type Parser::typeFromSuffix(std::string_view name)
{
    if (!name.empty())
    {
        char c = name.back();
        switch (c)
        {
            case '#':
            case '!':
                return Type::F64;
            case '$':
                return Type::Str;
            case '%':
            case '&':
                return Type::I64;
            default:
                break;
        }
    }
    return Type::I64;
}

/// @brief Parse a BASIC type keyword following an `AS` clause.
/// @details Accepts both reserved keywords (such as `BOOLEAN`) and legacy
///          identifier spellings (`INTEGER`, `DOUBLE`, `SINGLE`, `STRING`).  When
///          the token does not match any recognised type the helper returns the
///          default integer type so the caller can emit a targeted diagnostic.
/// @return Type decoded from the keyword, defaulting to @ref Type::I64.
Type Parser::parseTypeKeyword()
{
    if (at(TokenKind::KeywordBoolean))
    {
        consume();
        return Type::Bool;
    }
    if (at(TokenKind::Identifier))
    {
        std::string name = peek().lexeme;
        consume();
        if (name == "INTEGER")
            return Type::I64;
        if (name == "DOUBLE")
            return Type::F64;
        if (name == "SINGLE")
            return Type::F64;
        if (name == "STRING")
            return Type::Str;
    }
    return Type::I64;
}

/// @brief Parse the optional parameter list attached to a procedure header.
/// @details Handles empty lists, array suffixes, and comma-separated parameters.
///          Each parameter inherits its identifier suffix for type inference and
///          records whether it represents an array argument.  The helper stops
///          parsing at the closing parenthesis, leaving newline handling to the
///          caller.
/// @return Ordered list of parameters; empty when no list was present.
std::vector<Param> Parser::parseParamList()
{
    std::vector<Param> params;
    if (!at(TokenKind::LParen))
        return params;
    consume();
    if (at(TokenKind::RParen))
    {
        consume();
        return params;
    }
    while (true)
    {
        Token id = expect(TokenKind::Identifier);
        Param p;
        p.loc = id.loc;
        p.name = id.lexeme;
        p.type = typeFromSuffix(id.lexeme);
        if (at(TokenKind::LParen))
        {
            consume();
            expect(TokenKind::RParen);
            p.is_array = true;
        }
        params.push_back(std::move(p));
        if (at(TokenKind::Comma))
        {
            consume();
            continue;
        }
        break;
    }
    expect(TokenKind::RParen);
    return params;
}

/// @brief Parse a complete `FUNCTION` procedure declaration.
/// @details Builds the function AST node, applies explicit return-type
///          annotations, registers the procedure name for later CALL resolution,
///          and delegates to @ref parseProcedureBody to collect nested
///          statements.  Explicit BASIC type keywords are translated into the
///          internal @ref Type enumeration.
/// @return Newly constructed function declaration node.
StmtPtr Parser::parseFunctionStatement()
{
    auto func = parseFunctionHeader();
    if (func->explicitRetType != BasicType::Unknown)
    {
        switch (func->explicitRetType)
        {
            case BasicType::Int:
                func->ret = Type::I64;
                break;
            case BasicType::Float:
                func->ret = Type::F64;
                break;
            case BasicType::String:
                func->ret = Type::Str;
                break;
            case BasicType::Void:
                func->ret = Type::I64;
                break;
            case BasicType::Unknown:
                break;
        }
    }
    noteProcedureName(func->name);
    parseProcedureBody(TokenKind::KeywordFunction, func->body);
    return func;
}

/// @brief Parse a complete `SUB` procedure declaration.
/// @details SUB routines mirror functions without return values.  The parser
///          constructs the AST node, parses the parameter list, rejects any `AS`
///          type clause with a diagnostic, registers the procedure name, and
///          finally gathers the body statements through @ref parseProcedureBody.
/// @return Newly constructed subroutine declaration node.
StmtPtr Parser::parseSubStatement()
{
    auto loc = peek().loc;
    consume();
    Token nameTok = expect(TokenKind::Identifier);
    auto sub = std::make_unique<SubDecl>();
    sub->loc = loc;
    sub->name = nameTok.lexeme;
    sub->params = parseParamList();
    if (at(TokenKind::KeywordAs))
    {
        Token asTok = consume();
        if (!at(TokenKind::EndOfLine) && !at(TokenKind::EndOfFile))
            consume();
        if (emitter_)
        {
            std::string message = "SUB cannot have 'AS <TYPE>'";
            emitter_->emit(il::support::Severity::Error,
                           "B4007",
                           asTok.loc,
                           static_cast<uint32_t>(asTok.lexeme.size()),
                           std::move(message));
        }
        else
        {
            std::fprintf(stderr, "SUB cannot have 'AS <TYPE>'\n");
        }
    }
    noteProcedureName(sub->name);
    parseProcedureBody(TokenKind::KeywordSub, sub->body);
    return sub;
}

} // namespace il::frontends::basic

