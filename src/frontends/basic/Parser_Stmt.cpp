//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Parser_Stmt.cpp
// Purpose: Implement statement-level parsing routines for the BASIC front end.
// Key invariants: Relies on the token buffer for deterministic lookahead and
//                 records discovered procedures so forward references can be
//                 recognised.
// Ownership/Lifetime: Parser owns tokens produced by the lexer and returns
//                     statements as std::unique_ptr nodes that the caller
//                     manages.
// Links: docs/codemap.md, docs/basic-language.md#statements
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

/// @brief Register the minimal statement parsers required for core BASIC.
///
/// @details Installs handlers for assignment statements and procedure
///          declarations.  The registry keeps pointers-to-member functions so
///          dispatch remains table-driven.  Centralising the registration here
///          keeps @ref Parser::parseStatement focused on lookup rather than the
///          list of supported statements.
///
/// @param registry Dispatcher that maps statement-starting tokens to Parser
///        member functions.
void Parser::registerCoreParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordLet, &Parser::parseLetStatement);
    registry.registerHandler(TokenKind::KeywordFunction, &Parser::parseFunctionStatement);
    registry.registerHandler(TokenKind::KeywordSub, &Parser::parseSubStatement);
}

/// @brief Register object-oriented statement parsers when the feature set is enabled.
///
/// @details Adds handlers for class/type declarations and delete statements so
///          source files that opt into OOP extensions can parse the additional
///          constructs.  The core registry remains unchanged when these
///          keywords are absent.
///
/// @param registry Dispatcher that records keyword-to-handler mappings.
void Parser::registerOopParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordClass, &Parser::parseClassDecl);
    registry.registerHandler(TokenKind::KeywordType, &Parser::parseTypeDecl);
    registry.registerHandler(TokenKind::KeywordDelete, &Parser::parseDeleteStatement);
}

/// @brief Remember that a procedure with @p name was encountered.
///
/// @details Procedure names are cached so subsequent bare identifiers can be
///          interpreted as procedure calls even when parentheses are omitted.
///          The method inserts the name into a case-sensitive set without
///          filtering because the parser assumes upstream normalization.
///
/// @param name Canonical procedure identifier collected during parsing.
void Parser::noteProcedureName(std::string name)
{
    knownProcedures_.insert(std::move(name));
}

/// @brief Query whether a procedure name has been recorded previously.
///
/// @param name Candidate identifier to check.
/// @return @c true when @ref noteProcedureName registered the symbol earlier.
bool Parser::isKnownProcedureName(const std::string &name) const
{
    return knownProcedures_.find(name) != knownProcedures_.end();
}

/// @brief Parse a single BASIC statement based on the current token.
///
/// @details Delegates parsing to specialised helpers that either return a
///          constructed statement or report an error before invoking
///          @ref resyncAfterError for recovery.  The dispatcher keeps the
///          per-statement logic isolated, improving readability and making the
///          recovery strategy consistent across statement kinds.
///
/// @param line One-based line number attached to the statement from the
///        original source listing.
/// @return Owned AST node on success; @c nullptr when recovery is required.
StmtPtr Parser::parseStatement(int line)
{
    if (auto handled = parseLeadingLineNumberError())
        return std::move(*handled);
    if (auto stmt = parseIf(line))
        return std::move(*stmt);
    if (auto stmt = parseSelect(line))
        return std::move(*stmt);
    if (auto stmt = parseFor(line))
        return std::move(*stmt);
    if (auto stmt = parseWhile(line))
        return std::move(*stmt);
    if (auto stmt = parseLet())
        return std::move(*stmt);
    if (auto stmt = parseCall(line))
        return std::move(*stmt);
    if (auto stmt = parseRegisteredStatement(line))
        return std::move(*stmt);

    Token offendingTok = peek();
    reportUnknownStatement(offendingTok);
    resyncAfterError();
    while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
    {
        consume();
    }
    return nullptr;
}

