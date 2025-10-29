//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SelectModel.cpp
// Purpose: Implement the shared SELECT CASE model builder.
// Key invariants: Normalises label data into 32-bit ranges and relations while
//                 surfacing diagnostics through the supplied callback.
// Ownership/Lifetime: Operates on AST references without taking ownership.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/SelectModel.hpp"

#include "frontends/basic/SelectCaseRange.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/ast/StmtControl.hpp"

#include <utility>

namespace il::frontends::basic
{

/// @brief Construct a builder that reports diagnostics through @p diagnose.
/// @details Stores the callback so later conversions can surface range and type
///          issues using the caller's diagnostic machinery.  The builder itself
///          maintains no additional state beyond the callback.
/// @param diagnose Function invoked when semantic issues are detected.
SelectModelBuilder::SelectModelBuilder(DiagnoseFn diagnose) noexcept
    : diagnose_(std::move(diagnose))
{
}

/// @brief Narrow a 64-bit literal to the 32-bit range allowed by SELECT CASE.
/// @details Checks the bounds mandated by the BASIC specification and emits a
///          diagnostic through the stored callback when @p value falls outside
///          the permitted interval.  Successful conversions return the narrowed
///          value wrapped in @c std::optional.
/// @param value Literal being narrowed.
/// @param loc Source location used when reporting diagnostics.
/// @return Narrowed value on success; @c std::nullopt when out of range.
std::optional<int32_t> SelectModelBuilder::narrowToI32(int64_t value,
                                                       il::support::SourceLoc loc) const
{
    if (value < kCaseLabelMin || value > kCaseLabelMax)
    {
        if (diagnose_)
        {
            diagnose_(loc,
                      1,
                      makeSelectCaseLabelRangeMessage(value),
                      SemanticAnalyzer::DiagSelectCaseLabelRange);
        }
        return std::nullopt;
    }
    return static_cast<int32_t>(value);
}

/// @brief Construct the canonical model describing a SELECT CASE statement.
/// @details Iterates the statement's clauses, narrows literal values to the
///          runtime representation, records relational operators, and captures
///          the location of each branch target.  The resulting model is consumed
///          by lowering code to emit efficient IL while preserving diagnostic
///          fidelity.
/// @param stmt AST node describing the SELECT CASE statement.
/// @return Structured model containing normalised case ranges and metadata.
SelectModel SelectModelBuilder::build(const SelectCaseStmt &stmt)
{
    SelectModel model{};
    model.hasCaseElse = !stmt.elseBody.empty();

    size_t strCount = 0;
    size_t labelCount = 0;
    size_t rangeCount = 0;
    size_t relCount = 0;
    for (const auto &arm : stmt.arms)
    {
        strCount += arm.str_labels.size();
        labelCount += arm.labels.size();
        rangeCount += arm.ranges.size();
        relCount += arm.rels.size();
    }

    model.stringLabels.reserve(strCount);
    model.numericLabels.reserve(labelCount);
    model.numericRanges.reserve(rangeCount);
    model.numericRelations.reserve(relCount);

    for (size_t index = 0; index < stmt.arms.size(); ++index)
    {
        const CaseArm &arm = stmt.arms[index];
        const il::support::SourceLoc loc = arm.range.begin;

        for (const auto &str : arm.str_labels)
        {
            model.stringLabels.push_back({std::string_view(str), index, loc});
        }

        for (int64_t rawLabel : arm.labels)
        {
            if (auto narrowed = narrowToI32(rawLabel, loc))
            {
                model.numericLabels.push_back({*narrowed, index, loc});
            }
        }

        for (const auto &[rawLo, rawHi] : arm.ranges)
        {
            auto narrowedLo = narrowToI32(rawLo, loc);
            auto narrowedHi = narrowToI32(rawHi, loc);
            if (!narrowedLo || !narrowedHi)
                continue;

            model.numericRanges.push_back({*narrowedLo, *narrowedHi, index, loc});
            model.hasNumericRanges = true;
        }

        for (const auto &rel : arm.rels)
        {
            if (auto narrowed = narrowToI32(rel.rhs, loc))
            {
                SelectModel::NumericRelation entry{};
                entry.armIndex = index;
                entry.loc = loc;
                entry.rhs = *narrowed;
                switch (rel.op)
                {
                    case CaseArm::CaseRel::Op::LT:
                        entry.op = SelectModel::NumericRelation::Op::LT;
                        break;
                    case CaseArm::CaseRel::Op::LE:
                        entry.op = SelectModel::NumericRelation::Op::LE;
                        break;
                    case CaseArm::CaseRel::Op::EQ:
                        entry.op = SelectModel::NumericRelation::Op::EQ;
                        break;
                    case CaseArm::CaseRel::Op::GE:
                        entry.op = SelectModel::NumericRelation::Op::GE;
                        break;
                    case CaseArm::CaseRel::Op::GT:
                        entry.op = SelectModel::NumericRelation::Op::GT;
                        break;
                }
                model.numericRelations.push_back(entry);
            }
        }
    }

    return model;
}

} // namespace il::frontends::basic
