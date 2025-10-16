// File: src/frontends/basic/SemanticAnalyzer.Stmts.Control.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements control-flow statement analysis helpers for the BASIC
//          semantic analyzer, covering branching, loops, and error-handling
//          constructs.
// Key invariants: Loop/label stacks remain balanced via shared guards; control
//                 diagnostics reuse shared helpers for messaging.
// Ownership/Lifetime: Borrowed SemanticAnalyzer state only.
// Links: docs/codemap.md

#include "frontends/basic/SemanticAnalyzer.Stmts.Control.hpp"

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic::semantic_analyzer_detail
{

ControlStmtContext::ControlStmtContext(SemanticAnalyzer &analyzer) noexcept
    : StmtShared(analyzer)
{
}

} // namespace il::frontends::basic::semantic_analyzer_detail

namespace il::frontends::basic
{

using semantic_analyzer_detail::ControlStmtContext;
using semantic_analyzer_detail::conditionExprText;
using semantic_analyzer_detail::semanticTypeName;

void SemanticAnalyzer::checkConditionExpr(Expr &expr)
{
    Type condTy = visitExpr(expr);
    if (condTy == Type::Unknown || condTy == Type::Bool)
        return;

    if (condTy == Type::Int)
    {
        if (auto *intExpr = dynamic_cast<const IntExpr *>(&expr))
        {
            if (intExpr->value == 0 || intExpr->value == 1)
                return;
        }
    }

    std::string exprText = conditionExprText(expr);
    if (exprText.empty())
        exprText = "<expr>";

    de.emitNonBooleanCondition(std::string(DiagNonBooleanCondition),
                               expr.loc,
                               1,
                               semanticTypeName(condTy),
                               exprText);
}

void SemanticAnalyzer::analyzeIf(const IfStmt &i)
{
    if (i.cond)
        checkConditionExpr(*i.cond);
    auto analyzeBranch = [&](const StmtPtr &branch)
    {
        if (!branch)
            return;
        ScopeTracker::ScopedScope scope(scopes_);
        if (const auto *list = dynamic_cast<const StmtList *>(branch.get()))
        {
            for (const auto &child : list->stmts)
                if (child)
                    visitStmt(*child);
        }
        else
        {
            visitStmt(*branch);
        }
    };
    analyzeBranch(i.then_branch);
    for (const auto &e : i.elseifs)
    {
        if (e.cond)
            checkConditionExpr(*e.cond);
        analyzeBranch(e.then_branch);
    }
    analyzeBranch(i.else_branch);
}

struct SemanticAnalyzer::SelectCaseSelectorInfo
{
    bool selectorIsString = false;
    bool selectorIsNumeric = false;
    bool fatal = false;
};

struct SemanticAnalyzer::SelectCaseArmContext
{
    enum class LabelKind
    {
        None,
        Numeric,
        String,
    };

    struct RelInterval
    {
        bool hasLo = false;
        int64_t lo = 0;
        bool hasHi = false;
        int64_t hi = 0;
    };

    SelectCaseArmContext(SemanticDiagnostics &diagnostics,
                         bool selectorIsStringIn,
                         bool selectorIsNumericIn,
                         bool hasElseBody) noexcept
        : de(diagnostics),
          selectorIsString(selectorIsStringIn),
          selectorIsNumeric(selectorIsNumericIn),
          caseElseCount(hasElseBody ? 1 : 0)
    {
    }

