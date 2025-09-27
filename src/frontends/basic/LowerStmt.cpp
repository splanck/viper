// File: src/frontends/basic/LowerStmt.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements BASIC statement lowering routines targeting IL.
// Key invariants: Control-flow block creation remains deterministic via
//                 Lowerer::BlockNamer or NameMangler fallbacks.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

// Requires the consolidated Lowerer interface for statement lowering helpers.
#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Visitor that dispatches statement lowering to Lowerer helpers.
class LowererStmtVisitor final : public StmtVisitor
{
  public:
    explicit LowererStmtVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void visit(const PrintStmt &stmt) override { lowerer_.lowerPrint(stmt); }

    void visit(const LetStmt &stmt) override { lowerer_.lowerLet(stmt); }

    void visit(const DimStmt &stmt) override
    {
        if (stmt.isArray)
            lowerer_.lowerDim(stmt);
    }

    void visit(const ReDimStmt &stmt) override { lowerer_.lowerReDim(stmt); }

    void visit(const RandomizeStmt &stmt) override { lowerer_.lowerRandomize(stmt); }

    void visit(const IfStmt &stmt) override { lowerer_.lowerIf(stmt); }

    void visit(const WhileStmt &stmt) override { lowerer_.lowerWhile(stmt); }

    void visit(const DoStmt &stmt) override { lowerer_.lowerDo(stmt); }

    void visit(const ForStmt &stmt) override { lowerer_.lowerFor(stmt); }

    void visit(const NextStmt &stmt) override { lowerer_.lowerNext(stmt); }

    void visit(const ExitStmt &stmt) override { lowerer_.lowerExit(stmt); }

    void visit(const GotoStmt &stmt) override { lowerer_.lowerGoto(stmt); }

    void visit(const EndStmt &stmt) override { lowerer_.lowerEnd(stmt); }

    void visit(const InputStmt &stmt) override { lowerer_.lowerInput(stmt); }

    void visit(const ReturnStmt &stmt) override { lowerer_.lowerReturn(stmt); }

    void visit(const FunctionDecl &) override {}

    void visit(const SubDecl &) override {}

    void visit(const StmtList &stmt) override { lowerer_.lowerStmtList(stmt); }

  private:
    Lowerer &lowerer_;
};

/// @brief Lower a BASIC statement subtree into IL form.
/// @param stmt AST statement to lower.
/// @details Dispatches on the dynamic statement type and forwards to the
///          specialized helpers. The entry point updates @ref curLoc before
///          delegating so emitted instructions and diagnostics reference the
///          statement's source location. Helpers invoked from here may mutate
///          @ref cur when they split control flow; once a RETURN emits a
///          terminator the routine relies on @ref BasicBlock::terminated to
///          avoid lowering subsequent statements in the block.
void Lowerer::lowerStmt(const Stmt &stmt)
{
    curLoc = stmt.loc;
    LowererStmtVisitor visitor(*this);
    stmt.accept(visitor);
}

/// @brief Lower each statement within a statement list sequentially.
/// @param stmt StmtList aggregating multiple statements on one line.
/// @details Invokes @ref lowerStmt for every child while respecting
///          terminators emitted by earlier statements.
void Lowerer::lowerStmtList(const StmtList &stmt)
{
    for (const auto &child : stmt.stmts)
    {
        if (!child)
            continue;
        BasicBlock *current = context().current();
        if (current && current->terminated)
            break;
        lowerStmt(*child);
    }
}

/// @brief Lower a RETURN statement optionally yielding a value.
/// @param stmt RETURN statement describing the result expression.
/// @details Lowers the optional return value and emits the corresponding IL
///          return terminator, mirroring the legacy dispatch logic.
void Lowerer::lowerReturn(const ReturnStmt &stmt)
{
    if (stmt.value)
    {
        RVal v = lowerExpr(*stmt.value);
        emitRet(v.value);
    }
    else
    {
        emitRetVoid();
    }
}