Parser::StmtResult Parser::parseRegisteredStatement(int line)
{
    const Token &tok = peek();
    const auto [noArg, withLine] = statementRegistry().lookup(tok.kind);
    if (noArg)
    {
        auto stmt = (this->*noArg)();
        return StmtResult(std::move(stmt));
    }
    if (withLine)
    {
        auto stmt = (this->*withLine)(line);
        return StmtResult(std::move(stmt));
    }
    return std::nullopt;
}

Parser::StmtResult Parser::parseIf(int line)
{
    if (!at(TokenKind::KeywordIf))
        return std::nullopt;
    auto stmt = parseIfStatement(line);
    return StmtResult(std::move(stmt));
}

Parser::StmtResult Parser::parseSelect(int line)
{
    static_cast<void>(line);
    if (!at(TokenKind::KeywordSelect))
        return std::nullopt;
    auto stmt = parseSelectCaseStatement();
    return StmtResult(std::move(stmt));
}

Parser::StmtResult Parser::parseFor(int line)
{
    static_cast<void>(line);
    if (!at(TokenKind::KeywordFor))
        return std::nullopt;
    auto stmt = parseForStatement();
    return StmtResult(std::move(stmt));
}

Parser::StmtResult Parser::parseWhile(int line)
{
    static_cast<void>(line);
    if (!at(TokenKind::KeywordWhile))
        return std::nullopt;
    auto stmt = parseWhileStatement();
    return StmtResult(std::move(stmt));
}

Parser::StmtResult Parser::parseLet()
{
    if (!at(TokenKind::KeywordLet))
        return std::nullopt;
    auto stmt = parseLetStatement();
    return StmtResult(std::move(stmt));
}

Parser::StmtResult Parser::parseCall(int)
{
    if (!at(TokenKind::Identifier))
        return std::nullopt;

    const Token identTok = peek();
    const Token nextTok = peek(1);

    // Path A: instance-method call like: obj.Method(...)
    // Conservative lookahead to avoid backtracking: Ident '.' Ident '('
    if (nextTok.kind == TokenKind::Dot)
    {
        const Token t2 = peek(2);
        const Token t3 = peek(3);
        if (t2.kind == TokenKind::Identifier && t3.kind == TokenKind::LParen)
        {
            // Now parse a full expression; top-level must be a MethodCallExpr.
            auto expr = parseExpression(/*min_prec=*/0);
            if (expr && dynamic_cast<MethodCallExpr *>(expr.get()) != nullptr)
            {
                auto stmt = std::make_unique<CallStmt>();
                stmt->loc = identTok.loc;
                stmt->call = std::move(expr);
                return StmtResult(std::move(stmt));
            }

            // We saw Ident '.' Ident '(' but didn't end up with a method-call node.
            // Treat as a hard error (unknown statement), since we consumed tokens.
            reportUnknownStatement(identTok);
            resyncAfterError();
            return StmtResult(StmtPtr{});
        }

        // Not a method invocation start; let other statement parsers try.
        return std::nullopt;
    }

    // Path B (existing): bare SUB call: GREET("Alice")
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

    // Parse name(...) and accept only if it is a CallExpr (not an array ref).
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

Parser::StmtResult Parser::parseLeadingLineNumberError()
{
    if (!at(TokenKind::Number))
        return std::nullopt;

    reportUnexpectedLineNumber(peek());
    resyncAfterError();
    return StmtResult(StmtPtr{});
}

void Parser::reportUnexpectedLineNumber(const Token &tok)
{
    if (emitter_)
    {
        std::string message = "unexpected line number '" + tok.lexeme + "' before statement";
        emitter_->emit(il::support::Severity::Error,
                       "B0001",
                       tok.loc,
                       static_cast<uint32_t>(tok.lexeme.size()),
                       std::move(message));
    }
    else
    {
        std::fprintf(stderr, "unexpected line number '%s' before statement\n", tok.lexeme.c_str());
    }
}

void Parser::reportMissingCallParenthesis(const Token &identTok, const Token &nextTok)
{
    auto diagLoc = nextTok.loc.hasLine() ? nextTok.loc : identTok.loc;
    uint32_t length = nextTok.lexeme.empty() ? 1 : static_cast<uint32_t>(nextTok.lexeme.size());
    if (emitter_)
    {
        std::string message = "expected '(' after procedure name '" + identTok.lexeme +
                              "' in procedure call statement";
        emitter_->emit(il::support::Severity::Error, "B0001", diagLoc, length, std::move(message));
    }
    else
    {
        std::fprintf(stderr,
                     "expected '(' after procedure name '%s' in procedure call statement\n",
                     identTok.lexeme.c_str());
    }
}

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
        std::fprintf(
            stderr, "expected procedure call after identifier '%s'\n", identTok.lexeme.c_str());
    }
}

