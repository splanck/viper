//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Internal helpers for SELECT CASE semantic checking.
/// @details Defines context objects and routines shared between exported
///          dispatcher functions, keeping the translation unit focused on
///          orchestration.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/sem/Check_Common.hpp"

#include "frontends/basic/BasicDiagnosticMessages.hpp"
#include "frontends/basic/SelectCaseRange.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>

namespace il::frontends::basic
{

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

} // namespace il::frontends::basic

namespace il::frontends::basic::sem::detail
{
namespace
{
using il::frontends::basic::kCaseLabelMax;
using il::frontends::basic::kCaseLabelMin;

using ArmContext = SemanticAnalyzer::SelectCaseArmContext;
using LabelKind = SemanticAnalyzer::SelectCaseArmContext::LabelKind;
using RelInterval = SemanticAnalyzer::SelectCaseArmContext::RelInterval;

inline bool isCaseElseArm(const CaseArm &arm)
{
    return arm.labels.empty() && arm.ranges.empty() && arm.rels.empty() &&
           arm.str_labels.empty();
}

inline RelInterval makeRangeInterval(int32_t lo, int32_t hi)
{
    RelInterval interval;
    interval.hasLo = true;
    interval.lo = static_cast<int64_t>(lo);
    interval.hasHi = true;
    interval.hi = static_cast<int64_t>(hi);
    return interval;
}

inline RelInterval makeRelInterval(CaseArm::CaseRel::Op op, int32_t rhs)
{
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
}

inline bool intervalsOverlap(const RelInterval &lhs, const RelInterval &rhs)
{
    const int64_t lo = std::max(lhs.hasLo ? lhs.lo : std::numeric_limits<int64_t>::min(),
                                rhs.hasLo ? rhs.lo : std::numeric_limits<int64_t>::min());
    const int64_t hi = std::min(lhs.hasHi ? lhs.hi : std::numeric_limits<int64_t>::max(),
                                rhs.hasHi ? rhs.hi : std::numeric_limits<int64_t>::max());
    return lo <= hi;
}

inline bool intervalContains(const RelInterval &interval, int32_t value)
{
    if (interval.hasLo && static_cast<int64_t>(value) < interval.lo)
        return false;
    if (interval.hasHi && static_cast<int64_t>(value) > interval.hi)
        return false;
    return true;
}

inline void emitOverlap(ArmContext &ctx, const CaseArm &arm)
{
    std::string msg(diag::ERR_SelectCase_OverlappingRange.text);
    ctx.de.emit(il::support::Severity::Error,
                std::string(diag::ERR_SelectCase_OverlappingRange.id),
                arm.range.begin,
                1,
                std::move(msg));
}

inline bool checkIntervalCollision(ArmContext &ctx,
                                   const CaseArm &arm,
                                   const RelInterval &interval)
{
    const auto collidesRange = std::any_of(
        ctx.seenRanges.begin(),
        ctx.seenRanges.end(),
        [&](const auto &seen) {
            return intervalsOverlap(interval, makeRangeInterval(seen.first, seen.second));
        });
    if (collidesRange)
    {
        emitOverlap(ctx, arm);
        return true;
    }

    const auto collidesLabel = std::any_of(
        ctx.seenLabels.begin(),
        ctx.seenLabels.end(),
        [&](int32_t label) { return intervalContains(interval, label); });
    if (collidesLabel)
    {
        emitOverlap(ctx, arm);
        return true;
    }

    const auto collidesRel = std::any_of(
        ctx.seenRelIntervals.begin(),
        ctx.seenRelIntervals.end(),
        [&](const RelInterval &seen) { return intervalsOverlap(interval, seen); });
    if (collidesRel)
    {
        emitOverlap(ctx, arm);
        return true;
    }

    return false;
}

inline void noteCaseElse(ArmContext &ctx, const CaseArm &arm)
{
    ++ctx.caseElseCount;
    if (ctx.caseElseCount <= 1)
        return;

    std::string msg(diag::ERR_SelectCase_DuplicateElse.text);
    ctx.de.emit(il::support::Severity::Error,
                std::string(diag::ERR_SelectCase_DuplicateElse.id),
                arm.range.begin,
                1,
                std::move(msg));
}

inline void reportMixedLabelTypes(ArmContext &ctx, const CaseArm &arm)
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

inline void trackArmLabelKind(ArmContext &ctx, LabelKind kind, const CaseArm &arm)
{
    if (kind == LabelKind::None || ctx.reportedMixedLabelTypes)
        return;
    if (ctx.seenArmLabelKind == LabelKind::None)
    {
        ctx.seenArmLabelKind = kind;
        return;
    }
    if (ctx.seenArmLabelKind != kind)
        reportMixedLabelTypes(ctx, arm);
}

inline void emitRangeBoundError(ArmContext &ctx,
                                const CaseArm &arm,
                                const char *which,
                                int64_t value)
{
    std::string msg = "CASE range ";
    msg += which;
    msg += " bound ";
    msg += std::to_string(value);
    msg += " is outside 32-bit signed range";
    ctx.de.emit(il::support::Severity::Error,
                std::string(SemanticAnalyzer::DiagSelectCaseLabelRange),
                arm.range.begin,
                1,
                std::move(msg));
}

inline bool validateRangeBounds(ArmContext &ctx,
                                const CaseArm &arm,
                                int64_t rawLo,
                                int64_t rawHi)
{
    bool valid = true;
    if (rawLo < kCaseLabelMin || rawLo > kCaseLabelMax)
    {
        emitRangeBoundError(ctx, arm, "lower", rawLo);
        valid = false;
    }
    if (rawHi < kCaseLabelMin || rawHi > kCaseLabelMax)
    {
        emitRangeBoundError(ctx, arm, "upper", rawHi);
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
    return valid;
}

inline bool emitOutOfRangeLabel(const CaseArm &arm, ArmContext &ctx, int64_t raw)
{
    if (raw >= kCaseLabelMin && raw <= kCaseLabelMax)
        return false;

    std::string msg = il::frontends::basic::makeSelectCaseLabelRangeMessage(raw);
    ctx.de.emit(il::support::Severity::Error,
                std::string(SemanticAnalyzer::DiagSelectCaseLabelRange),
                arm.range.begin,
                1,
                std::move(msg));
    return true;
}

inline void emitDuplicateLabel(ArmContext &ctx, const CaseArm &arm, std::string label)
{
    std::string msg(diag::ERR_SelectCase_DuplicateLabel.text);
    msg += ": ";
    msg += std::move(label);
    ctx.de.emit(il::support::Severity::Error,
                std::string(diag::ERR_SelectCase_DuplicateLabel.id),
                arm.range.begin,
                1,
                std::move(msg));
}

} // namespace

inline SemanticAnalyzer::SelectCaseSelectorInfo
classifySelectCaseSelector(ControlCheckContext &context, const SelectCaseStmt &stmt)
{
    SemanticAnalyzer::SelectCaseSelectorInfo info;
    if (!stmt.selector)
        return info;

    using Type = SemanticAnalyzer::Type;
    const Type selectorType = context.evaluateExpr(*stmt.selector);
    if (selectorType == Type::Int)
    {
        context.markImplicitConversion(*stmt.selector, Type::Int);
        info.selectorIsNumeric = true;
        return info;
    }
    if (selectorType == Type::String)
    {
        info.selectorIsString = true;
        return info;
    }
    if (selectorType == Type::Unknown)
        return info;

    std::string msg(diag::ERR_SelectCase_NonIntegerSelector.text);
    context.diagnostics().emit(il::support::Severity::Error,
                               std::string(diag::ERR_SelectCase_NonIntegerSelector.id),
                               stmt.selector->loc,
                               1,
                               std::move(msg));
    info.fatal = true;
    return info;
}

inline bool validateSelectCaseStringArm(const CaseArm &arm, ArmContext &ctx)
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