/// @brief Lower an assignment or array store.
/// @param stmt Assignment statement describing the destination and source.
/// @details Evaluates the right-hand side, performs BASIC-to-IL conversions
///          (boolean extension, integer/floating conversions), and writes into
///          either a scalar slot or computed array address. The routine keeps
///          @ref cur unchanged but stamps each emitted instruction with
///          @ref curLoc so downstream diagnostics and runtime traps report the
///          correct location.
void Lowerer::lowerLet(const LetStmt &stmt)
{
    RVal v = lowerExpr(*stmt.expr);
    if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
    {
        const auto *info = findSymbol(var->name);
        assert(info && info->slotId);
        SlotType slotInfo = getSlotType(var->name);
        Type targetTy = slotInfo.type;
        bool isArray = slotInfo.isArray;
        bool isStr = targetTy.kind == Type::Kind::Str;
        bool isF64 = targetTy.kind == Type::Kind::F64;
        bool isBool = slotInfo.isBoolean;
        if (!isArray)
        {
            if (!isStr && !isF64 && !isBool && v.type.kind == Type::Kind::I1)
            {
                v = coerceToI64(std::move(v), stmt.loc);
            }
            if (isF64 && v.type.kind == Type::Kind::I64)
            {
                v = coerceToF64(std::move(v), stmt.loc);
            }
            else if (!isStr && !isF64 && !isBool && v.type.kind == Type::Kind::F64)
            {
                v = coerceToI64(std::move(v), stmt.loc);
            }
        }
        Value slot = Value::temp(*info->slotId);
        curLoc = stmt.loc;
        if (isArray)
            storeArray(slot, v.value);
        else
            emitStore(targetTy, slot, v.value);
    }
    else if (auto *arr = dynamic_cast<const ArrayExpr *>(stmt.target.get()))
    {
        if (v.type.kind == Type::Kind::I1)
        {
            v = coerceToI64(std::move(v), stmt.loc);
        }
        ArrayAccess access = lowerArrayAccess(*arr);
        curLoc = stmt.loc;
        emitCall("rt_arr_i32_set", {access.base, access.index, v.value});
    }
}

/// @brief Lower a PRINT statement into runtime calls.
/// @param stmt PRINT statement describing expression/items.
/// @details Iterates over the queued print items, converting boolean results
///          to integers and selecting the runtime shim (@c rt_print_str,
///          @c rt_print_i64, or @c rt_print_f64). Commas and semicolons are
///          translated into spacing/newline control, and a trailing semicolon
///          suppresses the newline emission. The procedure does not mutate
///          @ref cur but refreshes @ref curLoc for every runtime call to
///          propagate accurate diagnostic locations.
void Lowerer::lowerPrint(const PrintStmt &stmt)
{
    for (const auto &it : stmt.items)
    {
        switch (it.kind)
        {
            case PrintItem::Kind::Expr:
            {
                RVal v = lowerExpr(*it.expr);
                if (v.type.kind == Type::Kind::I1 || v.type.kind == Type::Kind::I16 ||
                    v.type.kind == Type::Kind::I32)
                {
                    v = coerceToI64(std::move(v), stmt.loc);
                }
                curLoc = stmt.loc;
                if (v.type.kind == Type::Kind::Str)
                    emitCall("rt_print_str", {v.value});
                else if (v.type.kind == Type::Kind::F64)
                    emitCall("rt_print_f64", {v.value});
                else
                    emitCall("rt_print_i64", {v.value});
                break;
            }
            case PrintItem::Kind::Comma:
            {
                std::string spaceLbl = getStringLabel(" ");
                Value sp = emitConstStr(spaceLbl);
                curLoc = stmt.loc;
                emitCall("rt_print_str", {sp});
                break;
            }
            case PrintItem::Kind::Semicolon:
                break;
        }
    }

    bool suppress_nl = !stmt.items.empty() && stmt.items.back().kind == PrintItem::Kind::Semicolon;
    if (!suppress_nl)
    {
        std::string nlLbl = getStringLabel("\n");
        Value nl = emitConstStr(nlLbl);
        curLoc = stmt.loc;
        emitCall("rt_print_str", {nl});
    }
}

