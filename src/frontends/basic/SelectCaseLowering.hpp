// File: src/frontends/basic/SelectCaseLowering.hpp
// Purpose: Declares helper responsible for lowering BASIC SELECT CASE statements.
// Key invariants: Maintains block structure compatible with Lowerer control flow.
// Ownership/Lifetime: Operates on a Lowerer instance without owning AST or IR.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtNodes.hpp"

#include "support/source_location.hpp"

#include <cstdint>
#include <functional>
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
        StringEq,
        RangeInclusive,
        RelLT,
        RelLE,
        RelEQ,
        RelGE,
        RelGT,
    };

    struct ValueRange
    {
        int32_t lo{};
        int32_t hi{};
    };

    struct CasePlanEntry
    {
        CaseKind kind{};
        ValueRange range{};
        size_t targetIdx{};
        il::support::SourceLoc loc{};
        std::string strPayload{};
    };

    using ConditionBuilder = std::function<il::core::Value(const CasePlanEntry &)>;

    Blocks prepareBlocks(const SelectCaseStmt &stmt, bool hasCaseElse, bool needsDispatch);

    void lowerStringArms(const SelectCaseStmt &stmt,
                         const Blocks &blocks,
                         il::core::Value stringSelector);

    void lowerNumericDispatch(const SelectCaseStmt &stmt,
                              const Blocks &blocks,
                              il::core::Value selWide,
                              il::core::Value selector,
                              bool hasRanges);

    size_t emitDecisionChain(const std::vector<CasePlanEntry> &plan,
                             size_t entryIdx,
                             std::optional<size_t> defaultIdx,
                             const ConditionBuilder &builder);

    void emitSwitchJumpTable(const SelectCaseStmt &stmt,
                             const Blocks &blocks,
                             il::core::Value selector,
                             size_t dispatchIdx);

    std::string chainLabelFor(CaseKind kind);

    void emitArmBody(const std::vector<StmtPtr> &body,
                     il::core::BasicBlock *entry,
                     il::support::SourceLoc loc,
                     il::core::BasicBlock *endBlk);

    Lowerer &lowerer_;
};

} // namespace il::frontends::basic
