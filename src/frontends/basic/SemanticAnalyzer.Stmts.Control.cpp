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

void SemanticAnalyzer::analyzeSelectCase(const SelectCaseStmt &stmt)
{
    constexpr int64_t kCaseLabelMin = static_cast<int64_t>(std::numeric_limits<int32_t>::min());
    constexpr int64_t kCaseLabelMax = static_cast<int64_t>(std::numeric_limits<int32_t>::max());

    struct RelInterval
    {
        bool hasLo = false;
        int64_t lo = 0;
        bool hasHi = false;
        int64_t hi = 0;
    };

    auto makeRangeInterval = [](int32_t lo, int32_t hi) {
        RelInterval interval;
        interval.hasLo = true;
        interval.lo = static_cast<int64_t>(lo);
        interval.hasHi = true;
        interval.hi = static_cast<int64_t>(hi);
        return interval;
    };

    auto makeRelInterval = [](CaseArm::CaseRel::Op op, int32_t rhs) {
        RelInterval interval;
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
    };

    auto intervalsOverlap = [](const RelInterval &lhs, const RelInterval &rhs) {
        const int64_t lo = std::max(lhs.hasLo ? lhs.lo : std::numeric_limits<int64_t>::min(),
                                    rhs.hasLo ? rhs.lo : std::numeric_limits<int64_t>::min());
        const int64_t hi = std::min(lhs.hasHi ? lhs.hi : std::numeric_limits<int64_t>::max(),
                                    rhs.hasHi ? rhs.hi : std::numeric_limits<int64_t>::max());
        return lo <= hi;
    };

    auto intervalContains = [](const RelInterval &interval, int32_t value) {
        if (interval.hasLo && static_cast<int64_t>(value) < interval.lo)
            return false;
        if (interval.hasHi && static_cast<int64_t>(value) > interval.hi)
            return false;
        return true;
    };

    Type selectorType = Type::Unknown;
    bool selectorIsString = false;
    bool selectorIsNumeric = false;
    if (stmt.selector)
    {
        selectorType = visitExpr(*stmt.selector);
        if (selectorType == Type::Int)
        {
            markImplicitConversion(*stmt.selector, Type::Int);
            selectorIsNumeric = true;
        }
        else if (selectorType == Type::String)
        {
            selectorIsString = true;
        }
        else if (selectorType != Type::Unknown)
        {
            std::string msg(diag::ERR_SelectCase_NonIntegerSelector.text);
            de.emit(il::support::Severity::Error,
                    std::string(diag::ERR_SelectCase_NonIntegerSelector.id),
                    stmt.selector->loc,
                    1,
                    std::move(msg));
        }
    }

    auto analyzeBody = [&](const std::vector<StmtPtr> &body) {
        ScopeTracker::ScopedScope scope(scopes_);
        for (const auto &child : body)
            if (child)
                visitStmt(*child);
    };

    std::unordered_set<int32_t> seenLabels;
    std::vector<std::pair<int32_t, int32_t>> seenRanges;
    std::vector<RelInterval> seenRelIntervals;
    std::unordered_set<std::string> seenStringLabels;
    enum class CaseArmLabelKind
    {
        None,
        Numeric,
        String,
    };
    CaseArmLabelKind seenArmLabelKind = CaseArmLabelKind::None;
    bool reportedMixedLabelTypes = false;
    int caseElseCount = stmt.elseBody.empty() ? 0 : 1;

    auto reportMixedLabelTypes = [&](const CaseArm &arm) {
        if (reportedMixedLabelTypes)
            return;
        std::string msg(diag::ERR_SelectCase_MixedLabelTypes.text);
        de.emit(il::support::Severity::Error,
                std::string(diag::ERR_SelectCase_MixedLabelTypes.id),
                arm.range.begin,
                1,
                std::move(msg));
        reportedMixedLabelTypes = true;
    };

    auto trackArmLabelKind = [&](CaseArmLabelKind kind, const CaseArm &arm) {
        if (kind == CaseArmLabelKind::None || reportedMixedLabelTypes)
            return;
        if (seenArmLabelKind == CaseArmLabelKind::None)
        {
            seenArmLabelKind = kind;
            return;
        }
        if (seenArmLabelKind != kind)
            reportMixedLabelTypes(arm);
    };

    for (const auto &arm : stmt.arms)
    {
        if (arm.labels.empty() && arm.ranges.empty() && arm.rels.empty() && arm.str_labels.empty())
        {
            ++caseElseCount;
            if (caseElseCount > 1)
            {
                std::string msg(diag::ERR_SelectCase_DuplicateElse.text);
                de.emit(il::support::Severity::Error,
                        std::string(diag::ERR_SelectCase_DuplicateElse.id),
                        arm.range.begin,
                        1,
                        std::move(msg));
            }
        }

        const bool armHasString = !arm.str_labels.empty();
        const bool armHasNumeric = !arm.labels.empty() || !arm.ranges.empty() || !arm.rels.empty();

        if (armHasString && armHasNumeric)
            reportMixedLabelTypes(arm);

        if (armHasString)
        {
            if (selectorIsNumeric)
            {
                std::string msg(diag::ERR_SelectCase_StringLabelSelector.text);
                de.emit(il::support::Severity::Error,
                        std::string(diag::ERR_SelectCase_StringLabelSelector.id),
                        arm.range.begin,
                        1,
                        std::move(msg));
            }
            trackArmLabelKind(CaseArmLabelKind::String, arm);
            for (const auto &label : arm.str_labels)
            {
                if (!seenStringLabels.insert(label).second)
                {
                    std::string msg(diag::ERR_SelectCase_DuplicateLabel.text);
                    msg += ": \"";
                    msg += label;
                    msg += '"';
                    de.emit(il::support::Severity::Error,
                            std::string(diag::ERR_SelectCase_DuplicateLabel.id),
                            arm.range.begin,
                            1,
                            std::move(msg));
                }
            }
        }

        if (armHasNumeric)
        {
            if (selectorIsString)
            {
                std::string msg(diag::ERR_SelectCase_StringSelectorLabels.text);
                de.emit(il::support::Severity::Error,
                        std::string(diag::ERR_SelectCase_StringSelectorLabels.id),
                        arm.range.begin,
                        1,
                        std::move(msg));
            }
            trackArmLabelKind(CaseArmLabelKind::Numeric, arm);

            for (const auto &[rawLo, rawHi] : arm.ranges)
            {
                bool valid = true;
                if (rawLo < kCaseLabelMin || rawLo > kCaseLabelMax)
                {
                    std::string msg = "CASE range lower bound ";
                    msg += std::to_string(rawLo);
                    msg += " is outside 32-bit signed range";
                    de.emit(il::support::Severity::Error,
                            std::string(DiagSelectCaseLabelRange),
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
                    de.emit(il::support::Severity::Error,
                            std::string(DiagSelectCaseLabelRange),
                            arm.range.begin,
                            1,
                            std::move(msg));
                    valid = false;
                }

                if (rawLo > rawHi)
                {
                    std::string msg(diag::ERR_SelectCase_InvalidRange.text);
                    de.emit(il::support::Severity::Error,
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
                for (int32_t label : seenLabels)
                {
                    if (label >= lo && label <= hi)
                    {
                        std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                        de.emit(il::support::Severity::Error,
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
                    for (const auto &[seenLo, seenHi] : seenRanges)
                    {
                        if (!(hi < seenLo || lo > seenHi))
                        {
                            std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                            de.emit(il::support::Severity::Error,
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
                    RelInterval interval = makeRangeInterval(lo, hi);
                    for (const auto &seenRel : seenRelIntervals)
                    {
                        if (intervalsOverlap(interval, seenRel))
                        {
                            std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                            de.emit(il::support::Severity::Error,
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

                seenRanges.emplace_back(lo, hi);
            }

            for (int64_t rawLabel : arm.labels)
            {
                if (rawLabel < kCaseLabelMin || rawLabel > kCaseLabelMax)
                {
                    std::string msg = "CASE label ";
                    msg += std::to_string(rawLabel);
                    msg += " is outside 32-bit signed range";
                    de.emit(il::support::Severity::Error,
                            std::string(DiagSelectCaseLabelRange),
                            arm.range.begin,
                            1,
                            std::move(msg));
                    continue;
                }

                const int32_t label = static_cast<int32_t>(rawLabel);

                bool overlapsRange = false;
                for (const auto &[seenLo, seenHi] : seenRanges)
                {
                    if (label >= seenLo && label <= seenHi)
                    {
                        std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                        de.emit(il::support::Severity::Error,
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
                for (const auto &seenRel : seenRelIntervals)
                {
                    if (intervalContains(seenRel, label))
                    {
                        std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                        de.emit(il::support::Severity::Error,
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

                if (!seenLabels.insert(label).second)
                {
                    std::string msg(diag::ERR_SelectCase_DuplicateLabel.text);
                    msg += ": ";
                    msg += std::to_string(rawLabel);
                    de.emit(il::support::Severity::Error,
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
                    de.emit(il::support::Severity::Error,
                            std::string(DiagSelectCaseLabelRange),
                            arm.range.begin,
                            1,
                            std::move(msg));
                    continue;
                }

                const int32_t rhs = static_cast<int32_t>(rel.rhs);
                if (rel.op == CaseArm::CaseRel::Op::EQ)
                {
                    bool overlapsRange = false;
                    for (const auto &[seenLo, seenHi] : seenRanges)
                    {
                        if (rhs >= seenLo && rhs <= seenHi)
                        {
                            std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                            de.emit(il::support::Severity::Error,
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
                    for (const auto &seenRel : seenRelIntervals)
                    {
                        if (intervalContains(seenRel, rhs))
                        {
                            std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                            de.emit(il::support::Severity::Error,
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

                    if (!seenLabels.insert(rhs).second)
                    {
                        std::string msg(diag::ERR_SelectCase_DuplicateLabel.text);
                        msg += ": ";
                        msg += std::to_string(rel.rhs);
                        de.emit(il::support::Severity::Error,
                                std::string(diag::ERR_SelectCase_DuplicateLabel.id),
                                arm.range.begin,
                                1,
                                std::move(msg));
                    }
                    continue;
                }

                RelInterval interval = makeRelInterval(rel.op, rhs);
                bool overlaps = false;

                for (const auto &[seenLo, seenHi] : seenRanges)
                {
                    if (intervalsOverlap(interval, makeRangeInterval(seenLo, seenHi)))
                    {
                        std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                        de.emit(il::support::Severity::Error,
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
                    for (int32_t label : seenLabels)
                    {
                        if (intervalContains(interval, label))
                        {
                            std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                            de.emit(il::support::Severity::Error,
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
                    for (const auto &seenRel : seenRelIntervals)
                    {
                        if (intervalsOverlap(interval, seenRel))
                        {
                            std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
                            de.emit(il::support::Severity::Error,
                                    std::string(diag::ERR_SelectCase_OverlappingRange.id),
                                    arm.range.begin,
                                    1,
                                    std::move(msg));
                            overlaps = true;
                            break;
                        }
                    }

                    if (!overlaps)
                        seenRelIntervals.push_back(interval);
                }
            }
        }

        analyzeBody(arm.body);
    }

    if (!stmt.elseBody.empty())
        analyzeBody(stmt.elseBody);
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
