//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements IO and screen-manipulation statement checks for the BASIC semantic
// analyser, covering PRINT/INPUT, channel management, and terminal control
// commands.  The routines live in a dedicated translation unit so the main
// analyzer can remain focused on expression semantics.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Semantic checks for BASIC IO statements (PRINT, INPUT, OPEN, etc.).
/// @details Each routine validates operand types, enforces channel usage
///          invariants, and emits diagnostics through
///          @ref SemanticDiagnostics when contracts are violated.

#include "frontends/basic/SemanticAnalyzer.Stmts.IO.hpp"

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Construct an IO statement context that shares analyzer state.
/// @details Wraps the common @ref StmtShared base so helpers can access
///          diagnostics and loop-tracking facilities without duplicating
///          plumbing code.
IOStmtContext::IOStmtContext(SemanticAnalyzer &analyzer) noexcept : StmtShared(analyzer) {}

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic
{

using semantic_analyzer_detail::IOStmtContext;
using semantic_analyzer_detail::semanticTypeName;

/// @brief Validate the CLS statement. No semantic checks are required.
/// @details CLS performs a screen clear and accepts no operands, so the visitor
///          intentionally performs no validation beyond acknowledging the node.
void SemanticAnalyzer::visit(const ClsStmt &)
{
    // nothing to validate
}

/// @brief Validate the COLOR statement operands.
/// @details Ensures the foreground expression is numeric and, when present, the
///          background expression is also numeric to match BASIC semantics.
///          Violations produce diagnostics via @ref requireNumeric.
void SemanticAnalyzer::visit(const ColorStmt &s)
{
    requireNumeric(*s.fg, "COLOR foreground must be numeric");
    if (s.bg)
        requireNumeric(*s.bg, "COLOR background must be numeric");
}

/// @brief Validate SLEEP statement operand.
/// @details Requires the duration expression to be numeric; narrowing occurs
///          during lowering to a 32-bit integer as expected by the runtime.
void SemanticAnalyzer::visit(const SleepStmt &s)
{
    requireNumeric(*s.ms, "SLEEP duration must be numeric");
}

/// @brief Validate LOCATE statement operands.
/// @details Requires the row expression to be numeric and conditionally checks
///          the column expression when supplied, mirroring the runtime
///          expectations of the LOCATE statement.
void SemanticAnalyzer::visit(const LocateStmt &s)
{
    requireNumeric(*s.row, "LOCATE row must be numeric");
    if (s.col)
        requireNumeric(*s.col, "LOCATE column must be numeric");
}

/// @brief Validate the CURSOR statement. No semantic checks are required.
/// @details CURSOR accepts only ON/OFF keywords which are validated during
///          parsing, so no expression validation is needed here.
void SemanticAnalyzer::visit(const CursorStmt &)
{
    // nothing to validate - ON/OFF is parsed as a boolean flag
}

/// @brief Validate the ALTSCREEN statement. No semantic checks are required.
/// @details ALTSCREEN accepts only ON/OFF keywords which are validated during
///          parsing, so no expression validation is needed here.
void SemanticAnalyzer::analyzeAltScreen(const AltScreenStmt &)
{
    // nothing to validate - ON/OFF is parsed as a boolean flag
}

/// @brief Analyze a PRINT statement for semantic correctness.
/// @details Traverses each printed expression (ignoring pure separators) so any
///          nested semantic issues are diagnosed before code generation.
void SemanticAnalyzer::analyzePrint(const PrintStmt &p)
{
    for (const auto &it : p.items)
        if (it.kind == PrintItem::Kind::Expr && it.expr)
            visitExpr(*it.expr);
}

/// @brief Analyze a PRINT# or WRITE# statement.
/// @details Validates the optional channel expression and each argument payload
///          by visiting them in turn.
void SemanticAnalyzer::analyzePrintCh(const PrintChStmt &p)
{
    if (p.channelExpr)
        visitExpr(*p.channelExpr);
    for (const auto &arg : p.args)
        if (arg)
            visitExpr(*arg);
}

/// @brief Analyze the CLS statement wrapper.
/// @details Delegates to the parameterless visitor for reuse.
void SemanticAnalyzer::analyzeCls(const ClsStmt &stmt)
{
    visit(stmt);
}

/// @brief Analyze the COLOR statement wrapper.
/// @details Simply dispatches to the visitor implementation.
void SemanticAnalyzer::analyzeColor(const ColorStmt &stmt)
{
    visit(stmt);
}

/// @brief Analyze the SLEEP statement wrapper.
/// @details Delegates to the visitor for numeric validation.
void SemanticAnalyzer::analyzeSleep(const SleepStmt &stmt)
{
    visit(stmt);
}

/// @brief Analyze the LOCATE statement wrapper.
/// @details Delegates to the visitor to reuse operand validation logic.
void SemanticAnalyzer::analyzeLocate(const LocateStmt &stmt)
{
    visit(stmt);
}

/// @brief Analyze the CURSOR statement wrapper.
/// @details Delegates to the visitor for consistency with other terminal statements.
void SemanticAnalyzer::analyzeCursor(const CursorStmt &stmt)
{
    visit(stmt);
}

/// @brief Analyze an OPEN statement including type checks and channel tracking.
/// @details Verifies the mode is supported, validates operand types, and records
///          channel mutations so later CLOSE statements can be checked for
///          balance. Emits warnings for channels reopened without closing.
void SemanticAnalyzer::analyzeOpen(OpenStmt &stmt)
{
    const bool modeValid =
        stmt.mode == OpenStmt::Mode::Input || stmt.mode == OpenStmt::Mode::Output ||
        stmt.mode == OpenStmt::Mode::Append || stmt.mode == OpenStmt::Mode::Binary ||
        stmt.mode == OpenStmt::Mode::Random;
    if (!modeValid)
    {
        std::string msg = "invalid OPEN mode";
        de.emit(il::support::Severity::Error, "B4001", stmt.loc, 4, std::move(msg));
    }

    if (stmt.pathExpr)
    {
        Type pathTy = visitExpr(*stmt.pathExpr);
        if (pathTy != Type::Unknown && pathTy != Type::String)
        {
            std::string msg = "OPEN path expression must be STRING, got ";
            msg += semanticTypeName(pathTy);
            msg += '.';
            de.emit(il::support::Severity::Error, "B2001", stmt.pathExpr->loc, 1, std::move(msg));
        }
    }

    if (stmt.channelExpr)
    {
        Type channelTy = visitExpr(*stmt.channelExpr);
        if (channelTy != Type::Unknown && channelTy != Type::Int)
        {
            std::string msg = "OPEN channel expression must be INTEGER, got ";
            msg += semanticTypeName(channelTy);
            msg += '.';
            de.emit(
                il::support::Severity::Error, "B2001", stmt.channelExpr->loc, 1, std::move(msg));
        }
        else if (auto *intExpr = dynamic_cast<IntExpr *>(stmt.channelExpr.get()))
        {
            long long channel = intExpr->value;
            bool wasOpen = openChannels_.count(channel) > 0;
            if (wasOpen)
            {
                std::string msg = "channel #";
                msg += std::to_string(channel);
                msg += " is already open";
                de.emit(il::support::Severity::Warning,
                        "B3002",
                        stmt.channelExpr->loc,
                        1,
                        std::move(msg));
            }
            else
            {
                if (activeProcScope_)
                    activeProcScope_->noteChannelMutation(channel, false);
                openChannels_.insert(channel);
            }
        }
    }
}

/// @brief Analyze a CLOSE statement and update channel bookkeeping.
/// @details Ensures the channel expression is numeric and, when it resolves to a
///          literal channel, records that the descriptor has been closed so the
///          warning state remains accurate.
void SemanticAnalyzer::analyzeClose(CloseStmt &stmt)
{
    if (!stmt.channelExpr)
        return;

    Type channelTy = visitExpr(*stmt.channelExpr);
    if (channelTy != Type::Unknown && channelTy != Type::Int)
    {
        std::string msg = "CLOSE channel expression must be INTEGER, got ";
        msg += semanticTypeName(channelTy);
        msg += '.';
        de.emit(il::support::Severity::Error, "B2001", stmt.channelExpr->loc, 1, std::move(msg));
        return;
    }

    if (auto *intExpr = dynamic_cast<IntExpr *>(stmt.channelExpr.get()))
    {
        long long channel = intExpr->value;
        if (openChannels_.count(channel))
        {
            if (activeProcScope_)
                activeProcScope_->noteChannelMutation(channel, true);
            openChannels_.erase(channel);
        }
    }
}

/// @brief Analyze a SEEK statement for channel and position correctness.
/// @details Visits both operands, emitting diagnostics when non-numeric values
///          are supplied.
void SemanticAnalyzer::analyzeSeek(SeekStmt &stmt)
{
    if (stmt.channelExpr)
    {
        Type channelTy = visitExpr(*stmt.channelExpr);
        if (channelTy != Type::Unknown && channelTy != Type::Int)
        {
            std::string msg = "SEEK channel expression must be INTEGER, got ";
            msg += semanticTypeName(channelTy);
            msg += '.';
            de.emit(
                il::support::Severity::Error, "B2001", stmt.channelExpr->loc, 1, std::move(msg));
        }
    }

    if (stmt.positionExpr)
    {
        Type posTy = visitExpr(*stmt.positionExpr);
        if (posTy != Type::Unknown && posTy != Type::Int)
        {
            std::string msg = "SEEK position expression must be INTEGER, got ";
            msg += semanticTypeName(posTy);
            msg += '.';
            de.emit(
                il::support::Severity::Error, "B2001", stmt.positionExpr->loc, 1, std::move(msg));
        }
    }
}

/// @brief Analyze an INPUT statement targeting variables in the current scope.
/// @details Visits the optional prompt expression and resolves each listed
///          variable, reporting diagnostics when attempting to mutate loop
///          control variables.
void SemanticAnalyzer::analyzeInput(InputStmt &inp)
{
    IOStmtContext ctx(*this);
    if (inp.prompt)
        visitExpr(*inp.prompt);
    for (auto &name : inp.vars)
    {
        if (name.empty())
            continue;
        resolveAndTrackSymbol(name, SymbolKind::InputTarget);
        if (ctx.isLoopVariable(name))
            ctx.reportLoopVariableMutation(name, inp.loc, static_cast<uint32_t>(name.size()));
    }
}

/// @brief Analyze an INPUT# statement targeting a specific channel.
/// @details Resolves the target variable and reports loop-variable mutations in
///          the same fashion as @ref analyzeInput.
void SemanticAnalyzer::analyzeInputCh(InputChStmt &inp)
{
    IOStmtContext ctx(*this);
    auto &name = inp.target.name;
    if (name.empty())
        return;

    resolveAndTrackSymbol(name, SymbolKind::InputTarget);
    if (ctx.isLoopVariable(name))
        ctx.reportLoopVariableMutation(name, inp.loc, static_cast<uint32_t>(name.size()));
}

/// @brief Analyze a LINE INPUT# statement.
/// @details Visits the optional channel expression and destination expression to
///          ensure nested semantics are validated.
void SemanticAnalyzer::analyzeLineInputCh(LineInputChStmt &inp)
{
    if (inp.channelExpr)
        visitExpr(*inp.channelExpr);
    if (inp.targetVar)
        visitExpr(*inp.targetVar);
}

} // namespace il::frontends::basic
