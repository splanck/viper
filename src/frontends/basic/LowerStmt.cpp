// File: src/frontends/basic/LowerStmt.cpp
// Purpose: Implements BASIC statement lowering routines targeting IL.
// Key invariants: Control-flow block creation remains deterministic via
//                 Lowerer::BlockNamer or NameMangler fallbacks.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

// Purpose: lower stmt.
// Parameters: const Stmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::lowerStmt(const Stmt &stmt)
{
    curLoc = stmt.loc;
    if (auto *lst = dynamic_cast<const StmtList *>(&stmt))
    {
        for (const auto &s : lst->stmts)
        {
            if (cur->terminated)
                break;
            lowerStmt(*s);
        }
    }
    else if (auto *p = dynamic_cast<const PrintStmt *>(&stmt))
        lowerPrint(*p);
    else if (auto *l = dynamic_cast<const LetStmt *>(&stmt))
        lowerLet(*l);
    else if (auto *i = dynamic_cast<const IfStmt *>(&stmt))
        lowerIf(*i);
    else if (auto *w = dynamic_cast<const WhileStmt *>(&stmt))
        lowerWhile(*w);
    else if (auto *f = dynamic_cast<const ForStmt *>(&stmt))
        lowerFor(*f);
    else if (auto *n = dynamic_cast<const NextStmt *>(&stmt))
        lowerNext(*n);
    else if (auto *g = dynamic_cast<const GotoStmt *>(&stmt))
        lowerGoto(*g);
    else if (auto *e = dynamic_cast<const EndStmt *>(&stmt))
        lowerEnd(*e);
    else if (auto *in = dynamic_cast<const InputStmt *>(&stmt))
        lowerInput(*in);
    else if (auto *d = dynamic_cast<const DimStmt *>(&stmt))
    {
        if (d->isArray)
            lowerDim(*d);
    }
    else if (auto *r = dynamic_cast<const RandomizeStmt *>(&stmt))
        lowerRandomize(*r);
    else if (auto *ret = dynamic_cast<const ReturnStmt *>(&stmt))
    {
        if (ret->value)
        {
            RVal v = lowerExpr(*ret->value);
            emitRet(v.value);
        }
        else
        {
            emitRetVoid();
        }
        // Block closed after RETURN; callers should skip further statements.
    }
}

// Purpose: lower let.
// Parameters: const LetStmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::lowerLet(const LetStmt &stmt)
{
    RVal v = lowerExpr(*stmt.expr);
    if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
    {
        auto it = varSlots.find(var->name);
        assert(it != varSlots.end());
        bool isStr = !var->name.empty() && var->name.back() == '$';
        bool isF64 = !var->name.empty() && var->name.back() == '#';
        bool isBool = false;
        auto typeIt = varTypes.find(var->name);
        if (typeIt != varTypes.end() && typeIt->second == AstType::Bool)
            isBool = true;
        if (!isStr && !isF64 && !isBool && v.type.kind == Type::Kind::I1)
        {
            curLoc = stmt.loc;
            Value z = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), v.value);
            v.value = z;
            v.type = Type(Type::Kind::I64);
        }
        if (isF64 && v.type.kind == Type::Kind::I64)
        {
            curLoc = stmt.loc;
            v.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), v.value);
            v.type = Type(Type::Kind::F64);
        }
        else if (!isStr && !isF64 && !isBool && v.type.kind == Type::Kind::F64)
        {
            curLoc = stmt.loc;
            v.value = emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), v.value);
            v.type = Type(Type::Kind::I64);
        }
        curLoc = stmt.loc;
        Type ty = isStr ? Type(Type::Kind::Str)
                         : (isF64 ? Type(Type::Kind::F64)
                                  : (isBool ? ilBoolTy() : Type(Type::Kind::I64)));
        emitStore(ty, Value::temp(it->second), v.value);
    }
    else if (auto *arr = dynamic_cast<const ArrayExpr *>(stmt.target.get()))
    {
        if (v.type.kind == Type::Kind::I1)
        {
            curLoc = stmt.loc;
            Value z = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), v.value);
            v.value = z;
        }
        Value ptr = lowerArrayAddr(*arr);
        curLoc = stmt.loc;
        emitStore(Type(Type::Kind::I64), ptr, v.value);
    }
}

