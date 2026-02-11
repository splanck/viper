//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/LowerStmt_Control.hpp
// Purpose: Declares control-flow lowering helpers for BASIC statements such as
// Key invariants: Helpers mutate the current block through the active Lowerer
// Ownership/Lifetime: Declarations are included inside Lowerer to extend its
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

/// @brief Control-flow state emitted by structured statement helpers.
/// @details `cur` tracks the block left active after lowering, while
///          `after` stores the merge/done block when it survives the
///          lowering step. Helpers mark `fallthrough` when execution can
///          reach `after` without an explicit transfer, ensuring callers
///          can reason about terminators consistently.
struct CtrlState
{
    BasicBlock *cur{nullptr};   ///< Block left active after lowering.
    BasicBlock *after{nullptr}; ///< Merge/done block if retained.
    bool fallthrough{false};    ///< True when `after` remains reachable.

    /// @brief Check whether the control flow has been terminated.
    /// @return True if no active block remains or the active block already has
    ///         a terminator instruction.
    [[nodiscard]] bool terminated() const
    {
        return !cur || cur->terminated;
    }
};

//===----------------------------------------------------------------------===//
/// @name IF / ELSEIF / ELSE Lowering
/// @{
//===----------------------------------------------------------------------===//

/// @brief Create the basic block structure for an IF statement with @p conds branches.
/// @param conds Number of condition/then pairs (1 for plain IF, more for ELSEIF chains).
/// @return An IfBlocks structure containing test, then, else, and merge blocks.
IfBlocks emitIfBlocks(size_t conds);

/// @brief Lower a single IF condition, branching to then/false blocks.
/// @param cond The condition expression AST node.
/// @param testBlk The basic block where the condition is evaluated.
/// @param thenBlk The basic block to branch to if the condition is true.
/// @param falseBlk The basic block to branch to if the condition is false.
/// @param loc Source location for diagnostics.
void lowerIfCondition(const Expr &cond,
                      BasicBlock *testBlk,
                      BasicBlock *thenBlk,
                      BasicBlock *falseBlk,
                      il::support::SourceLoc loc);

/// @brief Lower a boolean expression as a conditional branch to two targets.
/// @param expr The boolean expression AST node.
/// @param trueBlk The basic block to jump to when the expression is true.
/// @param falseBlk The basic block to jump to when the expression is false.
/// @param loc Source location for diagnostics.
void lowerCondBranch(const Expr &expr,
                     BasicBlock *trueBlk,
                     BasicBlock *falseBlk,
                     il::support::SourceLoc loc);

/// @brief Lower the body of a single IF/ELSEIF branch.
/// @param stmt The statement (or block) forming the branch body. May be nullptr
///             for empty branches.
/// @param thenBlk The basic block to emit the body into.
/// @param exitIdx Index into the IfBlocks exit array for the branch target.
/// @param loc Source location for diagnostics.
/// @return True if the branch body falls through (does not terminate).
bool lowerIfBranch(const Stmt *stmt,
                   BasicBlock *thenBlk,
                   size_t exitIdx,
                   il::support::SourceLoc loc);

/// @brief Emit the full IL for an IF/ELSEIF/ELSE statement.
/// @param stmt The parsed IfStmt AST node.
/// @return CtrlState describing the blocks and fall-through after lowering.
CtrlState emitIf(const IfStmt &stmt);

/// @brief Top-level entry point for lowering an IF statement.
/// @param stmt The IfStmt AST node to lower.
void lowerIf(const IfStmt &stmt);

/// @}

//===----------------------------------------------------------------------===//
/// @name Loop Lowering (WHILE, DO, FOR)
/// @{
//===----------------------------------------------------------------------===//

/// @brief Lower a sequence of statements forming a loop body.
/// @param body The vector of statements inside the loop.
void lowerLoopBody(const std::vector<StmtPtr> &body);

/// @brief Emit the IL for a WHILE...WEND loop.
/// @param stmt The parsed WhileStmt AST node.
/// @return CtrlState describing the blocks after lowering.
CtrlState emitWhile(const WhileStmt &stmt);

