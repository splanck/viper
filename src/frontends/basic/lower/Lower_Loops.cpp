//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements loop lowering helpers for BASIC WHILE, DO, and FOR forms.
/// @details Shared routines allocate deterministic head/body/done blocks,
/// establish loop-state bookkeeping, and ensure terminators are emitted with the
/// correct diagnostics context. Each helper preserves the active Lowerer state so
/// nested statements observe consistent control-flow graphs.

#include "frontends/basic/Lowerer.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

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

Lowerer::CtrlState Lowerer::emitWhile(const WhileStmt &stmt)
{
    CtrlState state{};
    auto &ctx = context();
    auto *func = ctx.function();
    auto *current = ctx.current();
    if (!func || !current)
        return state;

    BlockNamer *blockNamer = ctx.blockNames().namer();
    size_t start = func->blocks.size();
    unsigned id = blockNamer ? blockNamer->nextWhile() : 0;
    std::string headLbl = blockNamer ? blockNamer->whileHead(id) : mangler.block("loop_head");
    std::string bodyLbl = blockNamer ? blockNamer->whileBody(id) : mangler.block("loop_body");
    std::string doneLbl = blockNamer ? blockNamer->whileEnd(id) : mangler.block("done");
    builder->addBlock(*func, headLbl);
    builder->addBlock(*func, bodyLbl);
    builder->addBlock(*func, doneLbl);

    func = ctx.function();
    size_t headIdx = start;
    size_t bodyIdx = start + 1;
    size_t doneIdx = start + 2;
    auto *head = &func->blocks[headIdx];
    auto *body = &func->blocks[bodyIdx];
    auto *done = &func->blocks[doneIdx];
    state.after = done;
    ctx.loopState().push(done);
    curLoc = stmt.loc;
    emitBr(head);
    func = ctx.function();
    head = &func->blocks[headIdx];
    body = &func->blocks[bodyIdx];
    done = &func->blocks[doneIdx];
    ctx.setCurrent(head);
    curLoc = stmt.loc;
    lowerCondBranch(*stmt.cond, body, done, stmt.loc);

    func = ctx.function();
    body = &func->blocks[bodyIdx];
    done = &func->blocks[doneIdx];
    ctx.setCurrent(body);
    lowerLoopBody(stmt.body);
    auto *bodyCur = ctx.current();
    bool exitTaken = ctx.loopState().taken();
    bool term = bodyCur && bodyCur->terminated;
    if (!term)
    {
        func = ctx.function();
        head = &func->blocks[headIdx];
        curLoc = stmt.loc;
        emitBr(head);
    }

    func = ctx.function();
    done = &func->blocks[doneIdx];
    ctx.loopState().refresh(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.loopState().pop();

    state.cur = ctx.current();
    state.after = state.cur;
    state.fallthrough = !done->terminated;
    return state;
}
void Lowerer::lowerWhile(const WhileStmt &stmt)
{
    CtrlState state = emitWhile(stmt);
    if (state.cur)
        context().setCurrent(state.cur);
}
Lowerer::CtrlState Lowerer::emitDo(const DoStmt &stmt)
{
    CtrlState state{};
    auto &ctx = context();
    auto *func = ctx.function();
    auto *current = ctx.current();
    if (!func || !current)
        return state;

    const size_t currentIdx = static_cast<size_t>(current - &func->blocks.front());

    BlockNamer *blockNamer = ctx.blockNames().namer();
    size_t start = func->blocks.size();
    unsigned id = blockNamer ? blockNamer->nextDo() : 0;
    std::string headLbl = blockNamer ? blockNamer->doHead(id) : mangler.block("do_head");
    std::string bodyLbl = blockNamer ? blockNamer->doBody(id) : mangler.block("do_body");
    std::string doneLbl = blockNamer ? blockNamer->doEnd(id) : mangler.block("do_done");
    builder->addBlock(*func, headLbl);
    builder->addBlock(*func, bodyLbl);
    builder->addBlock(*func, doneLbl);

    func = ctx.function();
    size_t headIdx = start;
    size_t bodyIdx = start + 1;
    size_t doneIdx = start + 2;
    auto *done = &func->blocks[doneIdx];
    state.after = done;
    current = &func->blocks[currentIdx];
    ctx.setCurrent(current);
    ctx.loopState().push(done);
    auto emitHead = [&]() {
        func = ctx.function();
        func->blocks[headIdx].label = headLbl;
        func->blocks[bodyIdx].label = bodyLbl;
        auto *head = &func->blocks[headIdx];
        ctx.setCurrent(head);
        curLoc = stmt.loc;
        if (stmt.condKind == DoStmt::CondKind::None)
        {
            emitBr(&func->blocks[bodyIdx]);
            return;
        }
        assert(stmt.cond && "DO loop missing condition for conditional form");
        auto *body = &func->blocks[bodyIdx];
        auto *doneBlk = &func->blocks[doneIdx];
        if (stmt.condKind == DoStmt::CondKind::While)
        {
            lowerCondBranch(*stmt.cond, body, doneBlk, stmt.loc);
        }
        else
        {
            lowerCondBranch(*stmt.cond, doneBlk, body, stmt.loc);
        }
    };

    func = ctx.function();
    switch (stmt.testPos)
    {
        case DoStmt::TestPos::Pre:
            curLoc = stmt.loc;
            func->blocks[headIdx].label = headLbl;
            emitBr(&func->blocks[headIdx]);
            emitHead();
            ctx.setCurrent(&func->blocks[bodyIdx]);
            break;
        case DoStmt::TestPos::Post:
            curLoc = stmt.loc;
            func->blocks[bodyIdx].label = bodyLbl;
            emitBr(&func->blocks[bodyIdx]);
            ctx.setCurrent(&func->blocks[bodyIdx]);
            break;
    }

    lowerLoopBody(stmt.body);
    auto *bodyCur = ctx.current();
    bool exitTaken = ctx.loopState().taken();
    bool term = bodyCur && bodyCur->terminated;

    if (!term)
    {
        curLoc = stmt.loc;
        func = ctx.function();
        func->blocks[headIdx].label = headLbl;
        emitBr(&func->blocks[headIdx]);
    }

    if (stmt.testPos == DoStmt::TestPos::Post)
        emitHead();
    func = ctx.function();
    func->blocks[doneIdx].label = doneLbl;
    done = &func->blocks[doneIdx];
    ctx.loopState().refresh(done);
    ctx.setCurrent(done);
    const bool postTest = stmt.testPos == DoStmt::TestPos::Post;
    if (postTest)
    {
        done->terminated = false;
    }
    else
    {
        done->terminated = exitTaken ? false : term;
    }
    ctx.loopState().pop();

    state.cur = ctx.current();
    state.after = state.cur;
    state.fallthrough = postTest ? true : !done->terminated;
    return state;
}
void Lowerer::lowerDo(const DoStmt &stmt)
{
    CtrlState state = emitDo(stmt);
    if (state.cur)
        context().setCurrent(state.cur);
}

Lowerer::ForBlocks Lowerer::setupForBlocks(bool varStep)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && ctx.current());
    BlockNamer *blockNamer = ctx.blockNames().namer();
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