// Purpose: lower print.
// Parameters: const PrintStmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::lowerPrint(const PrintStmt &stmt)
{
    for (const auto &it : stmt.items)
    {
        switch (it.kind)
        {
            case PrintItem::Kind::Expr:
            {
                RVal v = lowerExpr(*it.expr);
                if (v.type.kind == Type::Kind::I1)
                {
                    curLoc = stmt.loc;
                    Value z = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), v.value);
                    v.value = z;
                    v.type = Type(Type::Kind::I64);
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

// Purpose: emit if blocks.
// Parameters: size_t conds.
// Returns: Lowerer::IfBlocks.
// Side effects: may modify lowering state or emit IL. Relies on deterministic block naming via
// BlockNamer.
Lowerer::IfBlocks Lowerer::emitIfBlocks(size_t conds)
{
    size_t curIdx = cur - &func->blocks[0];
    size_t start = func->blocks.size();
    unsigned firstId = 0;
    for (size_t i = 0; i < conds; ++i)
    {
        unsigned id = blockNamer ? blockNamer->nextIf() : static_cast<unsigned>(i);
        if (i == 0)
            firstId = id;
        std::string testLbl = blockNamer ? blockNamer->generic("if_test")
                                         : mangler.block("if_test_" + std::to_string(i));
        std::string thenLbl =
            blockNamer ? blockNamer->ifThen(id) : mangler.block("if_then_" + std::to_string(i));
        builder->addBlock(*func, testLbl);
        builder->addBlock(*func, thenLbl);
    }
    std::string elseLbl = blockNamer ? blockNamer->ifElse(firstId) : mangler.block("if_else");
    std::string endLbl = blockNamer ? blockNamer->ifEnd(firstId) : mangler.block("if_exit");
    builder->addBlock(*func, elseLbl);
    builder->addBlock(*func, endLbl);
    cur = &func->blocks[curIdx];
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

// Purpose: lower if condition.
// Parameters: const Expr &cond, BasicBlock *testBlk, BasicBlock *thenBlk, BasicBlock *falseBlk,
// il::support::SourceLoc loc. Returns: void. Side effects: may modify lowering state or emit IL.
// Relies on deterministic block naming via BlockNamer.
void Lowerer::lowerIfCondition(const Expr &cond,
                               BasicBlock *testBlk,
                               BasicBlock *thenBlk,
                               BasicBlock *falseBlk,
                               il::support::SourceLoc loc)
{
    cur = testBlk;
    RVal c = lowerExpr(cond);
    if (c.type.kind != Type::Kind::I1)
    {
        curLoc = loc;
        Value b1 = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), c.value);
        c = {b1, Type(Type::Kind::I1)};
    }
    emitCBr(c.value, thenBlk, falseBlk);
}

// Purpose: lower if branch.
// Parameters: const Stmt *stmt, BasicBlock *thenBlk, BasicBlock *exitBlk, il::support::SourceLoc
// loc. Returns: bool. Side effects: may modify lowering state or emit IL. Relies on deterministic
// block naming via BlockNamer.
bool Lowerer::lowerIfBranch(const Stmt *stmt,
                            BasicBlock *thenBlk,
                            BasicBlock *exitBlk,
                            il::support::SourceLoc loc)
{
    cur = thenBlk;
    if (stmt)
        lowerStmt(*stmt);
    if (!cur->terminated)
    {
        curLoc = loc;
        emitBr(exitBlk);
        return true;
    }
    return false;
}

// Purpose: lower if.
// Parameters: const IfStmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL. Relies on deterministic block naming via
// BlockNamer.
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
        cur = blocks.elseBlk;
        return;
    }

    cur = blocks.exitBlk;
}

