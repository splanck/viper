//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Parser_Stmt_Core.cpp
// Purpose: Implement parsing routines for core BASIC statements such as LET and procedure declarations.
// Key invariants: Maintains the parser's registry of known procedures so CALL statements without parentheses can still be
//                 resolved and ensures assignment targets honour BASIC's typing conventions.
// Ownership/Lifetime: Parser allocates AST nodes with std::unique_ptr and transfers ownership to the caller.
// Links: docs/codemap.md, docs/basic-language.md#statements
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"

#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

/// @file
/// @brief Core BASIC statement parsing entry points.
/// @details Provides the shared helpers that recognise procedure declarations,
///          LET assignments, and CALL statements.  The routines maintain the
///          parser's registry of known procedures so ambiguity between
///          identifier expressions and procedure calls can be resolved without
///          backtracking.

namespace il::frontends::basic
{

/// @brief Remember that a procedure declaration introduced @p name.
/// @details The parser keeps a set of procedure identifiers so later
///          parenthesis-free CALL statements can be interpreted correctly.  This
///          helper inserts the identifier into that set, guaranteeing idempotent
///          behaviour across multiple declarations.
/// @param name Canonical procedure name encountered in the source program.
void Parser::noteProcedureName(std::string name)
{
    knownProcedures_.insert(std::move(name));
}

/// @brief Query whether @p name is tracked as a known procedure.
/// @details Procedure references without parentheses rely on this lookup to
///          disambiguate between variable access and an implicit CALL.  The
///          check performs an @c O(log n) probe against the tracked identifier
///          set.
/// @param name Identifier token spelling to probe.
/// @return True when the parser has previously recorded the name as a
///         procedure.
bool Parser::isKnownProcedureName(const std::string &name) const
{
    return knownProcedures_.find(name) != knownProcedures_.end();
}

/// @brief Attempt to parse a BASIC `LET` assignment statement.
/// @details The parser peeks at the current token and, when it observes the
///          `LET` keyword, forwards to @ref parseLetStatement to build the AST
///          node.  When the keyword is absent a disengaged optional is returned
///          so callers can continue exploring other productions without
///          consuming input.
/// @return Parsed statement when the rule matched; otherwise an empty optional.
Parser::StmtResult Parser::parseLet()
{
    if (!at(TokenKind::KeywordLet))
        return std::nullopt;
    auto stmt = parseLetStatement();
    return StmtResult(std::move(stmt));
}

/// @brief Parse a procedure or method call statement when possible.
/// @details BASIC allows both object method invocations (e.g. `obj.method()`)
///          and legacy procedure calls that omit parentheses.  The routine first
///          detects object-style calls by scanning ahead for `identifier.
///          identifier(`.  Failing that, it interprets `identifier(` as a normal
///          call expression or, when the name is known to refer to a procedure,
///          emits a diagnostic if the parentheses are missing.  Any malformed
///          sequence triggers an error report and synchronisation so parsing can
///          continue.
/// @param unused Historical parameter used to disambiguate overload sets;
///        retained to preserve the call signature used by the parser dispatch.
/// @return Parsed call statement on success, an empty optional when no call is
///         present, or a null statement pointer when an error was reported.
Parser::StmtResult Parser::parseCall(int)
{
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

/// @brief Emit a diagnostic for procedure calls that omit parentheses.
/// @details When a known procedure name is followed by a non-`(` token the
///          parser expects the legacy CALL syntax and surfaces a diagnostic.
///          This helper routes the message either through the diagnostic
///          emitter or, in tooling-lite builds, directly to stderr.  The caret
///          is positioned at the unexpected token when available.
/// @param identTok Identifier naming the procedure.
/// @param nextTok  Token that violated the call syntax.
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

/// @brief Emit a diagnostic for identifiers that fail to form a valid call.
/// @details If expression parsing fails to yield a @ref CallExpr or
///          @ref MethodCallExpr the parser reports an error explaining the
///          expected construct.  The diagnostic is routed through the configured
///          emitter when available, otherwise it falls back to printing the
///          message directly.
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

/// @brief Parse a BASIC `LET` assignment statement.
/// @details Consumes the `LET` keyword, parses the left-hand side using
///          @ref parsePrimary, and then expects an `=` followed by a general
///          expression.  The resulting @ref LetStmt adopts the source location
///          of the keyword so diagnostics can report accurate spans.
/// @return Newly constructed LET statement node.
StmtPtr Parser::parseLetStatement()
{
    auto loc = peek().loc;
    consume();
    auto target = parseLetTarget();
    expect(TokenKind::Equal);
    auto e = parseExpression();
    auto stmt = std::make_unique<LetStmt>();
    stmt->loc = loc;
    stmt->target = std::move(target);
    stmt->expr = std::move(e);
    return stmt;
}

ExprPtr Parser::parseLetTarget()
{
    ExprPtr base;
    if (at(TokenKind::Identifier))
    {
        base = parseArrayOrVar();
    }
    else
    {
        base = parsePrimary();
    }
    return parsePostfix(std::move(base));
}

/// @brief Derive the default BASIC type from an identifier suffix.
/// @details BASIC permits suffix characters (such as `$` or `%`) that encode a
///          variable's type.  This helper inspects the final character of the
///          identifier and maps it to the appropriate semantic type, falling
///          back to integer when no suffix is present.
/// @param name Identifier spelling to inspect.
/// @return Semantic type dictated by the suffix.
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

/// @brief Parse a BASIC type keyword that follows an `AS` clause.
/// @details Recognises both reserved keywords (e.g. `BOOLEAN`) and legacy
///          identifiers such as `INTEGER` or `STRING`.  When no recognised
///          keyword is present the default integer type is returned so the
///          caller can flag the failure separately if desired.
/// @return Semantic type parsed from the token stream.
Type Parser::parseTypeKeyword()
{
    if (at(TokenKind::KeywordBoolean))
    {
        consume();
        return Type::Bool;
    }
    if (at(TokenKind::Identifier))
    {
        auto toUpper = [](std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            for (char ch : text)
            {
                unsigned char byte = static_cast<unsigned char>(ch);
                result.push_back(static_cast<char>(std::toupper(byte)));
            }
            return result;
        };

        std::string name = peek().lexeme;
        consume();
        std::string upperName = toUpper(name);
        if (upperName == "INTEGER" || upperName == "INT" || upperName == "LONG")
            return Type::I64;
        if (upperName == "DOUBLE" || upperName == "FLOAT")
            return Type::F64;
        if (upperName == "SINGLE")
            return Type::F64;
        if (upperName == "STRING")
            return Type::Str;
    }
    return Type::I64;
}

/// @brief Parse an optional parenthesised parameter list.
/// @details If the current token is an opening parenthesis the parser
///          repeatedly consumes identifiers, array markers, and commas until the
///          closing parenthesis is reached.  Each parameter inherits its type
///          from the identifier suffix and records whether array brackets were
///          present.
/// @return Sequence of parameter descriptors discovered in the token stream.
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
        if (at(TokenKind::KeywordAs))
        {
            consume();
            if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
            {
                p.type = parseTypeKeyword();
            }
            else
            {
                expect(TokenKind::Identifier);
            }
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

/// @brief Parse a full BASIC `FUNCTION` declaration.
/// @details Delegates to @ref parseFunctionHeader to build the declaration
///          scaffold, infers the return type from either an explicit suffix or
///          the `AS` clause, records the procedure name for later disambiguation
///          of CALL statements, and finally parses the body until the matching
///          `END FUNCTION` terminator is reached.
/// @return Newly constructed function declaration statement.
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

/// @brief Parse a complete BASIC `SUB` declaration.
/// @details Consumes the `SUB` keyword and identifier, parses the optional
///          parameter list, and rejects any stray `AS <type>` clause (which is
///          illegal for subroutines).  After recording the procedure name the
///          body is parsed until the closing `END SUB` token pair is found.
/// @return Newly constructed subroutine declaration statement.
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

