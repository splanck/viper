// File: src/frontends/basic/StatementSequencer.cpp
// Purpose: Implements BASIC statement sequencing helper shared by parser routines.
// Key invariants: Delegates token access to Parser while owning separator state.
// Ownership/Lifetime: Operates on borrowed Parser; no additional resources allocated.
// License: MIT (see LICENSE).
// Links: docs/codemap.md

#include "frontends/basic/StatementSequencer.hpp"

#include "frontends/basic/Parser.hpp"
#include <cstdlib>

namespace il::frontends::basic
{
StatementSequencer::StatementSequencer(Parser &parser) : parser_(parser) {}

void StatementSequencer::skipLeadingSeparator()
{
    bool consumedColon = false;
    bool consumedLineBreak = false;

    while (parser_.at(TokenKind::Colon) || parser_.at(TokenKind::EndOfLine))
    {
        if (parser_.at(TokenKind::Colon))
        {
            parser_.consume();
            consumedColon = true;
        }
        else
        {
            parser_.consume();
            consumedLineBreak = true;
        }
    }

    if (consumedLineBreak)
    {
        lastSeparator_ = SeparatorKind::LineBreak;
    }
    else if (consumedColon)
    {
        lastSeparator_ = SeparatorKind::Colon;
    }
    else
    {
        lastSeparator_ = SeparatorKind::None;
    }
}

bool StatementSequencer::skipLineBreaks()
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

void StatementSequencer::skipStatementSeparator()
{
    bool consumedColon = false;
    bool consumedLineBreak = false;

    while (parser_.at(TokenKind::Colon) || parser_.at(TokenKind::EndOfLine))
    {
        if (parser_.at(TokenKind::Colon))
        {
            parser_.consume();
            consumedColon = true;
        }
        else
        {
            parser_.consume();
            consumedLineBreak = true;
        }
    }

    if (consumedLineBreak)
    {
        lastSeparator_ = SeparatorKind::LineBreak;
    }
    else if (consumedColon)
    {
        lastSeparator_ = SeparatorKind::Colon;
    }
    else
    {
        lastSeparator_ = SeparatorKind::None;
    }
}

void StatementSequencer::withOptionalLineNumber(const std::function<void(int)> &fn)
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

void StatementSequencer::stashPendingLine(int line)
{
    pendingLine_ = line;
}

StatementSequencer::SeparatorKind StatementSequencer::lastSeparator() const
{
    return lastSeparator_;
}

StatementSequencer::TerminatorInfo StatementSequencer::collectStatements(
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
                if (stmt)
                {
                    stmt->line = line;
                    dst.push_back(std::move(stmt));
                }
            });
        if (done)
            break;
        skipStatementSeparator();
    }
    return info;
}

StatementSequencer::TerminatorInfo StatementSequencer::collectStatements(
    TokenKind terminator, std::vector<StmtPtr> &dst)
{
    auto predicate = [&](int) { return parser_.at(terminator); };
    auto consumer = [&](int, TerminatorInfo &) { parser_.consume(); };
    return collectStatements(predicate, consumer, dst);
}

StmtPtr StatementSequencer::parseStatementLine()
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

        if (lastSeparator() == SeparatorKind::LineBreak)
        {
            if (line > 0)
                stashPendingLine(line);
            return true;
        }

        if (lastSeparator() != SeparatorKind::Colon)
        {
            if (line > 0)
                stashPendingLine(line);
            return true;
        }

        if (line > 0 && line != lineNumber)
        {
            stashPendingLine(line);
            return true;
        }
        return false;
    };
    auto consumer = [&](int line, TerminatorInfo &)
    {
        if (line > 0)
            stashPendingLine(line);
    };

    collectStatements(predicate, consumer, stmts);

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

} // namespace il::frontends::basic
