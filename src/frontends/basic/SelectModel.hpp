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

    /// @brief Construct a builder that reports errors through the supplied callback.
    /// @details Stores @p diagnose so semantic issues discovered while normalising the SELECT
    ///          statement (such as overlapping numeric ranges) can surface through the same
    ///          diagnostic path used by the parser and lowerer.
    explicit SelectModelBuilder(DiagnoseFn diagnose) noexcept;

    /// @brief Create a normalised model for the provided SELECT CASE statement.
    /// @details Iterates each case arm, folds line-number labels, canonicalises numeric ranges,
    ///          and records whether CASE ELSE was present.  When the builder detects duplicate or
    ///          invalid arms it emits diagnostics via @ref DiagnoseFn while still producing a
    ///          best-effort model for downstream stages.
    [[nodiscard]] SelectModel build(const SelectCaseStmt &stmt);

  private:
    /// @brief Attempt to narrow a parsed integer literal into a 32-bit range.
    /// @details Emits a diagnostic when @p value does not fit into @c int32_t, returning
    ///          @c std::nullopt so the caller can skip recording the offending arm while keeping
    ///          the rest of the model consistent.
    [[nodiscard]] std::optional<int32_t> narrowToI32(int64_t value,
                                                     il::support::SourceLoc loc) const;

    DiagnoseFn diagnose_;
};

} // namespace il::frontends::basic
