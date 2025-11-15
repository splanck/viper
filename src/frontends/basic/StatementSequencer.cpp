//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the helper that coordinates statement sequencing in the BASIC
// parser. The sequencer handles separator trivia, optional line numbers, and
// terminator detection so the grammar productions can focus on the shape of
// individual statements.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/StatementSequencer.hpp"
#include "frontends/basic/AST.hpp"
#include "frontends/basic/LineUtils.hpp"

#include "frontends/basic/Parser.hpp"
#include <cstdlib>

/// @file
/// @brief Statement sequencing helper shared by BASIC parser productions.
/// @details Normalises the handling of colon/newline separators and optional
///          line labels so grammar productions can focus solely on recognising
///          statement shapes.  The sequencer also exposes utilities for parsing
///          lists of statements that terminate on custom predicates.

namespace il::frontends::basic
{
/// @brief Construct a sequencer bound to a parser instance.
/// @details Stores a reference to the owning parser so token queries and
///          sub-parses can be delegated while this helper focuses on separator
///          bookkeeping.  Pending line label state is initialised to an empty
///          sentinel.
///
/// @param parser Parser instance that supplies tokens and parsing callbacks.
StatementSequencer::StatementSequencer(Parser &parser) : parser_(parser) {}

/// @brief Consume any leading statement separators before parsing begins.
/// @details Eats an arbitrary number of colon or end-of-line tokens and updates
///          the cached separator kind to reflect the most recent token
///          consumed.  Callers can then determine whether the next statement was
///          preceded by a newline or a colon separator.
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

/// @brief Consume consecutive end-of-line tokens.
/// @details Used when parsing constructs that should coalesce blank lines.  The
///          routine records that the last separator encountered was a line break
///          whenever any tokens are consumed.
///
/// @return True when at least one end-of-line token was removed.
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

/// @brief Consume colon or newline separators following a statement.
/// @details Mirrors @ref skipLeadingSeparator but is intended for use once a
///          statement has already been parsed.  The cached separator kind is
///          updated so later logic knows how the last pair of statements were
///          separated.
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

/// @brief Execute a callback with the current line-number context.
/// @details BASIC permits optional numeric labels ahead of statements.  This
///          helper checks for such a label—either pending from a previous
///          iteration or present in the token stream—and passes the line number
///          and its location to @p fn.  If a label is read directly from tokens
///          and a subsequent numeric token appears without intervening code, the
///          sequencer marks @ref deferredLineOnly_ so the caller can defer
///          statement parsing until more input arrives.
///
/// @param fn Callback invoked with the discovered line number (or zero when
///           absent) and its source location.
void StatementSequencer::withOptionalLineNumber(
    const std::function<void(int, il::support::SourceLoc)> &fn, bool allowIdentifierLabel)
{
    int line = 0;
    il::support::SourceLoc loc{};
    deferredLineOnly_ = false;
    if (pendingLine_ >= 0)
    {
        line = pendingLine_;
        loc = pendingLineLoc_;
        pendingLine_ = -1;
        pendingLineLoc_ = {};

        if (parser_.at(TokenKind::Number))
            deferredLineOnly_ = true;
    }
    else if (allowIdentifierLabel && parser_.at(TokenKind::Identifier) &&
             parser_.peek(1).kind == TokenKind::Colon)
    {
        const Token &tok = parser_.peek();
        int labelNumber = parser_.ensureLabelNumber(tok.lexeme);
        parser_.noteNamedLabelDefinition(tok, labelNumber);
        parser_.consume();
        parser_.consume();
        line = labelNumber;
        loc = tok.loc;
    }
    else if (parser_.at(TokenKind::Number))
    {
        const Token &tok = parser_.peek();
        line = std::atoi(tok.lexeme.c_str());
        loc = tok.loc;
        parser_.noteNumericLabelUsage(line);
        parser_.consume();
    }
    fn(line, loc);
}

/// @brief Record a line number token for consumption by the next iteration.
/// @details When parsing falls through due to colon-separated content the
///          following line number needs to be preserved.  This method caches the
///          value so @ref withOptionalLineNumber can present it before reading
///          additional tokens.
///
/// @param line Parsed line number to replay later.
/// @param loc Source location associated with the numeric token.
void StatementSequencer::stashPendingLine(int line, il::support::SourceLoc loc)
{
    pendingLine_ = line;
    pendingLineLoc_ = loc;
}

/// @brief Retrieve the most recent separator kind consumed by the sequencer.
/// @details Helps callers distinguish between statements separated by newlines,
///          colons, or nothing when reasoning about layout-sensitive constructs
///          such as multi-line IF statements.
///
/// @return Enum describing the last observed separator.
StatementSequencer::SeparatorKind StatementSequencer::lastSeparator() const
{
    return lastSeparator_;
}

/// @brief Decide how statement collection should proceed for the current line.
/// @details Evaluates the supplied @p isTerminator predicate and deferred-line
///          state to determine whether parsing should continue, terminate, or
///          pause until more input arrives.  When terminating, the
///          @p onTerminator callback is invoked so callers can consume any
///          terminator tokens before returning.
///
/// @param line Numeric label associated with the pending statement (0 when absent).
/// @param lineLoc Source location of the label token, if any.
/// @param isTerminator Predicate consulted to decide whether to stop parsing.
/// @param onTerminator Callback executed when the terminator predicate succeeds.
/// @param state Mutable bookkeeping record that captures separator and deferral information.
/// @return Action describing whether to continue, terminate, or defer parsing.
StatementSequencer::LineAction StatementSequencer::evaluateLineAction(
    int line,
    il::support::SourceLoc lineLoc,
    const TerminatorPredicate &isTerminator,
    const TerminatorConsumer &onTerminator,
    CollectionState &state)
{
    if (isTerminator(line, lineLoc))
    {
        state.info.line = line;
        state.info.loc = parser_.peek().loc;
        onTerminator(line, lineLoc, state.info);
        return LineAction::Terminate;
    }

    if (deferredLineOnly_)
    {
        if (parser_.at(TokenKind::Number))
        {
            const Token &next = parser_.peek();
            int nextLine = std::atoi(next.lexeme.c_str());
            stashPendingLine(nextLine, next.loc);
            parser_.consume();
            state.hadPendingLine = true;
        }
        return LineAction::Defer;
    }

    return LineAction::Continue;
}

/// @brief Collect consecutive statements until a terminator predicate fires.
/// @details Repeatedly parses statements, forwarding them into @p dst while
///          consulting @p isTerminator before each iteration to decide whether
///          parsing should stop.  When the predicate succeeds the supplied
///          @p onTerminator callback is run to perform any clean-up or token
///          consumption, and the gathered terminator metadata is returned to the
///          caller.
///
/// @param isTerminator Predicate that inspects the current line context and
///        decides whether collection should stop.
/// @param onTerminator Callback invoked when @p isTerminator succeeds. It can
///        consume tokens or populate the returned info structure.
/// @param dst Destination vector that receives parsed statements.
/// @return Information describing the terminator that halted collection.
StatementSequencer::TerminatorInfo StatementSequencer::collectStatements(
    const TerminatorPredicate &isTerminator,
    const TerminatorConsumer &onTerminator,
    std::vector<StmtPtr> &dst)
{
    CollectionState state;
    skipLeadingSeparator();
    while (!parser_.at(TokenKind::EndOfFile))
    {
        skipLineBreaks();
        if (parser_.at(TokenKind::EndOfFile))
            break;

        state.separatorBefore = lastSeparator_;
        state.hadPendingLine = (pendingLine_ >= 0);

        int line = 0;
        il::support::SourceLoc lineLoc{};
        bool allowIdentifierLabel =
            (state.separatorBefore != SeparatorKind::Colon) && !state.hadPendingLine;
        withOptionalLineNumber(
            [&line, &lineLoc](int currentLine, il::support::SourceLoc currentLoc)
            {
                line = currentLine;
                lineLoc = currentLoc;
            },
            allowIdentifierLabel);

        LineAction action = evaluateLineAction(line, lineLoc, isTerminator, onTerminator, state);
        if (action == LineAction::Terminate || action == LineAction::Defer)
            break;

        TokenKind lookaheadKind = parser_.peek().kind;
        if (lookaheadKind == TokenKind::KeywordAddfile)
        {
            // Handle ADDFILE directive by splicing included content into dst.
            parser_.handleAddFileInto(dst);
        }
        else if (lookaheadKind != TokenKind::EndOfLine && lookaheadKind != TokenKind::EndOfFile &&
                 lookaheadKind != TokenKind::Colon)
        {
            auto stmt = parser_.parseStatement(line);
            if (stmt)
            {
                stmt->line = line;
                dst.push_back(std::move(stmt));
            }
        }

        skipStatementSeparator();
    }
    return state.info;
}

/// @brief Convenience overload collecting until a specific token appears.
///
/// Wraps the general `collectStatements` logic with a predicate that watches for
/// @p terminator in the token stream and consumes it when encountered.
///
/// @param terminator Token kind to treat as the terminator.
/// @param dst Destination vector for parsed statements.
/// @return Terminator metadata populated by the underlying collection routine.
StatementSequencer::TerminatorInfo StatementSequencer::collectStatements(TokenKind terminator,
                                                                         std::vector<StmtPtr> &dst)
{
    auto predicate = [&](int, il::support::SourceLoc) { return parser_.at(terminator); };
    auto consumer = [&](int, il::support::SourceLoc, TerminatorInfo &) { parser_.consume(); };
    return collectStatements(predicate, consumer, dst);
}

/// @brief Parse a full BASIC line, including optional label and statements.
///
/// The routine gathers all statements separated by colons until a line break or
/// terminator is observed. It preserves pending labels for subsequent lines and
/// normalises the resulting AST so the first statement's line metadata reflects
/// the parsed label when present.
///
/// @return A statement node representing the parsed line. For empty lines a
///         placeholder list or label is emitted to keep numbering consistent.
StmtPtr StatementSequencer::parseStatementLine()
{
    std::vector<StmtPtr> stmts;
    int lineNumber = 0;
    bool haveLine = false;
    il::support::SourceLoc lineLoc{};
    auto predicate = [&](int line, il::support::SourceLoc loc)
    {
        if (!haveLine)
        {
            haveLine = true;
            lineNumber = line;
            lineLoc = loc;
            return false;
        }

        if (lastSeparator() == SeparatorKind::LineBreak)
        {
            if (hasUserLine(line))
                stashPendingLine(line, loc);
            return true;
        }

        if (lastSeparator() != SeparatorKind::Colon)
        {
            if (hasUserLine(line))
                stashPendingLine(line, loc);
            return true;
        }

        if (hasUserLine(line) && line != lineNumber)
        {
            stashPendingLine(line, loc);
            return true;
        }
        return false;
    };
    auto consumer = [&](int line, il::support::SourceLoc loc, TerminatorInfo &)
    {
        if (hasUserLine(line))
            stashPendingLine(line, loc);
    };

    collectStatements(predicate, consumer, stmts);

    il::support::SourceLoc stmtLineLoc = lineLoc;
    if (!stmtLineLoc.isValid() || !stmtLineLoc.hasLine())
    {
        stmtLineLoc = parser_.peek().loc;
    }

    if (stmts.empty() && haveLine && hasUserLine(lineNumber))
    {
        auto label = std::make_unique<LabelStmt>();
        label->line = lineNumber;
        label->loc = stmtLineLoc;

        if (pendingLine_ == lineNumber)
        {
            pendingLine_ = -1;
            pendingLineLoc_ = {};
        }

        return label;
    }

    if (stmts.empty())
    {
        auto list = std::make_unique<StmtList>();
        list->line = lineNumber;
        list->loc = stmtLineLoc;
        list->stmts = std::move(stmts);
        return list;
    }

    if (!haveLine && !stmts.empty())
        lineNumber = stmts.front()->line;

    if (!isUnlabeledLine(lineNumber))
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

    il::support::SourceLoc firstLoc{};
    for (const auto &stmt : stmts)
    {
        if (stmt && stmt->loc.hasFile() && stmt->loc.hasLine())
        {
            firstLoc = stmt->loc;
            break;
        }
    }
    if (!firstLoc.hasLine())
    {
        firstLoc = stmtLineLoc;
    }

    list->loc = firstLoc;
    list->stmts = std::move(stmts);
    return list;
}

} // namespace il::frontends::basic
