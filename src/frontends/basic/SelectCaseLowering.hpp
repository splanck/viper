//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SelectCaseLowering helper class, which is responsible
// for lowering BASIC SELECT CASE statements to IL control flow.
//
// SELECT CASE Lowering:
// The SELECT CASE statement provides multi-way branching based on the value
// of a test expression:
//
//   SELECT CASE score
//     CASE IS < 60
//       PRINT "F"
//     CASE 60 TO 69
//       PRINT "D"
//     CASE 70, 80, 90
//       PRINT "C, B, or A"
//     CASE ELSE
//       PRINT "Invalid"
//   END SELECT
//
// This is lowered to a series of IL conditional branches and basic blocks.
//
// Lowering Strategy:
// The lowerer generates IL code that:
// 1. Evaluates the SELECT expression once and stores it in a temporary
// 2. For each CASE clause, generates comparison(s) against the temporary
// 3. Branches to the appropriate CASE body or continues to the next test
// 4. CASE ELSE provides a default branch if no cases match
// 5. All CASE bodies branch to a common exit block at END SELECT
//
// CASE Clause Types:
// - CASE IS <op> <expr>: Relational test (IS < 60, IS >= 100)
// - CASE <expr> TO <expr>: Range test (60 TO 69)
// - CASE <expr>, <expr>, ...: Value list (70, 80, 90)
// - CASE ELSE: Default clause (matches if no other case matches)
//
// IL Block Structure:
//   entry:
//     %temp = <select-expr>
//   case1_test:
//     if (<case1-condition>) goto case1_body else goto case2_test
//   case1_body:
//     <case1-statements>
//     goto exit
//   case2_test:
//     ...
//   exit:
//     <continue>
//
// Integration:
// - Used by: Lowerer when lowering SelectStmt AST nodes
// - Operates on: Lowerer instance state (IRBuilder, NameMangler)
// - No ownership: Does not own AST or IL module
//
// Design Notes:
// - Maintains block structure compatible with Lowerer control flow
// - Generates efficient IL by evaluating select expression once
// - Properly handles fallthrough and CASE ELSE semantics
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
                     size_t endBlkIdx);

    Lowerer &lowerer_;
};

} // namespace il::frontends::basic
