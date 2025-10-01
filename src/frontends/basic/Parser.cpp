// File: src/frontends/basic/Parser.cpp
// Purpose: Implements program-level orchestration for the BASIC parser,
//          delegating statement parsing to Parser_Stmt.cpp.
// Key invariants: Relies on token buffer for lookahead.
// Ownership/Lifetime: Parser owns tokens produced by lexer.
// License: MIT (see LICENSE).
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include <array>
#include <cstdlib>
#include <utility>

namespace il::frontends::basic
{
/// @brief Construct a parser for the given source.
/// @param src Full BASIC source to parse.
/// @param file_id Identifier for diagnostics.
/// @param emitter Destination for emitted diagnostics.
/// @note Initializes the token buffer with the first token for lookahead.
Parser::Parser(std::string_view src, uint32_t file_id, DiagnosticEmitter *emitter)
    : lexer_(src, file_id), emitter_(emitter)
{
    tokens_.push_back(lexer_.next());
    static constexpr std::array<std::pair<TokenKind, StmtHandler>, 22> kStatementHandlers = {{
        {TokenKind::KeywordPrint, {&Parser::parsePrint, nullptr}},
        {TokenKind::KeywordLet, {&Parser::parseLet, nullptr}},
        {TokenKind::KeywordIf, {nullptr, &Parser::parseIf}},
        {TokenKind::KeywordWhile, {&Parser::parseWhile, nullptr}},
        {TokenKind::KeywordDo, {&Parser::parseDo, nullptr}},
        {TokenKind::KeywordFor, {&Parser::parseFor, nullptr}},
        {TokenKind::KeywordNext, {&Parser::parseNext, nullptr}},
        {TokenKind::KeywordExit, {&Parser::parseExit, nullptr}},
        {TokenKind::KeywordGoto, {&Parser::parseGoto, nullptr}},
        {TokenKind::KeywordOpen, {&Parser::parseOpen, nullptr}},
        {TokenKind::KeywordClose, {&Parser::parseClose, nullptr}},
        {TokenKind::KeywordOn, {&Parser::parseOnErrorGoto, nullptr}},
        {TokenKind::KeywordResume, {&Parser::parseResume, nullptr}},
        {TokenKind::KeywordEnd, {&Parser::parseEnd, nullptr}},
        {TokenKind::KeywordInput, {&Parser::parseInput, nullptr}},
        {TokenKind::KeywordLine, {&Parser::parseLineInput, nullptr}},
        {TokenKind::KeywordDim, {&Parser::parseDim, nullptr}},
        {TokenKind::KeywordRedim, {&Parser::parseReDim, nullptr}},
        {TokenKind::KeywordRandomize, {&Parser::parseRandomize, nullptr}},
        {TokenKind::KeywordFunction, {&Parser::parseFunction, nullptr}},
        {TokenKind::KeywordSub, {&Parser::parseSub, nullptr}},
        {TokenKind::KeywordReturn, {&Parser::parseReturn, nullptr}},
    }};
    for (const auto &[kind, handler] : kStatementHandlers)
    {
        stmtHandlers_[static_cast<std::size_t>(kind)] = handler;
    }
}

Parser::StatementContext::StatementContext(Parser &parser) : parser_(parser) {}

void Parser::StatementContext::skipLeadingSeparator()
{
    if (parser_.at(TokenKind::EndOfLine))
    {
        parser_.consume();
        lastSeparator_ = SeparatorKind::LineBreak;
    }
    else if (parser_.at(TokenKind::Colon))
    {
        parser_.consume();
        lastSeparator_ = SeparatorKind::Colon;
    }
    else
    {
        lastSeparator_ = SeparatorKind::None;
    }
}

bool Parser::StatementContext::skipLineBreaks()
{
    bool consumed = false;
    while (parser_.at(TokenKind::EndOfLine))
    {
        parser_.consume();
        consumed = true;
    }
    if (consumed)
        lastSeparator_ = SeparatorKind::LineBreak;
    return consumed;
}

void Parser::StatementContext::skipStatementSeparator()
{
    if (parser_.at(TokenKind::Colon))
    {
        parser_.consume();
        lastSeparator_ = SeparatorKind::Colon;
    }
    else if (parser_.at(TokenKind::EndOfLine))
    {
        parser_.consume();
        lastSeparator_ = SeparatorKind::LineBreak;
    }
    else
    {
        lastSeparator_ = SeparatorKind::None;
    }
}

void Parser::StatementContext::withOptionalLineNumber(const std::function<void(int)> &fn)
{
    int line = 0;
    if (pendingLine_ >= 0)
    {
        line = pendingLine_;
        pendingLine_ = -1;
    }
    else if (parser_.at(TokenKind::Number))
    {
        line = std::atoi(parser_.peek().lexeme.c_str());
        parser_.consume();
    }
    fn(line);
}

void Parser::StatementContext::stashPendingLine(int line)
{
    pendingLine_ = line;
}

Parser::StatementContext::SeparatorKind Parser::StatementContext::lastSeparator() const
{
    return lastSeparator_;
}

Parser::StatementContext::TerminatorInfo Parser::StatementContext::consumeStatementBody(
    const TerminatorPredicate &isTerminator,
    const TerminatorConsumer &onTerminator,
    std::vector<StmtPtr> &dst)
{
    TerminatorInfo info;
    skipLeadingSeparator();
    while (!parser_.at(TokenKind::EndOfFile))
    {
        skipLineBreaks();
        if (parser_.at(TokenKind::EndOfFile))
            break;

        bool done = false;
        withOptionalLineNumber(
            [&](int line)
            {
                if (isTerminator(line))
                {
                    info.line = line;
                    info.loc = parser_.peek().loc;
                    onTerminator(line, info);
                    done = true;
                    return;
                }
                auto stmt = parser_.parseStatement(line);
                stmt->line = line;
                dst.push_back(std::move(stmt));
            });
        if (done)
            break;
        skipStatementSeparator();
    }
    return info;
}

Parser::StatementContext::TerminatorInfo Parser::StatementContext::consumeStatementBody(
    TokenKind terminator, std::vector<StmtPtr> &dst)
{
    auto predicate = [&](int) { return parser_.at(terminator); };
    auto consumer = [&](int, TerminatorInfo &) { parser_.consume(); };
    return consumeStatementBody(predicate, consumer, dst);
}

Parser::StatementContext Parser::statementContext()
{
    return StatementContext(*this);
}

StmtPtr Parser::parseStatementLine(StatementContext &ctx)
{
    std::vector<StmtPtr> stmts;
    int lineNumber = 0;
    bool haveLine = false;
    auto predicate = [&](int line)
    {
        if (!haveLine)
        {
            haveLine = true;
            lineNumber = line;
            return false;
        }

        if (ctx.lastSeparator() == StatementContext::SeparatorKind::LineBreak)
        {
            if (line > 0)
                ctx.stashPendingLine(line);
            return true;
        }

        if (ctx.lastSeparator() != StatementContext::SeparatorKind::Colon)
        {
            if (line > 0)
                ctx.stashPendingLine(line);
            return true;
        }

        if (line > 0 && line != lineNumber)
        {
            ctx.stashPendingLine(line);
            return true;
        }
        return false;
    };
    auto consumer = [&](int line, StatementContext::TerminatorInfo &)
    {
        if (line > 0)
            ctx.stashPendingLine(line);
    };

    ctx.consumeStatementBody(predicate, consumer, stmts);

    if (stmts.empty())
        return nullptr;

    if (!haveLine && !stmts.empty())
        lineNumber = stmts.front()->line;

    if (lineNumber != 0)
    {
        for (auto &stmt : stmts)
        {
            if (stmt)
                stmt->line = lineNumber;
        }
    }

    if (stmts.size() == 1)
        return std::move(stmts.front());

    auto list = std::make_unique<StmtList>();
    list->line = lineNumber;
    list->loc = stmts.front()->loc;
    list->stmts = std::move(stmts);
    return list;
}

/// @brief Parse the entire BASIC program.
/// @return Root program node with separated procedure and main sections.
/// @note Assumes all procedures appear before the first main statement.
std::unique_ptr<Program> Parser::parseProgram()
{
    auto prog = std::make_unique<Program>();
    prog->loc = peek().loc;
    bool inMain = false;
    auto ctx = statementContext();
    while (!at(TokenKind::EndOfFile))
    {
        ctx.skipLineBreaks();
        if (at(TokenKind::EndOfFile))
            break;
        auto root = parseStatementLine(ctx);
        if (!root)
            continue;
        if (!inMain &&
            (dynamic_cast<FunctionDecl *>(root.get()) || dynamic_cast<SubDecl *>(root.get())))
        {
            prog->procs.push_back(std::move(root));
        }
        else
        {
            inMain = true;
            prog->main.push_back(std::move(root));
        }
    }
    return prog;
}

} // namespace il::frontends::basic
