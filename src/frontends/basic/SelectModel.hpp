//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SelectModel.hpp
// Purpose: Define a normalized SELECT CASE model shared between parser and
//          lowering.
// Key invariants: Captures validated 32-bit ranges, labels, and relations,
//                 tracking CASE ELSE coverage without duplicating work across
//                 pipeline stages.
// Ownership/Lifetime: Views reference strings owned by the AST; consumers must
//                     keep the originating AST alive while using the model.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/source_location.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace il::frontends::basic
{

struct CaseArm;
struct SelectCaseStmt;

/// @brief Canonical representation of SELECT CASE dispatch data.
struct SelectModel
{
    struct NumericLabel
    {
        int32_t value = 0;
        size_t armIndex = 0;
        il::support::SourceLoc loc{};
    };

    struct NumericRange
    {
        int32_t lo = 0;
        int32_t hi = 0;
        size_t armIndex = 0;
        il::support::SourceLoc loc{};
    };

    struct NumericRelation
    {
        enum class Op
        {
            LT,
            LE,
            EQ,
            GE,
            GT,
        };

        Op op = Op::EQ;
        int32_t rhs = 0;
        size_t armIndex = 0;
        il::support::SourceLoc loc{};
    };

    struct StringLabel
    {
        std::string_view value{};
        size_t armIndex = 0;
        il::support::SourceLoc loc{};
    };

    std::vector<NumericLabel> numericLabels;
    std::vector<NumericRange> numericRanges;
    std::vector<NumericRelation> numericRelations;
    std::vector<StringLabel> stringLabels;
    bool hasCaseElse = false;
    bool hasNumericRanges = false;
};

/// @brief Build a normalized SELECT CASE model from parsed AST data.
class SelectModelBuilder
{
  public:
    using DiagnoseFn =
        std::function<void(il::support::SourceLoc, uint32_t, std::string_view, std::string_view)>;

    explicit SelectModelBuilder(DiagnoseFn diagnose) noexcept;

    /// @brief Create a model for the provided statement.
    [[nodiscard]] SelectModel build(const SelectCaseStmt &stmt);

  private:
    [[nodiscard]] std::optional<int32_t> narrowToI32(int64_t value,
                                                     il::support::SourceLoc loc) const;

    DiagnoseFn diagnose_;
};

} // namespace il::frontends::basic
