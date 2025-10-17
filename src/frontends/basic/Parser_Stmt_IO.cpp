//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC parser entry points for input/output statements.  The
// handlers interpret PRINT, WRITE, OPEN, CLOSE, SEEK, INPUT, and LINE INPUT
// constructs, translating tokens into strongly typed AST nodes while enforcing
// separator and terminator conventions.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Parsing helpers for BASIC I/O statements.
/// @details Each function inspects the current token stream, consumes the
///          syntactic form mandated by the BASIC language reference, and
///          assembles the corresponding AST representation.  Shared validation
///          logic ensures delimiters, separators, and optional clauses follow
///          the dialect rules captured in docs/codemap.md.

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/BasicDiagnosticMessages.hpp"

#include <cstdio>
#include <cstdlib>

namespace il::frontends::basic
{

/// @brief Register BASIC I/O statement parsers with the dispatcher.
/// @details Populates the @p registry with pointers to the member functions that
///          parse each I/O statement.  The dispatcher later invokes these
///          handlers when it encounters the matching keyword at the front of the
///          token stream, keeping statement dispatch table-driven.
/// @param registry Registry that maps keywords to parser member functions.
void Parser::registerIoParsers(StatementParserRegistry &registry)
{
    registry.registerHandler(TokenKind::KeywordPrint, &Parser::parsePrintStatement);
    registry.registerHandler(TokenKind::KeywordWrite, &Parser::parseWriteStatement);
    registry.registerHandler(TokenKind::KeywordOpen, &Parser::parseOpenStatement);
    registry.registerHandler(TokenKind::KeywordClose, &Parser::parseCloseStatement);
    registry.registerHandler(TokenKind::KeywordSeek, &Parser::parseSeekStatement);
    registry.registerHandler(TokenKind::KeywordInput, &Parser::parseInputStatement);
    registry.registerHandler(TokenKind::KeywordLine, &Parser::parseLineInputStatement);
}

/// @brief Parse the PRINT statement including comma/semicolon separators.
/// @details Consumes the PRINT keyword, collects optional destinations (using
///          `#` prefixes) and iteratively parses expressions separated by commas
///          or semicolons.  Trailing separators are recorded to match the BASIC
///          newline suppression semantics.  The routine stops when it reaches a
///          line terminator or colon separator and returns a @ref PrintStmt.
/// @return Owning pointer to the populated PRINT statement node.
StmtPtr Parser::parsePrintStatement()
{
    auto loc = peek().loc;
    consume(); // PRINT
    if (at(TokenKind::Hash))
    {
        consume();
        auto stmt = std::make_unique<PrintChStmt>();
        stmt->loc = loc;
        stmt->mode = PrintChStmt::Mode::Print;
        stmt->channelExpr = parseExpression();
        stmt->trailingNewline = true;
        if (at(TokenKind::Comma))
        {
            consume();
            while (true)
            {
                if (at(TokenKind::EndOfLine) || at(TokenKind::EndOfFile) || at(TokenKind::Colon))
                    break;
                if (isStatementStart(peek().kind))
                    break;
                stmt->args.push_back(parseExpression());
                if (!at(TokenKind::Comma))
                    break;
                consume();
            }
        }
        return stmt;
    }
    auto stmt = std::make_unique<PrintStmt>();
    stmt->loc = loc;
    while (!at(TokenKind::EndOfLine) && !at(TokenKind::EndOfFile) && !at(TokenKind::Colon))
    {
        TokenKind k = peek().kind;
        if (isStatementStart(k))
            break;
        if (at(TokenKind::Comma))
        {
            consume();
            stmt->items.push_back(PrintItem{PrintItem::Kind::Comma, nullptr});
            continue;
        }
        if (at(TokenKind::Semicolon))
        {
            consume();
            stmt->items.push_back(PrintItem{PrintItem::Kind::Semicolon, nullptr});
            continue;
        }
        stmt->items.push_back(PrintItem{PrintItem::Kind::Expr, parseExpression()});
    }
    return stmt;
}

/// @brief Parse the WRITE statement with file and list handling.
/// @details Mirrors @ref parsePrintStatement but preserves the WRITE-specific
///          behaviour: expressions are delimited by commas only and newline
///          suppression is not tracked.  Optional `#<channel>` targets are
///          parsed up front before the argument list is collected.
/// @return AST node representing the WRITE statement.
StmtPtr Parser::parseWriteStatement()
{
    auto loc = peek().loc;
    consume(); // WRITE
    expect(TokenKind::Hash);
    auto stmt = std::make_unique<PrintChStmt>();
    stmt->loc = loc;
    stmt->mode = PrintChStmt::Mode::Write;
    stmt->trailingNewline = true;
    stmt->channelExpr = parseExpression();
    expect(TokenKind::Comma);
    while (true)
    {
        stmt->args.push_back(parseExpression());
        if (!at(TokenKind::Comma))
            break;
        consume();
    }
    return stmt;
}

/// @brief Parse an OPEN statement configuring file channels.
/// @details Consumes the OPEN keyword, reads the path expression, and requires a
///          `FOR <mode> AS #<channel>` clause.  Mode keywords are mapped to the
///          appropriate enum while diagnostics fire on unrecognised tokens.
/// @return AST node capturing the OPEN statement parameters.
StmtPtr Parser::parseOpenStatement()
{
    auto loc = peek().loc;
    consume(); // OPEN
    auto stmt = std::make_unique<OpenStmt>();
    stmt->loc = loc;
    stmt->pathExpr = parseExpression();
    expect(TokenKind::KeywordFor);
    if (at(TokenKind::KeywordInput))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Input;
    }
    else if (at(TokenKind::KeywordOutput))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Output;
    }
    else if (at(TokenKind::KeywordAppend))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Append;
    }
    else if (at(TokenKind::KeywordBinary))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Binary;
    }
    else if (at(TokenKind::KeywordRandom))
    {
        consume();
        stmt->mode = OpenStmt::Mode::Random;
    }
    else
    {
        Token unexpected = consume();
        if (emitter_)
        {
            emitter_->emitExpected(unexpected.kind, TokenKind::KeywordInput, unexpected.loc);
        }
    }
    expect(TokenKind::KeywordAs);
    expect(TokenKind::Hash);
    stmt->channelExpr = parseExpression();
    return stmt;
}