// Purpose: lower while.
// Parameters: const WhileStmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL. Relies on deterministic block naming via
// BlockNamer.
void Lowerer::lowerWhile(const WhileStmt &stmt)
{
    // Adding blocks may reallocate the function's block list; capture index and
    // reacquire pointers to guarantee stability.
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
    BasicBlock *done = &func->blocks[start + 2];

    curLoc = stmt.loc;
    emitBr(head);

    // head
    cur = head;
    RVal cond = lowerExpr(*stmt.cond);
    if (cond.type.kind != Type::Kind::I1)
    {
        curLoc = stmt.loc;
        Value b1 = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), cond.value);
        cond = {b1, Type(Type::Kind::I1)};
    }
    curLoc = stmt.loc;
    emitCBr(cond.value, body, done);

    // body
    cur = body;
    for (auto &s : stmt.body)
    {
        lowerStmt(*s);
        if (cur->terminated)
            break;
    }
    bool term = cur->terminated;
    if (!term)
    {
        curLoc = stmt.loc;
        emitBr(head);
    }

    cur = done;
    cur->terminated = term;
}

// Purpose: setup for blocks.
// Parameters: bool varStep.
// Returns: Lowerer::ForBlocks.
// Side effects: may modify lowering state or emit IL. Relies on deterministic block naming via
// BlockNamer.
Lowerer::ForBlocks Lowerer::setupForBlocks(bool varStep)
{
    size_t curIdx = cur - &func->blocks[0];
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
    cur = &func->blocks[curIdx];
    return fb;
}

// Purpose: lower for const step.
// Parameters: const ForStmt &stmt, Value slot, RVal end, RVal step, int64_t stepConst.
// Returns: void.
// Side effects: may modify lowering state or emit IL. Relies on deterministic block naming via
// BlockNamer.
void Lowerer::lowerForConstStep(
    const ForStmt &stmt, Value slot, RVal end, RVal step, int64_t stepConst)
{
    ForBlocks fb = setupForBlocks(false);
    curLoc = stmt.loc;
    emitBr(&func->blocks[fb.headIdx]);
    cur = &func->blocks[fb.headIdx];
    curLoc = stmt.loc;
    Value curVal = emitLoad(Type(Type::Kind::I64), slot);
    Opcode cmp = stepConst >= 0 ? Opcode::SCmpLE : Opcode::SCmpGE;
    curLoc = stmt.loc;
    Value cond = emitBinary(cmp, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cond, &func->blocks[fb.bodyIdx], &func->blocks[fb.doneIdx]);
    cur = &func->blocks[fb.bodyIdx];
    for (auto &s : stmt.body)
    {
        lowerStmt(*s);
        if (cur->terminated)
            break;
    }
    bool term = cur->terminated;
    if (!term)
    {
        curLoc = stmt.loc;
        emitBr(&func->blocks[fb.incIdx]);
        cur = &func->blocks[fb.incIdx];
        curLoc = stmt.loc;
        emitForStep(slot, step.value);
        curLoc = stmt.loc;
        emitBr(&func->blocks[fb.headIdx]);
    }
    cur = &func->blocks[fb.doneIdx];
    cur->terminated = term;
}

// Purpose: lower for var step.
// Parameters: const ForStmt &stmt, Value slot, RVal end, RVal step.
// Returns: void.
// Side effects: may modify lowering state or emit IL. Relies on deterministic block naming via
// BlockNamer.
void Lowerer::lowerForVarStep(const ForStmt &stmt, Value slot, RVal end, RVal step)
{
    curLoc = stmt.loc;
    Value stepNonNeg =
        emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), step.value, Value::constInt(0));
    ForBlocks fb = setupForBlocks(true);
    curLoc = stmt.loc;
    emitCBr(stepNonNeg, &func->blocks[fb.headPosIdx], &func->blocks[fb.headNegIdx]);
    cur = &func->blocks[fb.headPosIdx];
    curLoc = stmt.loc;
    Value curVal = emitLoad(Type(Type::Kind::I64), slot);
    curLoc = stmt.loc;
    Value cmpPos = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cmpPos, &func->blocks[fb.bodyIdx], &func->blocks[fb.doneIdx]);
    cur = &func->blocks[fb.headNegIdx];
    curLoc = stmt.loc;
    curVal = emitLoad(Type(Type::Kind::I64), slot);
    curLoc = stmt.loc;
    Value cmpNeg = emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), curVal, end.value);
    curLoc = stmt.loc;
    emitCBr(cmpNeg, &func->blocks[fb.bodyIdx], &func->blocks[fb.doneIdx]);
    cur = &func->blocks[fb.bodyIdx];
    for (auto &s : stmt.body)
    {
        lowerStmt(*s);
        if (cur->terminated)
            break;
    }
    bool term = cur->terminated;
    if (!term)
    {
        curLoc = stmt.loc;
        emitBr(&func->blocks[fb.incIdx]);
        cur = &func->blocks[fb.incIdx];
        curLoc = stmt.loc;
        emitForStep(slot, step.value);
        curLoc = stmt.loc;
        emitCBr(stepNonNeg, &func->blocks[fb.headPosIdx], &func->blocks[fb.headNegIdx]);
    }
    cur = &func->blocks[fb.doneIdx];
    cur->terminated = term;
}