void Parser::reportUnknownStatement(const Token &tok)
{
    if (emitter_)
    {
        std::string message =
            "unknown statement '" + tok.lexeme + "'; expected keyword or procedure call";
        emitter_->emit(il::support::Severity::Error,
                       "B0001",
                       tok.loc,
                       static_cast<uint32_t>(tok.lexeme.size()),
                       std::move(message));
    }
    else
    {
        std::fprintf(stderr,
                     "unknown statement '%s'; expected keyword or procedure call\n",
                     tok.lexeme.c_str());
    }
}

void Parser::resyncAfterError()
{
    syncToStmtBoundary();
}

/// @brief Check whether @p kind marks the beginning of a statement.
///
/// @details Some keywords are used both as statement leaders and as infix
///          operators.  The parser explicitly filters out logical operators and
///          recognises structural keywords such as THEN and ELSE so the caller
///          can treat them as implicit statement boundaries when splitting
///          colon-separated sequences.
///
/// @param kind Token to examine.
/// @return True when a handler or structural keyword introduces a new statement.
bool Parser::isStatementStart(TokenKind kind) const
{
    switch (kind)
    {
        case TokenKind::KeywordAnd:
        case TokenKind::KeywordOr:
        case TokenKind::KeywordNot:
        case TokenKind::KeywordAndAlso:
        case TokenKind::KeywordOrElse:
            return false;
        case TokenKind::KeywordThen:
        case TokenKind::KeywordElse:
        case TokenKind::KeywordElseIf:
        case TokenKind::KeywordWend:
        case TokenKind::KeywordTo:
        case TokenKind::KeywordStep:
        case TokenKind::KeywordAs:
            return true;
        default:
            break;
    }
    return statementRegistry().contains(kind);
}

/// @brief Parse a `LET` assignment statement.
///
/// @details Consumes the keyword, parses the assignment target using
///          @ref Parser::parsePrimary so either variables or array elements are
///          accepted, requires an equals sign, and finally lowers the right-hand
///          side via @ref Parser::parseExpression.  The resulting AST node owns
///          both sub-expressions and captures the original source location for
///          diagnostics.
///
/// @return Newly allocated @ref LetStmt describing the assignment.
StmtPtr Parser::parseLetStatement()
{
    auto loc = peek().loc;
    consume(); // LET
    auto target = parsePrimary();
    expect(TokenKind::Equal);
    auto e = parseExpression();
    auto stmt = std::make_unique<LetStmt>();
    stmt->loc = loc;
    stmt->target = std::move(target);
    stmt->expr = std::move(e);
    return stmt;
}

/// @brief Derive a BASIC type from an identifier suffix.
///
/// @details BASIC allows trailing sigils (%, &, $, !, #) to convey type hints.
///          The parser applies those rules whenever a declaration omits an
///          explicit `AS <type>` clause so semantic analysis can rely on the
///          same defaults as the language runtime.
///
/// @param name Identifier to inspect.
/// @return Corresponding BASIC type; defaults to Type::I64 for unadorned names.
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

/// @brief Parse a type keyword following an `AS` clause.
///
/// @details Supports BOOLEAN, DOUBLE, SINGLE, STRING, and INTEGER synonyms.  The
///          lexer only produces a dedicated token for BOOLEAN, so the parser
///          accepts the remaining keywords as identifiers.  Unknown tokens fall
///          back to INTEGER semantics so the language's implicit typing rules
///          remain intact without flooding users with diagnostics.
///
/// @return BASIC type corresponding to the recognised keyword; defaults to Type::I64.
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