    trackArmLabelKind(ctx, LabelKind::String, arm);
    for (const auto &label : arm.str_labels)
    {
        if (ctx.seenStringLabels.insert(label).second)
            continue;
        emitDuplicateLabel(ctx, arm, '"' + label + '"');
    }
    return true;
}

inline bool validateSelectCaseNumericArm(const CaseArm &arm, ArmContext &ctx)
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

    trackArmLabelKind(ctx, LabelKind::Numeric, arm);

    for (const auto &[rawLo, rawHi] : arm.ranges)
    {
        if (!validateRangeBounds(ctx, arm, rawLo, rawHi))
            continue;

        const auto interval = makeRangeInterval(static_cast<int32_t>(rawLo),
                                                static_cast<int32_t>(rawHi));
        if (checkIntervalCollision(ctx, arm, interval))
            continue;

        ctx.seenRanges.emplace_back(static_cast<int32_t>(rawLo),
                                    static_cast<int32_t>(rawHi));
    }

    for (int64_t rawLabel : arm.labels)
    {
        if (emitOutOfRangeLabel(arm, ctx, rawLabel))
            continue;

        const int32_t label = static_cast<int32_t>(rawLabel);
        if (auto [_, inserted] = ctx.seenLabels.insert(label); !inserted)
            emitDuplicateLabel(ctx, arm, std::to_string(label));
    }

    for (const auto &rel : arm.rels)
    {
        if (emitOutOfRangeLabel(arm, ctx, rel.rhs))
            continue;

        const int32_t rhs = static_cast<int32_t>(rel.rhs);
        const auto interval = makeRelInterval(rel.op, rhs);
        if (checkIntervalCollision(ctx, arm, interval))
            continue;

        if (rel.op == CaseArm::CaseRel::Op::EQ)
        {
            if (auto [_, inserted] = ctx.seenLabels.insert(rhs); !inserted)
                emitDuplicateLabel(ctx, arm, std::to_string(rel.rhs));
            continue;
        }

        ctx.seenRelIntervals.push_back(interval);
    }

    return true;
}

inline bool validateSelectCaseArm(const CaseArm &arm, ArmContext &ctx)
{
    if (isCaseElseArm(arm))
    {
        noteCaseElse(ctx, arm);
        return true;
    }

    const bool hasString = !arm.str_labels.empty();
    const bool hasNumeric = !arm.labels.empty() || !arm.ranges.empty() ||
                            !arm.rels.empty();
    if (hasString && hasNumeric)
        reportMixedLabelTypes(ctx, arm);

    bool ok = true;
    if (hasString)
        ok = validateSelectCaseStringArm(arm, ctx) && ok;
    if (hasNumeric)
        ok = validateSelectCaseNumericArm(arm, ctx) && ok;
    return ok;
}

inline void analyzeSelectCaseBody(ControlCheckContext &context,
                                  const std::vector<StmtPtr> &body)
{
    auto scope = context.pushScope();
    for (const auto &child : body)
    {
        if (!child)
            continue;
        context.visitStmt(*child);
    }
}

} // namespace il::frontends::basic::sem::detail