/// @brief Reserve the block skeleton for an IF/ELSE ladder.
/// @param conds Number of conditions (primary IF plus ELSE IF arms).
/// @return Index bundle describing the inserted test, then, else, and exit
///         blocks.
/// @details Extends the current function with deterministically named blocks
///          using @ref blockNamer or the @ref NameMangler fallback. The helper
///          temporarily records the current block index and restores @ref cur
///          once the new blocks are appended.
Lowerer::IfBlocks Lowerer::emitIfBlocks(size_t conds)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && ctx.current());
    BlockNamer *blockNamer = ctx.blockNamer();
    size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
    size_t start = func->blocks.size();
    unsigned firstId = 0;
    for (size_t i = 0; i < conds; ++i)
    {
        unsigned id = blockNamer ? blockNamer->nextIf() : static_cast<unsigned>(i);
        if (i == 0)
            firstId = id;
        std::string testLbl = blockNamer ? blockNamer->generic("if_test")
                                         : mangler.block("if_test_" + std::to_string(i));
        std::string thenLbl = blockNamer ? blockNamer->ifThen(id)
                                         : mangler.block("if_then_" + std::to_string(i));
        builder->addBlock(*func, testLbl);
        builder->addBlock(*func, thenLbl);
    }
    std::string elseLbl = blockNamer ? blockNamer->ifElse(firstId) : mangler.block("if_else");
    std::string endLbl = blockNamer ? blockNamer->ifEnd(firstId) : mangler.block("if_exit");
    builder->addBlock(*func, elseLbl);
    builder->addBlock(*func, endLbl);
    ctx.setCurrent(&func->blocks[curIdx]);
    std::vector<size_t> testIdx(conds);
    std::vector<size_t> thenIdx(conds);
    for (size_t i = 0; i < conds; ++i)
    {
        testIdx[i] = start + 2 * i;
        thenIdx[i] = start + 2 * i + 1;
    }
    BasicBlock *elseBlk = &func->blocks[start + 2 * conds];
    BasicBlock *exitBlk = &func->blocks[start + 2 * conds + 1];
    return {std::move(testIdx), std::move(thenIdx), elseBlk, exitBlk};
}

/// @brief Lower the conditional branch used by an IF arm.
/// @param cond Expression providing the truth value.
/// @param testBlk Block that evaluates the condition.
/// @param thenBlk Destination when the condition is true.
/// @param falseBlk Destination when the condition is false.
/// @param loc Source location for diagnostics.
/// @details Moves @ref cur to @p testBlk, converts the expression to an I1 if
///          necessary, and emits a conditional branch that targets @p thenBlk
///          or @p falseBlk. @ref curLoc is refreshed so diagnostics generated by
///          failed conversions or runtime checks report @p loc.
void Lowerer::lowerIfCondition(const Expr &cond,
                               BasicBlock *testBlk,
                               BasicBlock *thenBlk,
                               BasicBlock *falseBlk,
                               il::support::SourceLoc loc)
{
    context().setCurrent(testBlk);
    RVal c = lowerExpr(cond);
    Value condVal = coerceToBool(std::move(c), loc).value;
    emitCBr(condVal, thenBlk, falseBlk);
}

/// @brief Lower the body of a single IF or ELSE branch.
/// @param stmt Statement executed when the branch is taken (may be null).
/// @param thenBlk Block that holds the branch body.
/// @param exitBlk Merge block for fall-through control flow.
/// @param loc Source location for diagnostics.
/// @return @c true when the branch falls through to @p exitBlk.
/// @details Positions @ref cur at @p thenBlk, lowers the branch body if
///          present, and emits an explicit jump to @p exitBlk when the lowered
///          code left the block unterminated. The helper sets @ref curLoc before
///          emitting the merge branch, ensuring diagnostics attribute to
///          @p loc.
bool Lowerer::lowerIfBranch(const Stmt *stmt,
                            BasicBlock *thenBlk,
                            BasicBlock *exitBlk,
                            il::support::SourceLoc loc)
{
    context().setCurrent(thenBlk);
    if (stmt)
        lowerStmt(*stmt);
    BasicBlock *current = context().current();
    if (current && !current->terminated)
    {
        curLoc = loc;
        emitBr(exitBlk);
        return true;
    }
    return false;
}

