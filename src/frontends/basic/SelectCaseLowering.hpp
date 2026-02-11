//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/SelectCaseLowering.hpp
// Purpose: Lowers BASIC SELECT CASE statements to IL conditional branch chains.
//          Evaluates the selector once, then emits per-arm compare-and-branch
//          sequences with support for IS, TO, value list, and CASE ELSE clauses.
// Key invariants: Selector is evaluated once and stored in a temporary.
//                 All arm bodies branch to a common exit block.
// Ownership/Lifetime: Borrows Lowerer reference; does not own AST or IL.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//
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
    /// @brief Construct a SELECT CASE lowering helper bound to a Lowerer instance.
    /// @param lowerer Parent lowerer providing context and helper methods.
    explicit SelectCaseLowering(Lowerer &lowerer) noexcept;

    /// @brief Lower the provided SELECT CASE statement.
    /// @param stmt SELECT CASE statement AST node to lower.
    void lower(const SelectCaseStmt &stmt);

  private:
    /// @brief Block indices produced by prepareBlocks for SELECT CASE lowering.
    struct Blocks
    {
        size_t currentIdx{};              ///< Index of the block active at SELECT entry.
        std::vector<size_t> armIdx;       ///< Indices of per-arm body blocks.
        std::optional<size_t> elseIdx;    ///< Index of the CASE ELSE block, if present.
        size_t switchIdx{};               ///< Index of the dispatch/switch block.
        size_t endIdx{};                  ///< Index of the common exit block.
    };

    /// @brief Describes a single comparison entry in the case dispatch plan.
    struct CasePlanEntry
    {
        /// @brief Classification of the case comparison type.
        enum class Kind
        {
            StringLabel, ///< Case tests a string literal via equality.
            RelLT,       ///< Relational: selector < value.
            RelLE,       ///< Relational: selector <= value.
            RelEQ,       ///< Relational: selector == value.
            RelGE,       ///< Relational: selector >= value.
            RelGT,       ///< Relational: selector > value.
            Range,       ///< Range test: lo <= selector <= hi.
            Default,     ///< CASE ELSE (unconditional fallback).
        };

        Kind kind{Kind::Default};                          ///< Comparison classification.
        std::pair<int32_t, int32_t> valueRange{0, 0};     ///< Integer bounds for Range/relational entries.
        size_t armIndex{};                                 ///< Index into the arm body block vector.
        size_t targetIdx{SIZE_MAX};                        ///< Block index of the branch target.
        il::support::SourceLoc loc{};                      ///< Source location for diagnostics.
        std::string_view strLiteral{};                     ///< String literal for StringLabel entries.
    };

    /// @brief Callback type that emits a comparison for a single case plan entry.
    using ConditionEmitter = std::function<il::core::Value(const CasePlanEntry &)>;

    /// @brief Allocate all basic blocks needed for the SELECT CASE structure.
    /// @param stmt SELECT CASE statement AST node.
    /// @param hasCaseElse True when a CASE ELSE arm is present.
    /// @param needsDispatch True when a dispatch/switch block is needed.
    /// @return Block indices for all arms, else, dispatch, and exit.
    Blocks prepareBlocks(const SelectCaseStmt &stmt, bool hasCaseElse, bool needsDispatch);

    /// @brief Lower string-typed SELECT CASE arms via equality comparisons.
    /// @param stmt SELECT CASE statement AST node.
    /// @param model Analyzed case model with arm metadata.
    /// @param blocks Pre-allocated block indices.
    /// @param stringSelector IL value holding the evaluated string selector.
    void lowerStringArms(const SelectCaseStmt &stmt,
                         const SelectModel &model,
                         const Blocks &blocks,
                         il::core::Value stringSelector);

    /// @brief Lower numeric SELECT CASE arms via compare chains or jump tables.
    /// @param stmt SELECT CASE statement AST node.
    /// @param model Analyzed case model with arm metadata.
    /// @param blocks Pre-allocated block indices.
    /// @param selWide Widened (i64) selector value for range comparisons.
    /// @param selector Original selector value for direct comparisons.
    void lowerNumericDispatch(const SelectCaseStmt &stmt,
                              const SelectModel &model,
                              const Blocks &blocks,
                              il::core::Value selWide,
                              il::core::Value selector);

    /// @brief Emit a chain of conditional branches for the given plan entries.
    /// @param startIdx Block index to start emission from.
    /// @param plan Mutable plan entries with target indices filled in.
    /// @param emitCond Callback to emit the condition value for each entry.
    /// @return Block index following the last emitted comparison block.
    size_t emitCompareChain(size_t startIdx,
                            std::vector<CasePlanEntry> &plan,
                            const ConditionEmitter &emitCond);

    /// @brief Return a descriptive block tag string for a plan entry kind.
    /// @param entry Case plan entry to describe.
    /// @return Short tag string used for block naming.
    static std::string_view blockTagFor(const CasePlanEntry &entry);

    /// @brief Emit a switch/jump-table dispatch for dense integer ranges.
    /// @param stmt SELECT CASE statement AST node.
    /// @param model Analyzed case model with arm metadata.
    /// @param blocks Pre-allocated block indices.
    /// @param selector IL value holding the integer selector.
    /// @param switchIdx Block index of the switch dispatch block.
    void emitSwitchJumpTable(const SelectCaseStmt &stmt,
                             const SelectModel &model,
                             const Blocks &blocks,
                             il::core::Value selector,
                             size_t switchIdx);

    /// @brief Emit the body statements for a single CASE arm.
    /// @param body Statements comprising the arm body.
    /// @param entry Basic block to emit the body into.
    /// @param loc Source location for the arm.
    /// @param endBlkIdx Block index of the common exit block to branch to.
    void emitArmBody(const std::vector<StmtPtr> &body,
                     il::core::BasicBlock *entry,
                     il::support::SourceLoc loc,
                     size_t endBlkIdx);

    Lowerer &lowerer_; ///< Parent lowerer providing context and helpers.
};

} // namespace il::frontends::basic