/// @brief Parse a BASIC `CLASS` declaration.
///
/// @details Captures the class name, iterates over field declarations introduced
///          by `<identifier> AS <type>`, and gathers nested member procedures.
///          Constructors (`SUB NEW`) are normalised to identifiers so downstream
///          passes treat them consistently with other subs.  The parser consumes
///          trailing line numbers to support legacy dialects and stops at
///          `END CLASS`.
///
/// @return Newly allocated @ref ClassDecl describing the parsed declaration.
StmtPtr Parser::parseClassDecl()
{
    auto loc = peek().loc;
    consume(); // CLASS

    Token nameTok = expect(TokenKind::Identifier);

    auto decl = std::make_unique<ClassDecl>();
    decl->loc = loc;
    if (nameTok.kind == TokenKind::Identifier)
        decl->name = nameTok.lexeme;

    auto equalsIgnoreCase = [](const std::string &lhs, std::string_view rhs)
    {
        if (lhs.size() != rhs.size())
            return false;
        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
            unsigned char lc = static_cast<unsigned char>(lhs[i]);
            unsigned char rc = static_cast<unsigned char>(rhs[i]);
            if (std::toupper(lc) != std::toupper(rc))
                return false;
        }
        return true;
    };

    if (at(TokenKind::EndOfLine))
        consume();

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordClass)
            break;

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::Identifier && peek(2).kind == TokenKind::KeywordAs)
            {
                consume();
                continue;
            }
        }

        if (!(at(TokenKind::Identifier) && peek(1).kind == TokenKind::KeywordAs))
            break;

        Token fieldNameTok = expect(TokenKind::Identifier);
        if (fieldNameTok.kind != TokenKind::Identifier)
            break;

        Token asTok = expect(TokenKind::KeywordAs);
        if (asTok.kind != TokenKind::KeywordAs)
            continue;

        Type fieldType = Type::I64;
        if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
        {
            fieldType = parseTypeKeyword();
        }
        else
        {
            expect(TokenKind::Identifier);
        }

        ClassDecl::Field field;
        field.name = fieldNameTok.lexeme;
        field.type = fieldType;
        decl->fields.push_back(std::move(field));

        if (at(TokenKind::EndOfLine))
            consume();
    }

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordClass)
            break;

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::KeywordSub || nextKind == TokenKind::KeywordFunction ||
                nextKind == TokenKind::KeywordDestructor ||
                (nextKind == TokenKind::KeywordEnd && peek(2).kind == TokenKind::KeywordClass))
            {
                consume();
                continue;
            }
        }

        if (at(TokenKind::KeywordSub))
        {
            auto subLoc = peek().loc;
            consume(); // SUB
            Token subNameTok;
            if (peek().kind == TokenKind::KeywordNew)
            {
                subNameTok = peek();
                consume();
                subNameTok.kind = TokenKind::Identifier;
            }
            else
            {
                subNameTok = expect(TokenKind::Identifier);
                if (subNameTok.kind != TokenKind::Identifier)
                    break;
            }

            if (equalsIgnoreCase(subNameTok.lexeme, "NEW"))
            {
                auto ctor = std::make_unique<ConstructorDecl>();
                ctor->loc = subLoc;
                ctor->params = parseParamList();
                parseProcedureBody(TokenKind::KeywordSub, ctor->body);
                decl->members.push_back(std::move(ctor));
                continue;
            }

            auto method = std::make_unique<MethodDecl>();
            method->loc = subLoc;
            method->name = subNameTok.lexeme;
            method->params = parseParamList();
            parseProcedureBody(TokenKind::KeywordSub, method->body);
            decl->members.push_back(std::move(method));
            continue;
        }

        if (at(TokenKind::KeywordFunction))
        {
            auto fnLoc = peek().loc;
            consume(); // FUNCTION
            Token fnNameTok = expect(TokenKind::Identifier);
            if (fnNameTok.kind != TokenKind::Identifier)
                break;

            auto method = std::make_unique<MethodDecl>();
            method->loc = fnLoc;
            method->name = fnNameTok.lexeme;
            method->ret = typeFromSuffix(fnNameTok.lexeme);
            method->params = parseParamList();
            parseProcedureBody(TokenKind::KeywordFunction, method->body);
            decl->members.push_back(std::move(method));
            continue;
        }

        if (at(TokenKind::KeywordDestructor))
        {
            auto dtorLoc = peek().loc;
            consume(); // DESTRUCTOR
            auto dtor = std::make_unique<DestructorDecl>();
            dtor->loc = dtorLoc;
            parseProcedureBody(TokenKind::KeywordDestructor, dtor->body);
            decl->members.push_back(std::move(dtor));
            continue;
        }

        break;
    }

    while (at(TokenKind::EndOfLine))
        consume();

    if (at(TokenKind::Number) && peek(1).kind == TokenKind::KeywordEnd &&
        peek(2).kind == TokenKind::KeywordClass)
    {
        consume();
    }

    expect(TokenKind::KeywordEnd);
    expect(TokenKind::KeywordClass);

    return decl;
}