/// @brief Lower an IF/ELSEIF/ELSE cascade.
/// @param stmt IF statement containing branches and optional else.
/// @details Allocates the block layout via @ref emitIfBlocks, sequentially
///          lowers each condition and branch, and merges control flow into the
///          shared exit block when at least one branch falls through. The
///          routine mutates @ref cur as it walks through the generated blocks
///          and keeps @ref curLoc aligned with the IF source span for emitted
///          diagnostics.
void Lowerer::lowerIf(const IfStmt &stmt)
{
    size_t conds = 1 + stmt.elseifs.size();
    IfBlocks blocks = emitIfBlocks(conds);
    std::vector<const Expr *> condExprs;
    std::vector<const Stmt *> thenStmts;
    condExprs.push_back(stmt.cond.get());
    thenStmts.push_back(stmt.then_branch.get());
    for (const auto &e : stmt.elseifs)
    {
        condExprs.push_back(e.cond.get());
        thenStmts.push_back(e.then_branch.get());
    }

    curLoc = stmt.loc;
    Function *func = context().function();
    assert(func && "lowerIf requires an active function");
    emitBr(&func->blocks[blocks.tests[0]]);

    bool fallthrough = false;
    for (size_t i = 0; i < conds; ++i)
    {
        BasicBlock *testBlk = &func->blocks[blocks.tests[i]];
        BasicBlock *thenBlk = &func->blocks[blocks.thens[i]];
        BasicBlock *falseBlk =
            (i + 1 < conds) ? &func->blocks[blocks.tests[i + 1]] : blocks.elseBlk;
        lowerIfCondition(*condExprs[i], testBlk, thenBlk, falseBlk, stmt.loc);
        bool branchFall = lowerIfBranch(thenStmts[i], thenBlk, blocks.exitBlk, stmt.loc);
        fallthrough = fallthrough || branchFall;
    }

    bool elseFall = lowerIfBranch(stmt.else_branch.get(), blocks.elseBlk, blocks.exitBlk, stmt.loc);
    fallthrough = fallthrough || elseFall;

    if (!fallthrough)
    {
        func->blocks.pop_back();
        context().setCurrent(blocks.elseBlk);
        return;
    }

    context().setCurrent(blocks.exitBlk);
}

/// @brief Lower statements forming a loop body until a terminator is hit.
/// @param body Sequence of statements comprising the loop body.
/// @details Iterates @p body and delegates to @ref lowerStmt while respecting
///          the current block's termination state. When @ref cur becomes null
///          or terminated the helper stops lowering additional statements.
void Lowerer::lowerLoopBody(const std::vector<StmtPtr> &body)
{
    for (const auto &stmt : body)
    {
        if (!stmt)
            continue;
        lowerStmt(*stmt);
        BasicBlock *current = context().current();
        if (!current || current->terminated)
            break;
    }
}

/// @brief Lower a WHILE loop into head/body/done blocks.
/// @param stmt WHILE statement describing the loop structure.
/// @details Adds head/body/done blocks, branches into the head, and enforces an
///          I1 condition before branching to the loop body or exit. The body
///          reuses @ref lowerStmt for nested statements and, when it does not
///          terminate, jumps back to the head to re-evaluate the condition. The
///          method mutates @ref cur as it traverses head, body, and done blocks
///          and refreshes @ref curLoc for diagnostics tied to loop locations.
void Lowerer::lowerWhile(const WhileStmt &stmt)
{
    // Adding blocks may reallocate the function's block list; capture index and
    // reacquire pointers to guarantee stability.
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "lowerWhile requires an active function");
    BlockNamer *blockNamer = ctx.blockNamer();
    size_t start = func->blocks.size();
    unsigned id = blockNamer ? blockNamer->nextWhile() : 0;
    std::string headLbl = blockNamer ? blockNamer->whileHead(id) : mangler.block("loop_head");
    std::string bodyLbl = blockNamer ? blockNamer->whileBody(id) : mangler.block("loop_body");
    std::string doneLbl = blockNamer ? blockNamer->whileEnd(id) : mangler.block("done");
    builder->addBlock(*func, headLbl);
    builder->addBlock(*func, bodyLbl);
    builder->addBlock(*func, doneLbl);
    BasicBlock *head = &func->blocks[start];
    BasicBlock *body = &func->blocks[start + 1];
    size_t doneIdx = start + 2;
    BasicBlock *done = &func->blocks[doneIdx];

    ctx.pushLoopExit(done);

    curLoc = stmt.loc;
    emitBr(head);

    // head
    ctx.setCurrent(head);
    RVal cond = lowerExpr(*stmt.cond);
    cond = coerceToBool(std::move(cond), stmt.loc);
    curLoc = stmt.loc;
    emitCBr(cond.value, body, done);

    // body
    ctx.setCurrent(body);
    lowerLoopBody(stmt.body);
    BasicBlock *current = ctx.current();
    bool exitTaken = ctx.loopExitTaken();
    bool term = current && current->terminated;
    if (!term)
    {
        curLoc = stmt.loc;
        emitBr(head);
    }

    done = &func->blocks[doneIdx];
    ctx.refreshLoopExitTarget(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.popLoopExit();
}

