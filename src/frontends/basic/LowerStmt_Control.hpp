// File: src/frontends/basic/LowerStmt_Control.hpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Declares control-flow lowering helpers for BASIC statements such as
//          IF, loops, and GOSUB/GOTO constructs.
// Key invariants: Helpers mutate the current block through the active Lowerer
//                 context while preserving deterministic block naming.
// Ownership/Lifetime: Declarations are included inside Lowerer to extend its
//                     private control-flow lowering surface.
// Links: docs/codemap.md
#pragma once

/// @brief Control-flow state emitted by structured statement helpers.
/// @details `cur` tracks the block left active after lowering, while
///          `after` stores the merge/done block when it survives the
///          lowering step. Helpers mark `fallthrough` when execution can
///          reach `after` without an explicit transfer, ensuring callers
///          can reason about terminators consistently.
struct CtrlState
{
    BasicBlock *cur{nullptr};      ///< Block left active after lowering.
    BasicBlock *after{nullptr};    ///< Merge/done block if retained.
    bool fallthrough{false};       ///< True when `after` remains reachable.

    [[nodiscard]] bool terminated() const
    {
        return !cur || cur->terminated;
    }
};

IfBlocks emitIfBlocks(size_t conds);
void lowerIfCondition(const Expr &cond,
                      BasicBlock *testBlk,
                      BasicBlock *thenBlk,
                      BasicBlock *falseBlk,
                      il::support::SourceLoc loc);
void lowerCondBranch(const Expr &expr,
                     BasicBlock *trueBlk,
                     BasicBlock *falseBlk,
                     il::support::SourceLoc loc);
bool lowerIfBranch(const Stmt *stmt,
                   BasicBlock *thenBlk,
                   BasicBlock *exitBlk,
                   il::support::SourceLoc loc);
CtrlState emitIf(const IfStmt &stmt);
void lowerIf(const IfStmt &stmt);
void lowerLoopBody(const std::vector<StmtPtr> &body);
CtrlState emitWhile(const WhileStmt &stmt);
void lowerWhile(const WhileStmt &stmt);
CtrlState emitDo(const DoStmt &stmt);
void lowerDo(const DoStmt &stmt);
ForBlocks setupForBlocks(bool varStep);
void lowerForConstStep(const ForStmt &stmt, Value slot, RVal end, RVal step, int64_t stepConst);
void lowerForVarStep(const ForStmt &stmt, Value slot, RVal end, RVal step);
CtrlState emitFor(const ForStmt &stmt, Value slot, RVal end, RVal step);
void lowerFor(const ForStmt &stmt);
void emitForStep(Value slot, Value step);
CtrlState emitSelect(const SelectCaseStmt &stmt);
void lowerNext(const NextStmt &stmt);
void lowerExit(const ExitStmt &stmt);
void lowerGosub(const GosubStmt &stmt);
void lowerGoto(const GotoStmt &stmt);
void lowerGosubReturn(const ReturnStmt &stmt);
void lowerOnErrorGoto(const OnErrorGoto &stmt);
void lowerResume(const Resume &stmt);
void lowerEnd(const EndStmt &stmt);
