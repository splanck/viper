// File: src/frontends/basic/SelectCaseLowering.hpp
// Purpose: Declares helper responsible for lowering BASIC SELECT CASE statements.
// Key invariants: Maintains block structure compatible with Lowerer control flow.
// Ownership/Lifetime: Operates on a Lowerer instance without owning AST or IR.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/SelectModel.hpp"
#include "frontends/basic/ast/StmtControl.hpp"

#include "support/source_location.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
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

    struct CasePlanEntry
    {
        enum class Kind
        {
            StringLabel,
            RelLT,
            RelLE,
            RelEQ,
            RelGE,
            RelGT,
            Range,
            Default,
        };

        Kind kind{Kind::Default};
        std::pair<int32_t, int32_t> valueRange{0, 0};
        size_t armIndex{};
        il::core::BasicBlock *target{};
        il::support::SourceLoc loc{};
        std::string_view strLiteral{};
    };

    using ConditionEmitter = std::function<il::core::Value(const CasePlanEntry &)>;

    Blocks prepareBlocks(const SelectCaseStmt &stmt, bool hasCaseElse, bool needsDispatch);

    void lowerStringArms(const SelectCaseStmt &stmt,
                         const SelectModel &model,
                         const Blocks &blocks,
                         il::core::Value stringSelector);

    void lowerNumericDispatch(const SelectCaseStmt &stmt,
                              const SelectModel &model,
                              const Blocks &blocks,
                              il::core::Value selWide,
                              il::core::Value selector);

    size_t emitCompareChain(size_t startIdx,
                            std::vector<CasePlanEntry> &plan,
                            const ConditionEmitter &emitCond);

    static std::string_view blockTagFor(const CasePlanEntry &entry);

    void emitSwitchJumpTable(const SelectCaseStmt &stmt,
                             const SelectModel &model,
                             const Blocks &blocks,
                             il::core::Value selector,
                             size_t switchIdx);

    void emitArmBody(const std::vector<StmtPtr> &body,
                     il::core::BasicBlock *entry,
                     il::support::SourceLoc loc,
                     il::core::BasicBlock *endBlk);

    Lowerer &lowerer_;
};

} // namespace il::frontends::basic