void Lowerer::lowerDo(const DoStmt &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "lowerDo requires an active function");
    BlockNamer *blockNamer = ctx.blockNamer();
    size_t start = func->blocks.size();
    unsigned id = blockNamer ? blockNamer->nextDo() : 0;
    std::string headLbl = blockNamer ? blockNamer->doHead(id) : mangler.block("do_head");
    std::string bodyLbl = blockNamer ? blockNamer->doBody(id) : mangler.block("do_body");
    std::string doneLbl = blockNamer ? blockNamer->doEnd(id) : mangler.block("do_done");
    builder->addBlock(*func, headLbl);
    builder->addBlock(*func, bodyLbl);
    builder->addBlock(*func, doneLbl);
    size_t headIdx = start;
    size_t bodyIdx = start + 1;
    size_t doneIdx = start + 2;
    BasicBlock *done = &func->blocks[doneIdx];

    ctx.pushLoopExit(done);

    auto emitConditionBranch = [&](const RVal &condVal) {
        BasicBlock *body = &func->blocks[bodyIdx];
        BasicBlock *doneBlk = &func->blocks[doneIdx];
        if (stmt.condKind == DoStmt::CondKind::While)
        {
            curLoc = stmt.loc;
            emitCBr(condVal.value, body, doneBlk);
        }
        else
        {
            curLoc = stmt.loc;
            emitCBr(condVal.value, doneBlk, body);
        }
    };

    auto emitHead = [&]() {
        func->blocks[headIdx].label = headLbl;
        func->blocks[bodyIdx].label = bodyLbl;
        BasicBlock *head = &func->blocks[headIdx];
        BasicBlock *body = &func->blocks[bodyIdx];
        ctx.setCurrent(head);
        curLoc = stmt.loc;
        if (stmt.condKind == DoStmt::CondKind::None)
        {
            emitBr(body);
            return;
        }
        assert(stmt.cond && "DO loop missing condition for conditional form");
        RVal cond = lowerExpr(*stmt.cond);
        cond = coerceToBool(std::move(cond), stmt.loc);
        emitConditionBranch(cond);
    };

    switch (stmt.testPos)
    {
        case DoStmt::TestPos::Pre:
        {
            curLoc = stmt.loc;
            func->blocks[headIdx].label = headLbl;
            emitBr(&func->blocks[headIdx]);
            emitHead();
            ctx.setCurrent(&func->blocks[bodyIdx]);
            break;
        }
        case DoStmt::TestPos::Post:
        {
            curLoc = stmt.loc;
            func->blocks[bodyIdx].label = bodyLbl;
            emitBr(&func->blocks[bodyIdx]);
            ctx.setCurrent(&func->blocks[bodyIdx]);
            break;
        }
    }

    lowerLoopBody(stmt.body);
    BasicBlock *current = ctx.current();
    bool exitTaken = ctx.loopExitTaken();
    bool term = current && current->terminated;

    if (!term)
    {
        curLoc = stmt.loc;
        func->blocks[headIdx].label = headLbl;
        emitBr(&func->blocks[headIdx]);
    }

    if (stmt.testPos == DoStmt::TestPos::Post)
    {
        emitHead();
    }

    func->blocks[doneIdx].label = doneLbl;
    done = &func->blocks[doneIdx];
    ctx.refreshLoopExitTarget(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.popLoopExit();
}

/// @brief Create the block layout shared by FOR loops.
/// @param varStep Whether the loop has a variable (runtime) step expression.
/// @return Descriptor pointing to the inserted head/body/inc/done blocks.
/// @details Appends the necessary blocks to @ref func using deterministic names
///          and restores @ref cur to the block active before allocation. When
///          @p varStep is @c true the helper adds both positive and negative
///          heads so the loop can branch based on the computed step sign.
Lowerer::ForBlocks Lowerer::setupForBlocks(bool varStep)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && ctx.current());
    BlockNamer *blockNamer = ctx.blockNamer();
    size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
    size_t base = func->blocks.size();
    unsigned id = blockNamer ? blockNamer->nextFor() : 0;
    ForBlocks fb;
    if (varStep)
    {
        std::string headPosLbl =
            blockNamer ? blockNamer->generic("for_head_pos") : mangler.block("for_head_pos");
        std::string headNegLbl =
            blockNamer ? blockNamer->generic("for_head_neg") : mangler.block("for_head_neg");
        builder->addBlock(*func, headPosLbl);
        builder->addBlock(*func, headNegLbl);
        fb.headPosIdx = base;
        fb.headNegIdx = base + 1;
        base += 2;
    }
    else
    {
        std::string headLbl = blockNamer ? blockNamer->forHead(id) : mangler.block("for_head");
        builder->addBlock(*func, headLbl);
        fb.headIdx = base;
        base += 1;
    }
    std::string bodyLbl = blockNamer ? blockNamer->forBody(id) : mangler.block("for_body");
    std::string incLbl = blockNamer ? blockNamer->forInc(id) : mangler.block("for_inc");
    std::string doneLbl = blockNamer ? blockNamer->forEnd(id) : mangler.block("for_done");
    builder->addBlock(*func, bodyLbl);
    builder->addBlock(*func, incLbl);
    builder->addBlock(*func, doneLbl);
    fb.bodyIdx = base;
    fb.incIdx = base + 1;
    fb.doneIdx = base + 2;
    ctx.setCurrent(&func->blocks[curIdx]);
    return fb;
}