void Lowerer::lowerForConstStep(
    const ForStmt &stmt, Value slot, RVal end, RVal step, int64_t stepConst)
{
    ForBlocks fb = setupForBlocks(false);
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "lowerForConstStep requires an active function");
    size_t doneIdx = fb.doneIdx;
    BasicBlock *done = &func->blocks[doneIdx];
    ctx.loopState().push(done);
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
    bool exitTaken = ctx.loopState().taken();
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
    ctx.loopState().refresh(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.loopState().pop();
}

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
    ctx.loopState().push(done);
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
    bool exitTaken = ctx.loopState().taken();
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
    ctx.loopState().refresh(done);
    ctx.setCurrent(done);
    done->terminated = exitTaken ? false : term;
    ctx.loopState().pop();
}

Lowerer::CtrlState Lowerer::emitFor(const ForStmt &stmt, Value slot, RVal end, RVal step)
{
    CtrlState state{};
    lowerForVarStep(stmt, slot, end, step);
    state.cur = context().current();
    state.after = state.cur;
    state.fallthrough = state.cur && !state.cur->terminated;
    return state;
}
void Lowerer::lowerFor(const ForStmt &stmt)
{
    RVal start = lowerScalarExpr(*stmt.start);
    RVal end = lowerScalarExpr(*stmt.end);
    RVal step = stmt.step ? lowerScalarExpr(*stmt.step)
                          : RVal{Value::constInt(1), Type(Type::Kind::I64)};
    const auto *info = findSymbol(stmt.var);
    assert(info && info->slotId);
    Value slot = Value::temp(*info->slotId);
    curLoc = stmt.loc;
    emitStore(Type(Type::Kind::I64), slot, start.value);

    CtrlState state = emitFor(stmt, slot, end, step);
    if (state.cur)
        context().setCurrent(state.cur);
}
void Lowerer::lowerNext(const NextStmt &next)
{
    (void)next;
}

void Lowerer::lowerExit(const ExitStmt &stmt)
{
    ProcedureContext &ctx = context();
    BasicBlock *target = ctx.loopState().current();
    curLoc = stmt.loc;
    if (!target)
    {
        emitTrap();
        return;
    }
    emitBr(target);
    ctx.loopState().markTaken();
}
} // namespace il::frontends::basic
