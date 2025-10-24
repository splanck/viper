// File: src/frontends/basic/StatementSequencer.hpp
// Purpose: Declares helper coordinating BASIC statement sequencing semantics.
// Key invariants: Maintains separator classification and pending line labels.
// Ownership/Lifetime: Helper borrows Parser token stream; no ownership transfer.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/Token.hpp"
#include "frontends/basic/ast/NodeFwd.hpp"
#include "support/source_location.hpp"
#include <functional>
#include <vector>

namespace il::frontends::basic
{
class Parser;

/// @brief Helper that manages separators, labels, and statement iteration.
class StatementSequencer
{
  public:
    /// @brief Aggregated information about a terminating keyword.
    struct TerminatorInfo
    {
        int line = 0;                 ///< Optional line number preceding terminator.
        il::support::SourceLoc loc{}; ///< Location where terminator keyword appeared.
    };

    /// @brief Classification of the most recently consumed separator.
    enum class SeparatorKind
    {
        None,      ///< No separator observed since last statement.
        Colon,     ///< Colon separated adjacent statements.
        LineBreak, ///< Line break separated adjacent statements.
    };

    using TerminatorPredicate =
        std::function<bool(int, il::support::SourceLoc)>; ///< Identifies terminator tokens.
    using TerminatorConsumer = std::function<void(int, il::support::SourceLoc, TerminatorInfo &)>;
    ///< Consumes terminator tokens.

    /// @brief Construct a sequencer bound to a parser instance.
    explicit StatementSequencer(Parser &parser);

    /// @brief Consume a single leading colon or newline if present.
    void skipLeadingSeparator();

    /// @brief Consume consecutive newline tokens.
    /// @return True when at least one newline token was consumed.
    bool skipLineBreaks();

    /// @brief Consume either a colon or newline separator when present.
    void skipStatementSeparator();

    /// @brief Invoke @p fn with an optional numeric line label.
    void withOptionalLineNumber(const std::function<void(int, il::support::SourceLoc)> &fn);

    /// @brief Remember a pending line label for the next statement.
    void stashPendingLine(int line, il::support::SourceLoc loc);

    /// @brief Inspect the last consumed separator classification.
    SeparatorKind lastSeparator() const;

    /// @brief Populate @p dst with statements until @p isTerminator fires.
    TerminatorInfo collectStatements(const TerminatorPredicate &isTerminator,
                                     const TerminatorConsumer &onTerminator,
                                     std::vector<StmtPtr> &dst);

    /// @brief Populate @p dst until the specified terminator token is encountered.
    TerminatorInfo collectStatements(TokenKind terminator, std::vector<StmtPtr> &dst);

    /// @brief Parse and coalesce statements that share a logical source line.
    /// @return Either a single statement or a StmtList representing the line.
    StmtPtr parseStatementLine();

  private:
    /// @brief Status returned when deciding how to handle the current line context.
    enum class LineAction
    {
        Continue,  ///< Proceed with parsing the statement body.
        Defer,     ///< Defer parsing until additional input arrives.
        Terminate, ///< Stop collection because a terminator was encountered.
    };

    /// @brief Local state threaded through statement collection iterations.
    struct CollectionState
    {
        SeparatorKind separatorBefore =
            SeparatorKind::None;     ///< Separator preceding the current line.
        bool hadPendingLine = false; ///< True when a pending line label exists before processing.
        TerminatorInfo info;         ///< Populated once a terminator is observed.
    };

    LineAction evaluateLineAction(int line,
                                  il::support::SourceLoc lineLoc,
                                  const TerminatorPredicate &isTerminator,
                                  const TerminatorConsumer &onTerminator,
                                  CollectionState &state);

    Parser &parser_;                          ///< Underlying parser providing token access.
    int pendingLine_ = -1;                    ///< Deferred numeric line label for next statement.
    il::support::SourceLoc pendingLineLoc_{}; ///< Location of the deferred line label.
    bool deferredLineOnly_ =
        false; ///< True when pending line should emit label before reading new tokens.
    SeparatorKind lastSeparator_ =
        SeparatorKind::None; ///< Treat start-of-file as neutral separator state.
};

} // namespace il::frontends::basic