/// @brief Lower a FOR loop whose STEP is a compile-time constant.
/// @param stmt Source FOR statement.
/// @param slot Storage slot for the induction variable.
/// @param end Evaluated end expression.
/// @param step Evaluated step expression.
/// @param stepConst Constant integer value of @p step.
/// @details Builds the canonical head/body/inc/done blocks and compares the
///          induction variable against the end bound using @c SCmpLE or
///          @c SCmpGE depending on the sign of @p stepConst. When the body does
///          not terminate it advances the induction variable via
///          @ref emitForStep and loops back to the head. The helper mutates
///          @ref cur as control moves across the loop blocks and tags emitted
///          instructions with @ref curLoc for diagnostics.
void Lowerer::lowerForConstStep(
    const ForStmt &stmt, Value slot, RVal end, RVal step, int64_t stepConst)
{
    ForBlocks fb = setupForBlocks(false);
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "lowerForConstStep requires an active function");
    size_t doneIdx = fb.doneIdx;
    BasicBlock *done = &func->blocks[doneIdx];
    ctx.pushLoopExit(done);
    curLoc = stmt.loc;
    emitBr(&func->blocks[fb.headIdx]);
    ctx.setCurrent(&func->blocks[fb.headIdx]);
    curLoc = stmt.loc;
    Value curVal = emitLoad(Type(Type::Kind::I64), slot);
    Opcode cmp = stepConst >= 0 ? Opcode::SCmpLE : Opcode::SCmpGE;
    curLoc = stmt.loc;
    Value cond = emitBinary(cmp, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cond, &func->blocks[fb.bodyIdx], &func->blocks[fb.doneIdx]);
    ctx.setCurrent(&func->blocks[fb.bodyIdx]);
    lowerLoopBody(stmt.body);
    BasicBlock *current = ctx.current();
    bool exitTaken = ctx.loopExitTaken();
    bool term = current && current->terminated;
    if (!term)
    {
        curLoc = stmt.loc;
        emitBr(&func->blocks[fb.incIdx]);
        ctx.setCurrent(&func->blocks[fb.incIdx]);
        curLoc = stmt.loc;
        emitForStep(slot, step.value);
        curLoc = stmt.loc;
        emitBr(&func->blocks[fb.headIdx]);
    }
    done = &func->blocks[doneIdx];
    ctx.refreshLoopExitTarget(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.popLoopExit();
}

/// @brief Lower a FOR loop whose STEP expression is evaluated at runtime.
/// @param stmt Source FOR statement.
/// @param slot Storage slot for the induction variable.
/// @param end Evaluated end expression.
/// @param step Evaluated step expression.
/// @details Computes the step sign, emits a branch to either the non-negative or
///          negative comparison head, and shares a single body/inc/done chain.
///          The method mutates @ref cur as it traverses these blocks and
///          updates @ref curLoc before each comparison and branch to preserve
///          accurate diagnostics.
void Lowerer::lowerForVarStep(const ForStmt &stmt, Value slot, RVal end, RVal step)
{
    curLoc = stmt.loc;
    Value stepNonNeg =
        emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), step.value, Value::constInt(0));
    ForBlocks fb = setupForBlocks(true);
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "lowerForVarStep requires an active function");
    size_t doneIdx = fb.doneIdx;
    BasicBlock *done = &func->blocks[doneIdx];
    ctx.pushLoopExit(done);
    curLoc = stmt.loc;
    emitCBr(stepNonNeg, &func->blocks[fb.headPosIdx], &func->blocks[fb.headNegIdx]);
    ctx.setCurrent(&func->blocks[fb.headPosIdx]);
    curLoc = stmt.loc;
    Value curVal = emitLoad(Type(Type::Kind::I64), slot);
    curLoc = stmt.loc;
    Value cmpPos = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cmpPos, &func->blocks[fb.bodyIdx], &func->blocks[fb.doneIdx]);
    ctx.setCurrent(&func->blocks[fb.headNegIdx]);
    curLoc = stmt.loc;
    curVal = emitLoad(Type(Type::Kind::I64), slot);
    curLoc = stmt.loc;
    Value cmpNeg = emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cmpNeg, &func->blocks[fb.bodyIdx], &func->blocks[fb.doneIdx]);
    ctx.setCurrent(&func->blocks[fb.bodyIdx]);
    lowerLoopBody(stmt.body);
    BasicBlock *current = ctx.current();
    bool exitTaken = ctx.loopExitTaken();
    bool term = current && current->terminated;
    if (!term)
    {
        curLoc = stmt.loc;
        emitBr(&func->blocks[fb.incIdx]);
        ctx.setCurrent(&func->blocks[fb.incIdx]);
        curLoc = stmt.loc;
        emitForStep(slot, step.value);
        curLoc = stmt.loc;
        emitCBr(stepNonNeg, &func->blocks[fb.headPosIdx], &func->blocks[fb.headNegIdx]);
    }
    done = &func->blocks[doneIdx];
    ctx.refreshLoopExitTarget(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.popLoopExit();
}