    SemanticDiagnostics &de;
    bool selectorIsString = false;
    bool selectorIsNumeric = false;
    int caseElseCount = 0;
    LabelKind seenArmLabelKind = LabelKind::None;
    bool reportedMixedLabelTypes = false;
    std::unordered_set<int32_t> seenLabels;
    std::vector<std::pair<int32_t, int32_t>> seenRanges;
    std::vector<RelInterval> seenRelIntervals;
    std::unordered_set<std::string> seenStringLabels;
};

namespace
{
constexpr int64_t kCaseLabelMin = static_cast<int64_t>(std::numeric_limits<int32_t>::min());
constexpr int64_t kCaseLabelMax = static_cast<int64_t>(std::numeric_limits<int32_t>::max());

bool isCaseElseArm(const CaseArm &arm)
{
    return arm.labels.empty() && arm.ranges.empty() && arm.rels.empty() && arm.str_labels.empty();
}

SemanticAnalyzer::SelectCaseArmContext::RelInterval makeRangeInterval(int32_t lo, int32_t hi)
{
    SemanticAnalyzer::SelectCaseArmContext::RelInterval interval;
    interval.hasLo = true;
    interval.lo = static_cast<int64_t>(lo);
    interval.hasHi = true;
    interval.hi = static_cast<int64_t>(hi);
    return interval;
}

SemanticAnalyzer::SelectCaseArmContext::RelInterval makeRelInterval(CaseArm::CaseRel::Op op, int32_t rhs)
{
    SemanticAnalyzer::SelectCaseArmContext::RelInterval interval;
    switch (op)
    {
        case CaseArm::CaseRel::Op::LT:
            interval.hasHi = true;
            interval.hi = static_cast<int64_t>(rhs) - 1;
            break;
        case CaseArm::CaseRel::Op::LE:
            interval.hasHi = true;
            interval.hi = static_cast<int64_t>(rhs);
            break;
        case CaseArm::CaseRel::Op::EQ:
            interval.hasLo = true;
            interval.lo = static_cast<int64_t>(rhs);
            interval.hasHi = true;
            interval.hi = static_cast<int64_t>(rhs);
            break;
        case CaseArm::CaseRel::Op::GE:
            interval.hasLo = true;
            interval.lo = static_cast<int64_t>(rhs);
            break;
        case CaseArm::CaseRel::Op::GT:
            interval.hasLo = true;
            interval.lo = static_cast<int64_t>(rhs) + 1;
            break;
    }
    return interval;
}

bool intervalsOverlap(const SemanticAnalyzer::SelectCaseArmContext::RelInterval &lhs,
                      const SemanticAnalyzer::SelectCaseArmContext::RelInterval &rhs)
{
    const int64_t lo = std::max(lhs.hasLo ? lhs.lo : std::numeric_limits<int64_t>::min(),
                                rhs.hasLo ? rhs.lo : std::numeric_limits<int64_t>::min());
    const int64_t hi = std::min(lhs.hasHi ? lhs.hi : std::numeric_limits<int64_t>::max(),
                                rhs.hasHi ? rhs.hi : std::numeric_limits<int64_t>::max());
    return lo <= hi;
}

bool intervalContains(const SemanticAnalyzer::SelectCaseArmContext::RelInterval &interval,
                      int32_t value)
{
    if (interval.hasLo && static_cast<int64_t>(value) < interval.lo)
        return false;
    if (interval.hasHi && static_cast<int64_t>(value) > interval.hi)
        return false;
    return true;
}

void noteCaseElse(SemanticAnalyzer::SelectCaseArmContext &ctx, const CaseArm &arm)
{
    ++ctx.caseElseCount;
    if (ctx.caseElseCount > 1)
    {
        std::string msg(diag::ERR_SelectCase_DuplicateElse.text);
        ctx.de.emit(il::support::Severity::Error,
                    std::string(diag::ERR_SelectCase_DuplicateElse.id),
                    arm.range.begin,
                    1,
                    std::move(msg));
    }
}

void reportMixedLabelTypes(SemanticAnalyzer::SelectCaseArmContext &ctx, const CaseArm &arm)
{
    if (ctx.reportedMixedLabelTypes)
        return;
    std::string msg(diag::ERR_SelectCase_MixedLabelTypes.text);
    ctx.de.emit(il::support::Severity::Error,
                std::string(diag::ERR_SelectCase_MixedLabelTypes.id),
                arm.range.begin,
                1,
                std::move(msg));
    ctx.reportedMixedLabelTypes = true;
}

void trackArmLabelKind(SemanticAnalyzer::SelectCaseArmContext &ctx,
                       SemanticAnalyzer::SelectCaseArmContext::LabelKind kind,
                       const CaseArm &arm)
{
    if (kind == SemanticAnalyzer::SelectCaseArmContext::LabelKind::None || ctx.reportedMixedLabelTypes)
        return;
    if (ctx.seenArmLabelKind == SemanticAnalyzer::SelectCaseArmContext::LabelKind::None)
    {
        ctx.seenArmLabelKind = kind;
        return;
    }
    if (ctx.seenArmLabelKind != kind)
        reportMixedLabelTypes(ctx, arm);
}
} // namespace

SemanticAnalyzer::SelectCaseSelectorInfo
SemanticAnalyzer::classifySelectCaseSelector(const SelectCaseStmt &stmt)
{
    SelectCaseSelectorInfo info;
    if (!stmt.selector)
        return info;

    Type selectorType = visitExpr(*stmt.selector);
    if (selectorType == Type::Int)
    {
        markImplicitConversion(*stmt.selector, Type::Int);
        info.selectorIsNumeric = true;
    }
    else if (selectorType == Type::String)
    {
        info.selectorIsString = true;
    }
    else if (selectorType != Type::Unknown)
    {
        std::string msg(diag::ERR_SelectCase_NonIntegerSelector.text);
        de.emit(il::support::Severity::Error,
                std::string(diag::ERR_SelectCase_NonIntegerSelector.id),
                stmt.selector->loc,
                1,
                std::move(msg));
        info.fatal = true;
    }

    return info;
}

bool SemanticAnalyzer::validateSelectCaseStringArm(const CaseArm &arm, SelectCaseArmContext &ctx)
{
    if (ctx.selectorIsNumeric)
    {
        std::string msg(diag::ERR_SelectCase_StringLabelSelector.text);
        ctx.de.emit(il::support::Severity::Error,
                    std::string(diag::ERR_SelectCase_StringLabelSelector.id),
                    arm.range.begin,
                    1,
                    std::move(msg));
    }

    trackArmLabelKind(ctx, SelectCaseArmContext::LabelKind::String, arm);

    for (const auto &label : arm.str_labels)
    {
        if (!ctx.seenStringLabels.insert(label).second)
        {
            std::string msg(diag::ERR_SelectCase_DuplicateLabel.text);
            msg += ": \"";
            msg += label;
            msg += '"';
            ctx.de.emit(il::support::Severity::Error,
                        std::string(diag::ERR_SelectCase_DuplicateLabel.id),
                        arm.range.begin,
                        1,
                        std::move(msg));
        }
    }

    return true;
}

bool SemanticAnalyzer::validateSelectCaseNumericArm(const CaseArm &arm, SelectCaseArmContext &ctx)
{
    if (ctx.selectorIsString)
    {
        std::string msg(diag::ERR_SelectCase_StringSelectorLabels.text);
        ctx.de.emit(il::support::Severity::Error,
                    std::string(diag::ERR_SelectCase_StringSelectorLabels.id),
                    arm.range.begin,
                    1,
                    std::move(msg));
    }

    trackArmLabelKind(ctx, SelectCaseArmContext::LabelKind::Numeric, arm);

    for (const auto &[rawLo, rawHi] : arm.ranges)
    {
        bool valid = true;
        if (rawLo < kCaseLabelMin || rawLo > kCaseLabelMax)
        {
            std::string msg = "CASE range lower bound ";
            msg += std::to_string(rawLo);
            msg += " is outside 32-bit signed range";
            ctx.de.emit(il::support::Severity::Error,
                        std::string(SemanticAnalyzer::DiagSelectCaseLabelRange),
                        arm.range.begin,
                        1,
                        std::move(msg));
            valid = false;
        }
        if (rawHi < kCaseLabelMin || rawHi > kCaseLabelMax)
        {
            std::string msg = "CASE range upper bound ";
            msg += std::to_string(rawHi);
            msg += " is outside 32-bit signed range";
            ctx.de.emit(il::support::Severity::Error,
                        std::string(SemanticAnalyzer::DiagSelectCaseLabelRange),
                        arm.range.begin,
                        1,
                        std::move(msg));
            valid = false;
        }

        if (rawLo > rawHi)
        {
            std::string msg(diag::ERR_SelectCase_InvalidRange.text);
            ctx.de.emit(il::support::Severity::Error,
                        std::string(diag::ERR_SelectCase_InvalidRange.id),
                        arm.range.begin,
                        1,
                        std::move(msg));
            valid = false;
        }

        if (!valid)
            continue;

        const int32_t lo = static_cast<int32_t>(rawLo);
        const int32_t hi = static_cast<int32_t>(rawHi);

        bool overlaps = false;
        for (int32_t label : ctx.seenLabels)
        {
            if (label >= lo && label <= hi)
            {
                std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                ctx.de.emit(il::support::Severity::Error,
                            std::string(diag::ERR_SelectCase_OverlappingRange.id),
                            arm.range.begin,
                            1,
                            std::move(msg));
                overlaps = true;
                break;
            }
        }

        if (!overlaps)
        {
            for (const auto &[seenLo, seenHi] : ctx.seenRanges)
            {
                if (!(hi < seenLo || lo > seenHi))
                {
                    std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                    ctx.de.emit(il::support::Severity::Error,
                                std::string(diag::ERR_SelectCase_OverlappingRange.id),
                                arm.range.begin,
                                1,
                                std::move(msg));
                    overlaps = true;
                    break;
                }
            }
        }

        if (!overlaps)
        {
        SelectCaseArmContext::RelInterval interval = makeRangeInterval(lo, hi);
            for (const auto &seenRel : ctx.seenRelIntervals)
            {
                if (intervalsOverlap(interval, seenRel))
                {
                    std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                    ctx.de.emit(il::support::Severity::Error,
                                std::string(diag::ERR_SelectCase_OverlappingRange.id),
                                arm.range.begin,
                                1,
                                std::move(msg));
                    overlaps = true;
                    break;
                }
            }
        }

        if (overlaps)
            continue;

        ctx.seenRanges.emplace_back(lo, hi);
    }

    for (int64_t rawLabel : arm.labels)
    {
        if (rawLabel < kCaseLabelMin || rawLabel > kCaseLabelMax)
        {
            std::string msg = "CASE label ";
            msg += std::to_string(rawLabel);
            msg += " is outside 32-bit signed range";
            ctx.de.emit(il::support::Severity::Error,
                        std::string(SemanticAnalyzer::DiagSelectCaseLabelRange),
                        arm.range.begin,
                        1,
                        std::move(msg));
            continue;
        }

        const int32_t label = static_cast<int32_t>(rawLabel);

        bool overlapsRange = false;
        for (const auto &[seenLo, seenHi] : ctx.seenRanges)
        {
            if (label >= seenLo && label <= seenHi)
            {
                std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                ctx.de.emit(il::support::Severity::Error,
                            std::string(diag::ERR_SelectCase_OverlappingRange.id),
                            arm.range.begin,
                            1,
                            std::move(msg));
                overlapsRange = true;
                break;
            }
        }

        if (overlapsRange)
            continue;

        bool overlapsRel = false;
        for (const auto &seenRel : ctx.seenRelIntervals)
        {
            if (intervalContains(seenRel, label))
            {
                std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                ctx.de.emit(il::support::Severity::Error,
                            std::string(diag::ERR_SelectCase_OverlappingRange.id),
                            arm.range.begin,
                            1,
                            std::move(msg));
                overlapsRel = true;
                break;
            }
        }

        if (overlapsRel)
            continue;

        if (!ctx.seenLabels.insert(label).second)
        {
            std::string msg(diag::ERR_SelectCase_DuplicateLabel.text);
            msg += ": ";
            msg += std::to_string(rawLabel);
            ctx.de.emit(il::support::Severity::Error,
                        std::string(diag::ERR_SelectCase_DuplicateLabel.id),
                        arm.range.begin,
                        1,
                        std::move(msg));
        }
    }

    for (const auto &rel : arm.rels)
    {
        if (rel.rhs < kCaseLabelMin || rel.rhs > kCaseLabelMax)
        {
            std::string msg = "CASE label ";
            msg += std::to_string(rel.rhs);
            msg += " is outside 32-bit signed range";
            ctx.de.emit(il::support::Severity::Error,
                        std::string(SemanticAnalyzer::DiagSelectCaseLabelRange),
                        arm.range.begin,
                        1,
                        std::move(msg));
            continue;
        }

        const int32_t rhs = static_cast<int32_t>(rel.rhs);
        if (rel.op == CaseArm::CaseRel::Op::EQ)
        {
            bool overlapsRange = false;
            for (const auto &[seenLo, seenHi] : ctx.seenRanges)
            {
                if (rhs >= seenLo && rhs <= seenHi)
                {
                    std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                    ctx.de.emit(il::support::Severity::Error,
                                std::string(diag::ERR_SelectCase_OverlappingRange.id),
                                arm.range.begin,
                                1,
                                std::move(msg));
                    overlapsRange = true;
                    break;
                }
            }

            if (overlapsRange)
                continue;

            bool overlapsRel = false;
            for (const auto &seenRel : ctx.seenRelIntervals)
            {
                if (intervalContains(seenRel, rhs))
                {
                    std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                    ctx.de.emit(il::support::Severity::Error,
                                std::string(diag::ERR_SelectCase_OverlappingRange.id),
                                arm.range.begin,
                                1,
                                std::move(msg));
                    overlapsRel = true;
                    break;
                }
            }

            if (overlapsRel)
                continue;

            if (!ctx.seenLabels.insert(rhs).second)
            {
                std::string msg(diag::ERR_SelectCase_DuplicateLabel.text);
                msg += ": ";
                msg += std::to_string(rel.rhs);
                ctx.de.emit(il::support::Severity::Error,
                            std::string(diag::ERR_SelectCase_DuplicateLabel.id),
                            arm.range.begin,
                            1,
                            std::move(msg));
            }
            continue;
        }

        SelectCaseArmContext::RelInterval interval = makeRelInterval(rel.op, rhs);
        bool overlaps = false;

        for (const auto &[seenLo, seenHi] : ctx.seenRanges)
        {
            if (intervalsOverlap(interval, makeRangeInterval(seenLo, seenHi)))
            {
                std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                ctx.de.emit(il::support::Severity::Error,
                            std::string(diag::ERR_SelectCase_OverlappingRange.id),
                            arm.range.begin,
                            1,
                            std::move(msg));
                overlaps = true;
                break;
            }
        }

        if (!overlaps)
        {
            for (int32_t label : ctx.seenLabels)
            {
                if (intervalContains(interval, label))
                {
                    std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                    ctx.de.emit(il::support::Severity::Error,
                                std::string(diag::ERR_SelectCase_OverlappingRange.id),
                                arm.range.begin,
                                1,
                                std::move(msg));
                    overlaps = true;
                    break;
                }
            }
        }

        if (!overlaps)
        {
            for (const auto &seenRel : ctx.seenRelIntervals)
            {
                if (intervalsOverlap(interval, seenRel))
                {
                    std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                    ctx.de.emit(il::support::Severity::Error,
                                std::string(diag::ERR_SelectCase_OverlappingRange.id),
                                arm.range.begin,
                                1,
                                std::move(msg));
                    overlaps = true;
                    break;
                }
            }

            if (!overlaps)
                ctx.seenRelIntervals.push_back(interval);
        }
    }

    return true;
}

bool SemanticAnalyzer::validateSelectCaseArm(const CaseArm &arm, SelectCaseArmContext &ctx)
{
    if (isCaseElseArm(arm))
    {
        noteCaseElse(ctx, arm);
        return true;
    }

    const bool armHasString = !arm.str_labels.empty();
    const bool armHasNumeric = !arm.labels.empty() || !arm.ranges.empty() || !arm.rels.empty();

    if (armHasString && armHasNumeric)
        reportMixedLabelTypes(ctx, arm);

    bool ok = true;
    if (armHasString)
        ok = validateSelectCaseStringArm(arm, ctx) && ok;
    if (armHasNumeric)
        ok = validateSelectCaseNumericArm(arm, ctx) && ok;

    return ok;
}

void SemanticAnalyzer::analyzeSelectCase(const SelectCaseStmt &stmt)
{
    const SelectCaseSelectorInfo selectorInfo = classifySelectCaseSelector(stmt);
    if (selectorInfo.fatal)
        return;

    SelectCaseArmContext context(de,
                                 selectorInfo.selectorIsString,
                                 selectorInfo.selectorIsNumeric,
                                 !stmt.elseBody.empty());

    for (const auto &arm : stmt.arms)
    {
        if (!validateSelectCaseArm(arm, context))
            return;
        analyzeSelectCaseBody(arm.body);
    }

    if (!stmt.elseBody.empty())
        analyzeSelectCaseBody(stmt.elseBody);
}

void SemanticAnalyzer::analyzeSelectCaseBody(const std::vector<StmtPtr> &body)
{
    ScopeTracker::ScopedScope scope(scopes_);
    for (const auto &child : body)
        if (child)
            visitStmt(*child);
}

void SemanticAnalyzer::analyzeWhile(const WhileStmt &w)
{
    if (w.cond)
        checkConditionExpr(*w.cond);
    ControlStmtContext::LoopGuard loopGuard(*this, LoopKind::While);
    ScopeTracker::ScopedScope scope(scopes_);
    for (const auto &bs : w.body)
        if (bs)
            visitStmt(*bs);
}

void SemanticAnalyzer::analyzeDo(const DoStmt &d)
{
    auto checkCond = [&]() {
        if (d.cond)
            checkConditionExpr(*d.cond);
    };

    if (d.testPos == DoStmt::TestPos::Pre)
        checkCond();

    ControlStmtContext::LoopGuard loopGuard(*this, LoopKind::Do);
    {
        ScopeTracker::ScopedScope scope(scopes_);
        for (const auto &bs : d.body)
            if (bs)
                visitStmt(*bs);
    }

    if (d.testPos == DoStmt::TestPos::Post)
        checkCond();
}

void SemanticAnalyzer::analyzeFor(ForStmt &f)
{
    resolveAndTrackSymbol(f.var, SymbolKind::Definition);
    if (f.start)
        visitExpr(*f.start);
    if (f.end)
        visitExpr(*f.end);
    if (f.step)
        visitExpr(*f.step);
    ControlStmtContext::ForLoopGuard forGuard(*this, f.var);
    ControlStmtContext::LoopGuard loopGuard(*this, LoopKind::For);
    ScopeTracker::ScopedScope scope(scopes_);
    for (const auto &bs : f.body)
        if (bs)
            visitStmt(*bs);
}

void SemanticAnalyzer::analyzeGoto(const GotoStmt &g)
{
    auto insertResult = labelRefs_.insert(g.target);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteLabelRefInserted(g.target);
    if (!labels_.count(g.target))
    {
        std::string msg = "unknown line " + std::to_string(g.target);
        de.emit(il::support::Severity::Error, "B1003", g.loc, 4, std::move(msg));
    }
}

void SemanticAnalyzer::analyzeGosub(const GosubStmt &stmt)
{
    auto insertResult = labelRefs_.insert(stmt.targetLine);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteLabelRefInserted(stmt.targetLine);
    if (!labels_.count(stmt.targetLine))
    {
        std::string msg = "unknown line " + std::to_string(stmt.targetLine);
        de.emit(il::support::Severity::Error, "B1003", stmt.loc, 5, std::move(msg));
    }
}

void SemanticAnalyzer::analyzeOnErrorGoto(const OnErrorGoto &stmt)
{
    if (stmt.toZero)
    {
        clearErrorHandler();
        return;
    }
    auto insertResult = labelRefs_.insert(stmt.target);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteLabelRefInserted(stmt.target);
    if (!labels_.count(stmt.target))
    {
        std::string msg = "unknown line " + std::to_string(stmt.target);
        de.emit(il::support::Severity::Error, "B1003", stmt.loc, 4, std::move(msg));
    }
    installErrorHandler(stmt.target);
}

void SemanticAnalyzer::analyzeNext(const NextStmt &n)
{
    if (forStack_.empty() || (!n.var.empty() && n.var != forStack_.back()))
    {
        std::string msg = "mismatched NEXT";
        if (!n.var.empty())
            msg += " '" + n.var + "'";
        if (!forStack_.empty())
            msg += ", expected '" + forStack_.back() + "'";
        else
            msg += ", no active FOR";
        de.emit(il::support::Severity::Error, "B1002", n.loc, 4, std::move(msg));
    }
    else
    {
        popForVariable();
    }
}

void SemanticAnalyzer::analyzeExit(const ExitStmt &stmt)
{
    const auto toLoopKind = [](ExitStmt::LoopKind kind) {
        switch (kind)
        {
            case ExitStmt::LoopKind::For:
                return LoopKind::For;
            case ExitStmt::LoopKind::While:
                return LoopKind::While;
            case ExitStmt::LoopKind::Do:
                return LoopKind::Do;
        }
        return LoopKind::While;
    };
    const auto loopKindName = [](LoopKind kind) {
        switch (kind)
        {
            case LoopKind::For:
                return "FOR";
            case LoopKind::While:
                return "WHILE";
            case LoopKind::Do:
                return "DO";
        }
        return "WHILE";
    };

    const auto targetLoop = toLoopKind(stmt.kind);
    const char *targetName = loopKindName(targetLoop);

    if (loopStack_.empty())
    {
        std::string msg = "EXIT ";
        msg += targetName;
        msg += " used outside of any loop";
        de.emit(il::support::Severity::Error, "B1011", stmt.loc, 4, std::move(msg));
        return;
    }

    const auto activeLoop = loopStack_.back();
    if (activeLoop != targetLoop)
    {
        std::string msg = "EXIT ";
        msg += targetName;
        msg += " does not match innermost loop (";
        msg += loopKindName(activeLoop);
        msg += ')';
        de.emit(il::support::Severity::Error, "B1011", stmt.loc, 4, std::move(msg));
    }
}

void SemanticAnalyzer::analyzeEnd(const EndStmt &)
{
    // nothing
}

void SemanticAnalyzer::analyzeResume(const Resume &stmt)
{
    if (!hasActiveErrorHandler())
    {
        std::string msg = "RESUME requires an active error handler";
        de.emit(il::support::Severity::Error,
                "B1012",
                stmt.loc,
                6,
                std::move(msg));
    }
    if (stmt.mode != Resume::Mode::Label)
        return;
    auto insertResult = labelRefs_.insert(stmt.target);
    if (insertResult.second && activeProcScope_)
        activeProcScope_->noteLabelRefInserted(stmt.target);
    if (!labels_.count(stmt.target))
    {
        std::string msg = "unknown line " + std::to_string(stmt.target);
        de.emit(il::support::Severity::Error, "B1003", stmt.loc, 4, std::move(msg));
    }
}

void SemanticAnalyzer::analyzeReturn(ReturnStmt &stmt)
{
    if (!activeProcScope_)
    {
        if (stmt.value)
        {
            std::string msg = "RETURN with value not allowed at top level";
            de.emit(il::support::Severity::Error,
                    "B1008",
                    stmt.loc,
                    6,
                    std::move(msg));
        }
        else
        {
            stmt.isGosubReturn = true;
        }
    }
    if (hasActiveErrorHandler())
        clearErrorHandler();
}

void SemanticAnalyzer::installErrorHandler(int label)
{
    errorHandlerActive_ = true;
    errorHandlerTarget_ = label;
}

void SemanticAnalyzer::clearErrorHandler()
{
    errorHandlerActive_ = false;
    errorHandlerTarget_.reset();
}

bool SemanticAnalyzer::hasActiveErrorHandler() const noexcept
{
    return errorHandlerActive_;
}

} // namespace il::frontends::basic