// Purpose: lower for.
// Parameters: const ForStmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL. Relies on deterministic block naming via
// BlockNamer.
void Lowerer::lowerFor(const ForStmt &stmt)
{
    RVal start = lowerExpr(*stmt.start);
    RVal end = lowerExpr(*stmt.end);
    RVal step = stmt.step ? lowerExpr(*stmt.step) : RVal{Value::constInt(1), Type(Type::Kind::I64)};
    auto it = varSlots.find(stmt.var);
    assert(it != varSlots.end());
    Value slot = Value::temp(it->second);
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

// Purpose: lower next.
// Parameters: const NextStmt &.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::lowerNext(const NextStmt &) {}

// Purpose: lower goto.
// Parameters: const GotoStmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL. Relies on deterministic block naming via
// BlockNamer.
void Lowerer::lowerGoto(const GotoStmt &stmt)
{
    auto it = lineBlocks.find(stmt.target);
    if (it != lineBlocks.end())
    {
        curLoc = stmt.loc;
        emitBr(&func->blocks[it->second]);
    }
}

// Purpose: lower end.
// Parameters: const EndStmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::lowerEnd(const EndStmt &stmt)
{
    curLoc = stmt.loc;
    emitBr(&func->blocks[fnExit]);
}

// Purpose: lower input.
// Parameters: const InputStmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
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
    bool isStr = !stmt.var.empty() && stmt.var.back() == '$';
    Value target = Value::temp(varSlots[stmt.var]);
    if (isStr)
    {
        emitStore(Type(Type::Kind::Str), target, s);
    }
    else
    {
        Value n = emitCallRet(Type(Type::Kind::I64), "rt_to_int", {s});
        emitStore(Type(Type::Kind::I64), target, n);
    }
}

// Purpose: lower dim.
// Parameters: const DimStmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::lowerDim(const DimStmt &stmt)
{
    RVal sz = lowerExpr(*stmt.size);
    curLoc = stmt.loc;
    Value bytes = emitBinary(Opcode::Mul, Type(Type::Kind::I64), sz.value, Value::constInt(8));
    Value base = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {bytes});
    auto it = varSlots.find(stmt.name);
    assert(it != varSlots.end());
    emitStore(Type(Type::Kind::Ptr), Value::temp(it->second), base);
    if (boundsChecks)
    {
        auto lenIt = arrayLenSlots.find(stmt.name);
        if (lenIt != arrayLenSlots.end())
            emitStore(Type(Type::Kind::I64), Value::temp(lenIt->second), sz.value);
    }
}

// Purpose: lower randomize.
// Parameters: const RandomizeStmt &stmt.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::lowerRandomize(const RandomizeStmt &stmt)
{
    RVal s = lowerExpr(*stmt.seed);
    Value seed = s.value;
    if (s.type.kind == Type::Kind::F64)
    {
        seed = emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), seed);
    }
    else if (s.type.kind == Type::Kind::I1)
    {
        seed = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), seed);
    }
    curLoc = stmt.loc;
    emitCall("rt_randomize_i64", {seed});
}

} // namespace il::frontends::basic