/// @brief Lower a BASIC FOR statement.
/// @param stmt Parsed FOR statement containing bounds and optional step.
/// @details Evaluates the start/end/step expressions, stores the initial value
///          into the induction slot, and dispatches to either the constant-step
///          or variable-step lowering path. The helper updates @ref curLoc for
///          each emitted instruction and leaves @ref cur at the block chosen by
///          the delegated lowering routine.
void Lowerer::lowerFor(const ForStmt &stmt)
{
    RVal start = lowerExpr(*stmt.start);
    RVal end = lowerExpr(*stmt.end);
    RVal step = stmt.step ? lowerExpr(*stmt.step) : RVal{Value::constInt(1), Type(Type::Kind::I64)};
    const auto *info = findSymbol(stmt.var);
    assert(info && info->slotId);
    Value slot = Value::temp(*info->slotId);
    curLoc = stmt.loc;
    emitStore(Type(Type::Kind::I64), slot, start.value);

    bool constStep = !stmt.step || dynamic_cast<const IntExpr *>(stmt.step.get());
    int64_t stepConst = 1;
    if (constStep && stmt.step)
    {
        if (auto *ie = dynamic_cast<const IntExpr *>(stmt.step.get()))
            stepConst = ie->value;
    }
    if (constStep)
        lowerForConstStep(stmt, slot, end, step, stepConst);
    else
        lowerForVarStep(stmt, slot, end, step);
}

/// @brief Handle a NEXT marker.
/// @param next NEXT statement (ignored).
/// @details The lowering pipeline already encodes loop back-edges inside FOR
///          lowering, so NEXT does not emit IL and leaves @ref cur untouched.
void Lowerer::lowerNext(const NextStmt &next)
{
    (void)next;
}

void Lowerer::lowerExit(const ExitStmt &stmt)
{
    ProcedureContext &ctx = context();
    BasicBlock *target = ctx.currentLoopExit();
    curLoc = stmt.loc;
    if (!target)
    {
        emitTrap();
        return;
    }
    emitBr(target);
    ctx.markLoopExitTaken();
}

/// @brief Lower a GOTO jump.
/// @param stmt GOTO statement naming a BASIC line label.
/// @details Looks up the destination basic block recorded during statement
///          discovery and emits an unconditional branch. @ref curLoc is set so
///          diagnostics reference the jump site, and the resulting branch marks
///          the current block as terminated.
void Lowerer::lowerGoto(const GotoStmt &stmt)
{
    auto &lineBlocks = context().lineBlocks();
    auto it = lineBlocks.find(stmt.target);
    if (it != lineBlocks.end())
    {
        curLoc = stmt.loc;
        Function *func = context().function();
        assert(func && "lowerGoto requires an active function");
        emitBr(&func->blocks[it->second]);
    }
}

/// @brief Lower an END statement.
/// @param stmt END statement closing the program.
/// @details Emits a branch to the function exit block recorded in @ref fnExit.
///          The branch uses @ref curLoc for diagnostics and leaves the current
///          block terminated.
void Lowerer::lowerEnd(const EndStmt &stmt)
{
    curLoc = stmt.loc;
    Function *func = context().function();
    assert(func && "lowerEnd requires an active function");
    emitBr(&func->blocks[context().exitIndex()]);
}