/// @brief Parse a BASIC `TYPE` declaration used for user-defined records.
///
/// @details Collects field declarations consisting of identifier/type pairs,
///          tolerates legacy line-number prefixes, and terminates at
///          `END TYPE`.  Each field is stored with the resolved BASIC type so
///          semantic analysis can validate member accesses.
///
/// @return Newly allocated @ref TypeDecl describing the record type.
StmtPtr Parser::parseTypeDecl()
{
    auto loc = peek().loc;
    consume(); // TYPE

    Token nameTok = expect(TokenKind::Identifier);

    auto decl = std::make_unique<TypeDecl>();
    decl->loc = loc;
    if (nameTok.kind == TokenKind::Identifier)
        decl->name = nameTok.lexeme;

    if (at(TokenKind::EndOfLine))
        consume();

    while (!at(TokenKind::EndOfFile))
    {
        while (at(TokenKind::EndOfLine))
            consume();

        if (at(TokenKind::KeywordEnd) && peek(1).kind == TokenKind::KeywordType)
            break;

        if (at(TokenKind::Number))
        {
            TokenKind nextKind = peek(1).kind;
            if (nextKind == TokenKind::Identifier ||
                (nextKind == TokenKind::KeywordEnd && peek(2).kind == TokenKind::KeywordType))
            {
                consume();
                continue;
            }
        }

        Token fieldNameTok = expect(TokenKind::Identifier);
        if (fieldNameTok.kind != TokenKind::Identifier)
            break;

        Token asTok = expect(TokenKind::KeywordAs);
        if (asTok.kind != TokenKind::KeywordAs)
            continue;

        Type fieldType = Type::I64;
        if (at(TokenKind::KeywordBoolean) || at(TokenKind::Identifier))
        {
            fieldType = parseTypeKeyword();
        }
        else
        {
            expect(TokenKind::Identifier);
        }

        TypeDecl::Field field;
        field.name = fieldNameTok.lexeme;
        field.type = fieldType;
        decl->fields.push_back(std::move(field));

        if (at(TokenKind::EndOfLine))
            consume();
    }

    while (at(TokenKind::EndOfLine))
        consume();

    if (at(TokenKind::Number) && peek(1).kind == TokenKind::KeywordEnd &&
        peek(2).kind == TokenKind::KeywordType)
    {
        consume();
    }

    expect(TokenKind::KeywordEnd);
    expect(TokenKind::KeywordType);

    return decl;
}

/// @brief Parse the `DELETE` statement for object lifetimes.
///
/// @details Consumes the keyword and parses a single expression that evaluates
///          to the target object reference.  Diagnostics are deferred to later
///          phases; the parser simply records the expression for lowering.
///
/// @return Newly allocated @ref DeleteStmt representing the statement.
StmtPtr Parser::parseDeleteStatement()
{
    auto loc = peek().loc;
    consume(); // DELETE

    auto target = parseExpression();
    auto stmt = std::make_unique<DeleteStmt>();
    stmt->loc = loc;
    stmt->target = std::move(target);
    return stmt;
}

