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
#include <string>
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

    enum class CaseKind
    {
        String,
        Rel,
        Range,
    };

    struct ComparePlanEntry
    {
        CaseKind kind{CaseKind::String};
        size_t armIndex{};
        il::support::SourceLoc loc{};
        const std::string *strLabel{};
        const CaseArm::CaseRel *rel{};
        int32_t lo{};
        int32_t hi{};
    };

    using ComparePlan = std::vector<ComparePlanEntry>;

    Blocks prepareBlocks(const SelectCaseStmt &stmt, bool hasCaseElse, bool needsDispatch);

    void lowerStringArms(const SelectCaseStmt &stmt,
                         const Blocks &blocks,
                         il::core::Value stringSelector,
                         const ComparePlan &plan);

    void lowerNumericDispatch(const SelectCaseStmt &stmt,
                              const Blocks &blocks,
                              il::core::Value selWide,
                              il::core::Value selector,
                              const ComparePlan &plan);

    void emitSwitchJumpTable(const SelectCaseStmt &stmt,
                             const Blocks &blocks,
                             il::core::Value selector);

    il::core::BasicBlock *emitCompareChain(const Blocks &blocks,
                                           size_t startIdx,
                                           il::core::BasicBlock *defaultBlk,
                                           const ComparePlan &plan,
                                           il::core::Value stringSelector,
                                           il::core::Value selWide,
                                           il::support::SourceLoc fallbackLoc);

    void emitArmBody(const std::vector<StmtPtr> &body,
                     il::core::BasicBlock *entry,
                     il::support::SourceLoc loc,
                     il::core::BasicBlock *endBlk);

    Lowerer &lowerer_;
};

} // namespace il::frontends::basic
