//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the core statement parsing routines for the BASIC frontend.  These
// helpers register keyword handlers, parse top-level statements, and manage the
// dispatch scaffolding shared by specialised translation units.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Core statement parsing utilities for the BASIC front end.
/// @details The functions defined here install parser handlers, maintain symbol
///          tables for procedures, and implement the shared statement-dispatch
///          loop that delegates to statement-specific translation units.

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

/// @brief Register core (non-OOP) statement parsers.
/// @details Adds handlers for LET, FUNCTION, and SUB so the dispatcher can
///          route those keywords to their parsing routines during statement
///          parsing.
/// @param registry Statement parser registry being populated.
void Parser::registerCoreParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordLet, &Parser::parseLetStatement);
    registry.registerHandler(TokenKind::KeywordFunction, &Parser::parseFunctionStatement);
    registry.registerHandler(TokenKind::KeywordSub, &Parser::parseSubStatement);
}

#if VIPER_ENABLE_OOP
/// @brief Register OOP-specific statement parsers when the feature is enabled.
/// @details Installs handlers for CLASS, TYPE, and DELETE keywords so the parser
///          can interpret object-oriented constructs.
/// @param registry Statement parser registry being populated.
void Parser::registerOopParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordClass, &Parser::parseClassDecl);
    registry.registerHandler(TokenKind::KeywordType, &Parser::parseTypeDecl);
    registry.registerHandler(TokenKind::KeywordDelete, &Parser::parseDeleteStatement);
}
#endif

/// @brief Record the name of a procedure encountered during parsing.
/// @details Inserts the name into the known-procedure set so forward references
///          can be resolved when parsing CALL or GOSUB statements.
/// @param name Fully qualified procedure name.
void Parser::noteProcedureName(std::string name)
{
    knownProcedures_.insert(std::move(name));
}

/// @brief Query whether a procedure name has been seen in the current module.
/// @details Used to disambiguate identifiers that may refer to procedures or
///          variables when parsing statements.
/// @param name Candidate procedure name.
/// @return True when @p name was previously recorded via @ref noteProcedureName.
bool Parser::isKnownProcedureName(const std::string &name) const
{
    return knownProcedures_.find(name) != knownProcedures_.end();
}

/// @brief Parse a single statement at the current token position.
/// @details Uses the statement registry to locate a handler for the leading
///          token and invokes it, falling back to CALL statement synthesis when
///          encountering identifier-invocation syntax.  Reports diagnostics and
///          performs error recovery when the token sequence does not correspond
///          to any known statement.
/// @param line Line number supplied by the caller for diagnostic accuracy.
/// @return Parsed statement node or nullptr when the parser recovered from an error.
StmtPtr Parser::parseStatement(int line)
{
    const Token &tok = peek();
    std::string tokLexeme = tok.lexeme;
    auto tokLoc = tok.loc;
    if (tok.kind == TokenKind::Number)
    {
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           tok.loc,
                           static_cast<uint32_t>(tok.lexeme.size()),
                           "unexpected line number");
        }
        else
        {
            std::fprintf(stderr, "unexpected line number '%s'\n", tok.lexeme.c_str());
        }

        while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
        {
            consume();
        }
        return nullptr;
    }

    const auto kind = tok.kind;
    const auto [noArg, withLine] = statementRegistry().lookup(kind);
    if (noArg)
        return (this->*noArg)();
    if (withLine)
        return (this->*withLine)(line);
    if (tok.kind == TokenKind::Identifier && peek(1).kind == TokenKind::LParen)
    {
        std::string ident = tokLexeme;
        auto identLoc = tokLoc;
        auto expr = parseArrayOrVar();
        if (auto *callExpr = dynamic_cast<CallExpr *>(expr.get()))
        {
            auto stmt = std::make_unique<CallStmt>();
            stmt->loc = identLoc;
            stmt->call.reset(static_cast<CallExpr *>(expr.release()));
            return stmt;
        }
        if (emitter_)
        {
            std::string msg = std::string("unknown statement '") + ident + "'";
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           identLoc,
                           static_cast<uint32_t>(ident.size()),
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr, "unknown statement '%s'\n", ident.c_str());
        }
        while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
        {
            consume();
        }
        return nullptr;
    }
    if (tok.kind == TokenKind::Identifier && isKnownProcedureName(tokLexeme) &&
        peek(1).kind != TokenKind::LParen)
    {
        std::string ident = tokLexeme;
        auto nextTok = peek(1);
        auto diagLoc = nextTok.loc.isValid() ? nextTok.loc : tokLoc;
        uint32_t length = 1;
        if (emitter_)
        {
            std::string msg = "expected '(' after procedure name '" + ident + "'";
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           diagLoc,
                           length,
                           std::move(msg));
        }
        else
        {
            std::fprintf(stderr,
                         "expected '(' after procedure name '%s'\n",
                         ident.c_str());
        }
        while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
        {
            consume();
        }
        return nullptr;
    }
    if (emitter_)
    {
            std::string msg = std::string("unknown statement '") + tokLexeme + "'";
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           tokLoc,
                           static_cast<uint32_t>(tokLexeme.size()),
                           std::move(msg));
    }
    else
    {
        std::fprintf(stderr, "unknown statement '%s'\n", tok.lexeme.c_str());
    }

    while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine))
    {
        consume();
    }

    return nullptr;
}