/// @brief Parse a parenthesised parameter list.
///
/// @details Accepts comma-separated identifiers, honours trailing "()" to mark
///          array parameters, and infers default types using BASIC suffix rules.
///          The helper aborts early when no opening parenthesis is present so
///          callers can reuse it for optional parameter sections.  Any
///          structural mismatches are reported via @ref expect, ensuring
///          diagnostics remain consistent.
///
/// @return Vector of parameters, possibly empty if no list is present.
std::vector<Param> Parser::parseParamList()
{
    std::vector<Param> params;
    if (!at(TokenKind::LParen))
        return params;
    consume(); // (
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

/// @brief Parse the header of a `FUNCTION` declaration.
///
/// @details Reads the function name, infers the default return type from any
///          suffix, and parses an optional parameter list.  The body remains
///          unparsed so callers can decide when to collect statements.  The
///          returned node is pre-populated with location and signature
///          information ready for semantic analysis.
///
/// @return Function declaration node containing signature metadata.
std::unique_ptr<FunctionDecl> Parser::parseFunctionHeader()
{
    auto loc = peek().loc;
    consume(); // FUNCTION
    Token nameTok = expect(TokenKind::Identifier);
    auto fn = std::make_unique<FunctionDecl>();
    fn->loc = loc;
    fn->name = nameTok.lexeme;
    fn->ret = typeFromSuffix(nameTok.lexeme);
    fn->params = parseParamList();
    if (at(TokenKind::KeywordAs))
    {
        consume();
        fn->explicitRetType = parseBasicType();
    }
    return fn;
}

/// @brief Parse a sequence of statements terminated by `END <keyword>`.
///
/// @details Delegates to @ref StatementSequencer so nested statements can reuse
///          colon-separated parsing logic.  The callback consumes the terminating
///          `END` token followed by the expected keyword, leaving the parser
///          positioned after the body.  All parsed statements are appended to
///          @p body and the location of the `END` keyword is returned for later
///          diagnostics.
///
/// @param endKind Keyword expected after END to close the body.
/// @param body Destination vector for parsed statements.
/// @return Source location of the terminating END token.
il::support::SourceLoc Parser::parseProcedureBody(TokenKind endKind, std::vector<StmtPtr> &body)
{
    auto ctx = statementSequencer();
    auto info =
        ctx.collectStatements([&](int, il::support::SourceLoc)
                              { return at(TokenKind::KeywordEnd) && peek(1).kind == endKind; },
                              [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &)
                              {
                                  consume();
                                  consume();
                              },
                              body);
    return info.loc;
}

/// @brief Parse statements comprising a function body.
///
/// @details Invokes @ref parseProcedureBody with the `FUNCTION` terminator so
///          the helper collects statements until `END FUNCTION` is encountered.
///          The resulting end location is stored on the declaration for
///          downstream diagnostics.
///
/// @param fn Function declaration to populate with body statements.
void Parser::parseFunctionBody(FunctionDecl *fn)
{
    fn->endLoc = parseProcedureBody(TokenKind::KeywordFunction, fn->body);
}

/// @brief Parse a complete `FUNCTION` declaration including body.
///
/// @details Builds the signature via @ref parseFunctionHeader, records the name
///          for later procedure-call disambiguation, and then parses the body.
///          The returned node owns all constituent statements.
///
/// @return Owned @ref FunctionDecl statement node including body.
StmtPtr Parser::parseFunctionStatement()
{
    auto fn = parseFunctionHeader();
    noteProcedureName(fn->name);
    parseFunctionBody(fn.get());
    return fn;
}

/// @brief Parse a complete `SUB` procedure declaration.
///
/// @details Captures the procedure name, parses an optional parameter list,
///          records the name for call recognition, and gathers statements until
///          `END SUB`.  The resulting AST node owns the collected body.
///
/// @return Owned @ref SubDecl statement node including body.
StmtPtr Parser::parseSubStatement()
{
    auto loc = peek().loc;
    consume(); // SUB
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
