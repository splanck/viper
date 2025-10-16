// File: src/frontends/basic/SemanticAnalyzer.Stmts.IO.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements IO and screen-manipulation statement checks for the BASIC
//          semantic analyzer, covering PRINT/INPUT, channel management, and
//          simple terminal controls.
// Key invariants: Shared helpers report loop-variable mutations consistently;
//                 channel bookkeeping remains balanced across procedure scopes.
// Ownership/Lifetime: Borrowed SemanticAnalyzer state only.
// Links: docs/codemap.md

#include "frontends/basic/SemanticAnalyzer.Stmts.IO.hpp"

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

namespace il::frontends::basic::semantic_analyzer_detail
{

IOStmtContext::IOStmtContext(SemanticAnalyzer &analyzer) noexcept
    : StmtShared(analyzer)
{
}

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic
{

using semantic_analyzer_detail::IOStmtContext;
using semantic_analyzer_detail::semanticTypeName;

void SemanticAnalyzer::visit(const ClsStmt &)
{
    // nothing to validate
}

void SemanticAnalyzer::visit(const ColorStmt &s)
{
    requireNumeric(*s.fg, "COLOR foreground must be numeric");
    if (s.bg)
        requireNumeric(*s.bg, "COLOR background must be numeric");
}

void SemanticAnalyzer::visit(const LocateStmt &s)
{
    requireNumeric(*s.row, "LOCATE row must be numeric");
    if (s.col)
        requireNumeric(*s.col, "LOCATE column must be numeric");
}

void SemanticAnalyzer::analyzePrint(const PrintStmt &p)
{
    for (const auto &it : p.items)
        if (it.kind == PrintItem::Kind::Expr && it.expr)
            visitExpr(*it.expr);
}

void SemanticAnalyzer::analyzePrintCh(const PrintChStmt &p)
{
    if (p.channelExpr)
        visitExpr(*p.channelExpr);
    for (const auto &arg : p.args)
        if (arg)
            visitExpr(*arg);
}

void SemanticAnalyzer::analyzeCls(const ClsStmt &stmt)
{
    visit(stmt);
}

void SemanticAnalyzer::analyzeColor(const ColorStmt &stmt)
{
    visit(stmt);
}

void SemanticAnalyzer::analyzeLocate(const LocateStmt &stmt)
{
    visit(stmt);
}

void SemanticAnalyzer::analyzeOpen(OpenStmt &stmt)
{
    const bool modeValid = stmt.mode == OpenStmt::Mode::Input || stmt.mode == OpenStmt::Mode::Output ||
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
            de.emit(il::support::Severity::Error, "B2001", stmt.channelExpr->loc, 1, std::move(msg));
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
            de.emit(il::support::Severity::Error, "B2001", stmt.channelExpr->loc, 1, std::move(msg));
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
            de.emit(il::support::Severity::Error, "B2001", stmt.positionExpr->loc, 1, std::move(msg));
        }
    }
}

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

void SemanticAnalyzer::analyzeLineInputCh(LineInputChStmt &inp)
{
    if (inp.channelExpr)
        visitExpr(*inp.channelExpr);
    if (inp.targetVar)
        visitExpr(*inp.targetVar);
}

} // namespace il::frontends::basic
