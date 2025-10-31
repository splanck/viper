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
/// @brief Implements the statement sequencing helper used by the BASIC parser.
/// @details The sequencer coordinates colon/newline separators, optional line
///          numbers, and termination checks so grammar productions can focus on
///          individual statement shapes.

namespace il::frontends::basic
{
/// @brief Construct a sequencer bound to a parser instance.
/// @details Stores a reference to the owning parser so token queries and
///          statement parsing can be delegated while the sequencer tracks
///          separators and deferred line numbers.
/// @param parser Parser instance that supplies tokens and parsing callbacks.
StatementSequencer::StatementSequencer(Parser &parser) : parser_(parser) {}

/// @brief Consume any leading statement separators before parsing begins.
/// @details Consumes colon or end-of-line tokens at the head of the stream and
///          records which separator was encountered.  Callers can then inspect
///          @ref lastSeparator to differentiate between colon-separated and
///          newline-separated statements.
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
/// @details Used when parsing constructs that can span blank lines.  Updates
///          the cached separator so subsequent logic knows a newline separated
///          the surrounding statements.
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
/// @details Mirrors @ref skipLeadingSeparator but is invoked after a statement
///          has already been parsed.  The cached separator guides line
///          collection during the next iteration.
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
/// @details BASIC permits optional numeric or identifier labels.  The helper
///          first consumes any pending label from a previous iteration, then
///          inspects the token stream for identifier or numeric labels.  When a
///          tokenised label appears immediately followed by another numeric
///          token, @ref deferredLineOnly_ is set so callers can defer parsing
///          until additional statements arrive.
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
/// @details Used when a colon-separated block ends before consuming a trailing
///          line label.  The label is cached so @ref withOptionalLineNumber can
///          surface it before reading further tokens.
/// @param line Parsed line number to replay later.
/// @param loc Source location associated with the numeric token.
void StatementSequencer::stashPendingLine(int line, il::support::SourceLoc loc)
{
    pendingLine_ = line;
    pendingLineLoc_ = loc;
}

/// @brief Retrieve the most recent separator kind consumed by the sequencer.
/// @details Allows layout-sensitive consumers to distinguish between newline
///          and colon separation when interpreting the parsed statement list.
/// @return Enum describing the last observed separator.
StatementSequencer::SeparatorKind StatementSequencer::lastSeparator() const
{
    return lastSeparator_;
}

/// @brief Decide how the sequencer should handle the current line label.
/// @details Applies the provided terminator predicate and deferred-line rules to
///          determine whether parsing should continue, terminate, or defer until
///          more tokens arrive.  When terminating, the supplied consumer is
///          invoked so callers can record metadata or consume tokens.
/// @param line Current line number being considered.
/// @param lineLoc Source location associated with @p line.
/// @param isTerminator Predicate that signals whether collection should stop.
/// @param onTerminator Callback executed when @p isTerminator succeeds.
/// @param state Mutable sequencing state shared across iterations.
/// @return Enum describing the action to take.
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
/// @details Alternates between consuming separators, parsing statements via the
///          parser, and consulting @p isTerminator to decide whether parsing
///          should stop.  When the predicate succeeds the @p onTerminator
///          callback is run to perform clean-up before returning the collected
///          terminator metadata.
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
        if (lookaheadKind != TokenKind::EndOfLine && lookaheadKind != TokenKind::EndOfFile &&
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
/// @details Wraps @ref collectStatements with a predicate that checks for
///          @p terminator and consumes it when encountered.
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
/// @details Repeatedly collects statements until a colon- or newline-driven
///          terminator fires, preserving labels for subsequent lines.  Normalises
///          the resulting AST so the emitted node carries the correct line
///          metadata even when the source line contained only a label.
/// @return Statement node representing the parsed line.  Empty lines yield a
///         placeholder list or label to keep numbering consistent.
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