/// @brief Parse a CLOSE statement that shuts down a channel.
/// @details Expects `CLOSE #<expr>` and records the channel expression for later
///          validation.  Additional operands result in diagnostics emitted by the
///          parser.
/// @return AST node representing the CLOSE command.
StmtPtr Parser::parseCloseStatement()
{
    auto loc = peek().loc;
    consume(); // CLOSE
    auto stmt = std::make_unique<CloseStmt>();
    stmt->loc = loc;
    expect(TokenKind::Hash);
    stmt->channelExpr = parseExpression();
    return stmt;
}

/// @brief Parse the SEEK statement for repositioning file handles.
/// @details Requires a channel designator and a target record expression.  The
///          parser enforces the presence of the comma separator so the AST
///          conveys both operands explicitly to the lowering phase.
/// @return AST node for the SEEK invocation.
StmtPtr Parser::parseSeekStatement()
{
    auto loc = peek().loc;
    consume(); // SEEK
    auto stmt = std::make_unique<SeekStmt>();
    stmt->loc = loc;
    expect(TokenKind::Hash);
    stmt->channelExpr = parseExpression();
    expect(TokenKind::Comma);
    stmt->positionExpr = parseExpression();
    return stmt;
}

/// @brief Parse the INPUT statement supporting prompt text and variable lists.
/// @details Handles optional prompt strings (followed by semicolons), optional
///          `;` suppressors, and then collects a comma-delimited list of target
///          expressions.  The resulting AST differentiates between the prompt
///          expression and the variables so later passes can emit runtime calls
///          accurately.
/// @return AST node for the INPUT statement.
StmtPtr Parser::parseInputStatement()
{
    auto loc = peek().loc;
    consume(); // INPUT
    if (at(TokenKind::Hash))
    {
        consume();
        Token channelTok = expect(TokenKind::Number);
        int channel = std::atoi(channelTok.lexeme.c_str());
        expect(TokenKind::Comma);
        Token targetTok = expect(TokenKind::Identifier);
        auto stmt = std::make_unique<InputChStmt>();
        stmt->loc = loc;
        stmt->channel = channel;
        stmt->target.name = targetTok.lexeme;
        stmt->target.loc = targetTok.loc;

        if (at(TokenKind::Comma))
        {
            Token extra = peek();
            if (emitter_)
            {
                emitter_->emit(il::support::Severity::Error,
                               "B0001",
                               extra.loc,
                               1,
                               "INPUT # with multiple targets not yet supported");
            }
            else
            {
                std::fprintf(stderr, "INPUT # with multiple targets not yet supported\n");
            }
            while (!at(TokenKind::EndOfFile) && !at(TokenKind::EndOfLine) && !at(TokenKind::Colon))
            {
                consume();
            }
        }

        return stmt;
    }
    ExprPtr prompt;
    if (at(TokenKind::String))
    {
        auto s = std::make_unique<StringExpr>();
        s->loc = peek().loc;
        s->value = peek().lexeme;
        prompt = std::move(s);
        consume();
        expect(TokenKind::Comma);
    }
    auto stmt = std::make_unique<InputStmt>();
    stmt->loc = loc;
    stmt->prompt = std::move(prompt);

    Token nameTok = expect(TokenKind::Identifier);
    stmt->vars.push_back(nameTok.lexeme);

    while (at(TokenKind::Comma))
    {
        consume();
        Token nextTok = expect(TokenKind::Identifier);
        stmt->vars.push_back(nextTok.lexeme);
    }

    return stmt;
}

/// @brief Parse the LINE INPUT statement variant.
/// @details Similar to @ref parseInputStatement but only accepts a single target
///          variable.  Optional prompt handling mirrors INPUT while the AST
///          tracks the `;` newline suppression flag.
/// @return AST node representing the LINE INPUT statement.
StmtPtr Parser::parseLineInputStatement()
{
    auto loc = peek().loc;
    consume(); // LINE
    expect(TokenKind::KeywordInput);
    expect(TokenKind::Hash);
    auto stmt = std::make_unique<LineInputChStmt>();
    stmt->loc = loc;
    stmt->channelExpr = parseExpression();
    expect(TokenKind::Comma);
    auto target = parseArrayOrVar();
    Expr *rawTarget = target.get();
    if (rawTarget && !dynamic_cast<VarExpr *>(rawTarget) && !dynamic_cast<ArrayExpr *>(rawTarget))
    {
        il::support::SourceLoc diagLoc = rawTarget->loc.isValid() ? rawTarget->loc : loc;
        if (emitter_)
        {
            emitter_->emit(il::support::Severity::Error,
                           "B0001",
                           diagLoc,
                           1,
                           "expected variable");
        }
        auto fallback = std::make_unique<VarExpr>();
        fallback->loc = diagLoc;
        stmt->targetVar = std::move(fallback);
    }
    else
    {
        stmt->targetVar = std::move(target);
    }
    return stmt;
}

} // namespace il::frontends::basic

