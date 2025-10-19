// File: src/frontends/basic/SelectCaseLowering.hpp
// Purpose: Declares helper responsible for lowering BASIC SELECT CASE statements.
// Key invariants: Maintains block structure compatible with Lowerer control flow.
// Ownership/Lifetime: Operates on a Lowerer instance without owning AST or IR.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtNodes.hpp"

#include "support/source_location.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace il::core
{
struct BasicBlock;
struct Value;
} // namespace il::core

namespace il::frontends::basic
{

class Lowerer;

/// @brief Internal helper for lowering SELECT CASE statements.
/// @notes Encapsulates block preparation and dispatch emission logic to keep
///        Lowerer.hpp free of private implementation details.
class SelectCaseLowering
{
  public:
    explicit SelectCaseLowering(Lowerer &lowerer) noexcept;

    /// @brief Lower the provided SELECT CASE statement.
    void lower(const SelectCaseStmt &stmt);

  private:
    struct Blocks
    {
        size_t currentIdx{};
        std::vector<size_t> armIdx;
        std::optional<size_t> elseIdx;
        size_t switchIdx{};
        size_t endIdx{};
    };

    struct NumericDispatchState
    {
        struct RangeCheck
        {
            int32_t lo{};
            int32_t hi{};
            size_t armIndex{};
        };

        struct RelCheck
        {
            const CaseArm::CaseRel *rel{};
            size_t armIndex{};
        };

        std::vector<RangeCheck> rangeChecks;
        std::vector<RelCheck> relChecks;
        size_t afterRelIdx{};
        size_t switchIdx{};
    };

    Blocks prepareBlocks(const SelectCaseStmt &stmt, bool hasCaseElse, bool needsDispatch);

    void lowerStringArms(const SelectCaseStmt &stmt,
                         const Blocks &blocks,
                         il::core::Value stringSelector);

    void lowerNumericDispatch(const SelectCaseStmt &stmt,
                              const Blocks &blocks,
                              il::core::Value selWide,
                              il::core::Value selector,
                              bool hasRanges,
                              size_t totalRangeCount);

    void emitRelationalChecks(const SelectCaseStmt &stmt,
                              const Blocks &blocks,
                              il::core::Value selWide,
                              NumericDispatchState &state);

    void emitRangeChecks(const SelectCaseStmt &stmt,
                         const Blocks &blocks,
                         il::core::Value selWide,
                         NumericDispatchState &state);

    void emitSwitchJumpTable(const SelectCaseStmt &stmt,
                             const Blocks &blocks,
                             il::core::Value selector,
                             NumericDispatchState &state);

    void emitArmBody(const std::vector<StmtPtr> &body,
                     il::core::BasicBlock *entry,
                     il::support::SourceLoc loc,
                     il::core::BasicBlock *endBlk);

    Lowerer &lowerer_;
};

} // namespace il::frontends::basic