/// @brief Determine whether a token kind begins a BASIC statement.
/// @details Used by statement sequencing logic to decide if a new statement
///          should start at the current token during block parsing.
/// @param kind Token kind to inspect.
/// @return True when @p kind can introduce a statement.
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
/// @brief Parse a LET statement assigning to a variable or array element.
/// @details Supports optional omission of the LET keyword, distinguishes between
///          variable and array destinations, and parses the assignment expression
///          while respecting colon-separated chained statements.
/// @return AST node representing the LET statement.
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
/// @brief Infer a BASIC type from a name suffix.
/// @details Examines trailing sigils (e.g. `$`, `%`, `!`) and maps them to the
///          corresponding BASIC type.  When no suffix appears the routine
///          returns BASIC's default integer type.
/// @param name Identifier being inspected.
/// @return Inferred BASIC type.
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

/// @brief Parse an explicit type keyword.
/// @details Consumes the current token when it matches a BASIC type keyword and
///          returns the corresponding enumeration value, otherwise falls back to
///          BASIC's default integer semantics.
/// @return BASIC type specified by the keyword or @ref Type::I64 when no keyword matches.
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

/// @brief Parse a function or subroutine parameter list.
/// @details Reads parameter declarations between parentheses, capturing passing
///          conventions, names, and optional type annotations.  Maintains
///          compatibility with both positional and keyword forms supported by
///          the dialect.
/// @return Ordered list of parsed parameters.
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

/// @brief Parse the header of a FUNCTION declaration.
/// @details Parses the function keyword, identifier, parameter list, and return
///          type (including suffix inference).  The body is left for subsequent
///          parsing.
/// @return Owning pointer to the partially constructed function declaration.
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
    return fn;
}

/// @brief Parse the body of a procedure until its terminating keyword.
/// @details Delegates to the statement parsing loop while tracking the source
///          location of the closing END keyword.  Accumulates statements in
///          @p body and returns the location for range tracking.
/// @param endKind Keyword that terminates the procedure (FUNCTION or SUB).
/// @param body    Destination vector receiving parsed statements.
/// @return Source location of the terminating keyword.
il::support::SourceLoc Parser::parseProcedureBody(TokenKind endKind, std::vector<StmtPtr> &body)
{
    auto ctx = statementSequencer();
    auto info = ctx.collectStatements(
        [&](int, il::support::SourceLoc)
        { return at(TokenKind::KeywordEnd) && peek(1).kind == endKind; },
        [&](int, il::support::SourceLoc, StatementSequencer::TerminatorInfo &)
        {
            consume();
            consume();
        },
        body);
    return info.loc;
}

/// @brief Parse the body associated with a function declaration.
/// @details Invokes @ref parseProcedureBody with FUNCTION terminators and stores
///          the resulting statements on the declaration.
/// @param fn Function declaration being completed.
void Parser::parseFunctionBody(FunctionDecl *fn)
{
    fn->endLoc = parseProcedureBody(TokenKind::KeywordFunction, fn->body);
}

/// @brief Parse a FUNCTION statement including header and body.
/// @details Builds the declaration header, records the procedure name, parses
///          the body, and returns the fully populated AST node.
/// @return AST node representing the function declaration.
StmtPtr Parser::parseFunctionStatement()
{
    auto fn = parseFunctionHeader();
    noteProcedureName(fn->name);
    parseFunctionBody(fn.get());
    return fn;
}

/// @brief Parse a SUB statement including header and body.
/// @details Similar to @ref parseFunctionStatement but for void subroutines;
///          records the name and parses statements until END SUB.
/// @return AST node representing the subroutine declaration.
StmtPtr Parser::parseSubStatement()
{
    auto loc = peek().loc;
    consume(); // SUB
    Token nameTok = expect(TokenKind::Identifier);
    auto sub = std::make_unique<SubDecl>();
    sub->loc = loc;
    sub->name = nameTok.lexeme;
    sub->params = parseParamList();
    noteProcedureName(sub->name);
    parseProcedureBody(TokenKind::KeywordSub, sub->body);
    return sub;
}
} // namespace il::frontends::basic