/// @brief Top-level entry point for lowering a WHILE statement.
/// @param stmt The WhileStmt AST node to lower.
void lowerWhile(const WhileStmt &stmt);

/// @brief Emit the IL for a DO...LOOP statement (with optional WHILE/UNTIL).
/// @param stmt The parsed DoStmt AST node.
/// @return CtrlState describing the blocks after lowering.
CtrlState emitDo(const DoStmt &stmt);

/// @brief Top-level entry point for lowering a DO statement.
/// @param stmt The DoStmt AST node to lower.
void lowerDo(const DoStmt &stmt);

/// @brief Create the basic block structure for a FOR loop.
/// @param varStep True if the step value is a variable (not a compile-time constant),
///                which requires a runtime direction check.
/// @return A ForBlocks structure containing header, body, step, and exit blocks.
ForBlocks setupForBlocks(bool varStep);

/// @brief Lower a FOR loop with a compile-time constant step value.
/// @param stmt The ForStmt AST node.
/// @param slot The alloca holding the loop counter variable.
/// @param end The upper/lower bound value.
/// @param step The step value.
/// @param stepConst The compile-time constant step value (for direction optimization).
void lowerForConstStep(const ForStmt &stmt, Value slot, RVal end, RVal step, int64_t stepConst);

/// @brief Lower a FOR loop with a variable (runtime) step value.
/// @param stmt The ForStmt AST node.
/// @param slot The alloca holding the loop counter variable.
/// @param end The upper/lower bound value.
/// @param step The step value (evaluated at runtime each iteration).
void lowerForVarStep(const ForStmt &stmt, Value slot, RVal end, RVal step);

/// @brief Emit the IL for a FOR...NEXT loop.
/// @param stmt The ForStmt AST node.
/// @param slot The loop counter variable slot.
/// @param end The loop bound value.
/// @param step The loop step value.
/// @return CtrlState describing the blocks after lowering.
CtrlState emitFor(const ForStmt &stmt, Value slot, RVal end, RVal step);

/// @brief Top-level entry point for lowering a FOR statement.
/// @param stmt The ForStmt AST node to lower.
void lowerFor(const ForStmt &stmt);

/// @brief Emit the step increment for a FOR loop iteration.
/// @param slot The loop counter variable slot.
/// @param step The step value to add to the counter.
void emitForStep(Value slot, Value step);

/// @}

//===----------------------------------------------------------------------===//
/// @name SELECT CASE / NEXT / EXIT / Jump Lowering
/// @{
//===----------------------------------------------------------------------===//

/// @brief Emit the IL for a SELECT CASE statement (multi-way branch).
/// @param stmt The SelectCaseStmt AST node.
/// @return CtrlState describing the blocks after lowering.
CtrlState emitSelect(const SelectCaseStmt &stmt);

/// @brief Lower a NEXT statement (advances the innermost FOR loop).
/// @param stmt The NextStmt AST node.
void lowerNext(const NextStmt &stmt);

/// @brief Lower an EXIT statement (breaks out of a loop or procedure).
/// @param stmt The ExitStmt AST node.
void lowerExit(const ExitStmt &stmt);

/// @brief Lower a GOSUB statement (push return address, jump to label).
/// @param stmt The GosubStmt AST node.
void lowerGosub(const GosubStmt &stmt);

/// @brief Lower a GOTO statement (unconditional jump to a label).
/// @param stmt The GotoStmt AST node.
void lowerGoto(const GotoStmt &stmt);

/// @brief Lower a RETURN statement inside a GOSUB (pop return address, jump back).
/// @param stmt The ReturnStmt AST node.
void lowerGosubReturn(const ReturnStmt &stmt);

/// @brief Lower an ON ERROR GOTO statement (install error handler).
/// @param stmt The OnErrorGoto AST node.
void lowerOnErrorGoto(const OnErrorGoto &stmt);

/// @brief Lower a RESUME statement (resume execution after error handling).
/// @param stmt The Resume AST node.
void lowerResume(const Resume &stmt);

/// @brief Lower an END statement (terminate program execution).
/// @param stmt The EndStmt AST node.
void lowerEnd(const EndStmt &stmt);

/// @}