/// @brief Lower an INPUT statement.
/// @param stmt INPUT statement providing optional prompt and destination.
/// @details Optionally prints the prompt via @c rt_print_str, then calls the
///          runtime @c rt_input_line helper to obtain a string. Numeric targets
///          convert the string with @c rt_to_int before storing. The routine
///          does not mutate @ref cur but refreshes @ref curLoc prior to each
///          runtime interaction for diagnostics.
void Lowerer::lowerInput(const InputStmt &stmt)
{
    curLoc = stmt.loc;
    if (stmt.prompt)
    {
        if (auto *se = dynamic_cast<const StringExpr *>(stmt.prompt.get()))
        {
            std::string lbl = getStringLabel(se->value);
            Value v = emitConstStr(lbl);
            emitCall("rt_print_str", {v});
        }
    }
    Value s = emitCallRet(Type(Type::Kind::Str), "rt_input_line", {});
    SlotType slotInfo = getSlotType(stmt.var);
    const auto *info = findSymbol(stmt.var);
    assert(info && info->slotId);
    Value target = Value::temp(*info->slotId);
    if (slotInfo.type.kind == Type::Kind::Str)
    {
        emitStore(Type(Type::Kind::Str), target, s);
    }
    else
    {
        Value n = emitCallRet(Type(Type::Kind::I64), "rt_to_int", {s});
        if (slotInfo.isBoolean)
        {
            Value b = coerceToBool({n, Type(Type::Kind::I64)}, stmt.loc).value;
            curLoc = stmt.loc;
            emitStore(ilBoolTy(), target, b);
        }
        else if (slotInfo.type.kind == Type::Kind::F64)
        {
            Value f = coerceToF64({n, Type(Type::Kind::I64)}, stmt.loc).value;
            curLoc = stmt.loc;
            emitStore(Type(Type::Kind::F64), target, f);
        }
        else
        {
            curLoc = stmt.loc;
            emitStore(Type(Type::Kind::I64), target, n);
        }
    }
}

/// @brief Lower a DIM array allocation.
/// @param stmt DIM statement describing the array name and size.
/// @details Evaluates the requested element count, allocates backing storage
///          via @c rt_arr_i32_new, and stores the resulting handle into the
///          array slot. When @ref boundsChecks is enabled it also records the
///          logical length so runtime bounds checks can read it. Instructions
///          are tagged with @ref curLoc for diagnostics; @ref cur itself
///          remains unchanged.
void Lowerer::lowerDim(const DimStmt &stmt)
{
    RVal sz = lowerExpr(*stmt.size);
    curLoc = stmt.loc;
    Value handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_i32_new", {sz.value});
    const auto *info = findSymbol(stmt.name);
    assert(info && info->slotId);
    storeArray(Value::temp(*info->slotId), handle);
    if (boundsChecks)
    {
        if (info && info->arrayLengthSlot)
            emitStore(Type(Type::Kind::I64), Value::temp(*info->arrayLengthSlot), sz.value);
    }
}

/// @brief Lower a REDIM array reallocation.
/// @param stmt REDIM statement describing the new size.
/// @details Re-evaluates the target length, invokes @c rt_arr_i32_resize to
///          adjust the array storage, and stores the returned handle into the
///          tracked array slot, mirroring DIM lowering semantics.
void Lowerer::lowerReDim(const ReDimStmt &stmt)
{
    RVal sz = lowerExpr(*stmt.size);
    curLoc = stmt.loc;
    const auto *info = findSymbol(stmt.name);
    assert(info && info->slotId);
    Value slot = Value::temp(*info->slotId);
    Value current = emitLoad(Type(Type::Kind::Ptr), slot);
    Value resized = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_i32_resize", {current, sz.value});
    storeArray(slot, resized);
    if (boundsChecks && info && info->arrayLengthSlot)
        emitStore(Type(Type::Kind::I64), Value::temp(*info->arrayLengthSlot), sz.value);
}

/// @brief Lower a RANDOMIZE seed update.
/// @param stmt RANDOMIZE statement carrying the seed expression.
/// @details Converts the seed expression to a 64-bit integer when necessary and
///          invokes the runtime @c rt_randomize_i64 helper. @ref curLoc is
///          updated to associate diagnostics with the statement while leaving
///          @ref cur unchanged.
void Lowerer::lowerRandomize(const RandomizeStmt &stmt)
{
    RVal s = lowerExpr(*stmt.seed);
    Value seed = coerceToI64(std::move(s), stmt.loc).value;
    curLoc = stmt.loc;
    emitCall("rt_randomize_i64", {seed});
}

} // namespace il::frontends::basic

