// File: src/frontends/basic/Lowerer.cpp
// Purpose: Lowers BASIC AST to IL.
// Key invariants: None.
// Ownership/Lifetime: Uses contexts managed externally.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/io/Serializer.hpp" // might not needed but fine
#include <cassert>
#include <functional>
#include <limits>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

Lowerer::Lowerer(bool boundsChecks) : boundsChecks(boundsChecks) {}

Module Lowerer::lower(const Program &prog)
{
    Module m;
    mod = &m;
    build::IRBuilder b(m);
    builder = &b;

    mangler = NameMangler();
    lineBlocks.clear();
    varSlots.clear();
    arrayLenSlots.clear();
    strings.clear();
    usedStrEq = false;
    arrays.clear();
    boundsCheckId = 0;

    bool needInput = false;
    bool needToInt = false;
    bool needAlloc = false;
    for (const auto &s : prog.statements)
    {
        if (auto *in = dynamic_cast<InputStmt *>(s.get()))
        {
            needInput = true;
            if (in->var.empty() || in->var.back() != '$')
                needToInt = true;
        }
        if (dynamic_cast<DimStmt *>(s.get()))
            needAlloc = true;
    }

    b.addExtern("rt_print_str", Type(Type::Kind::Void), {Type(Type::Kind::Str)});
    b.addExtern("rt_print_i64", Type(Type::Kind::Void), {Type(Type::Kind::I64)});
    b.addExtern("rt_print_f64", Type(Type::Kind::Void), {Type(Type::Kind::F64)});
    b.addExtern("rt_len", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
    b.addExtern("rt_substr",
                Type(Type::Kind::Str),
                {Type(Type::Kind::Str), Type(Type::Kind::I64), Type(Type::Kind::I64)});
    if (boundsChecks)
        b.addExtern("rt_trap", Type(Type::Kind::Void), {Type(Type::Kind::Str)});
    if (needInput)
        b.addExtern("rt_input_line", Type(Type::Kind::Str), {});
    if (needToInt)
        b.addExtern("rt_to_int", Type(Type::Kind::I64), {Type(Type::Kind::Str)});
    if (needAlloc)
        b.addExtern("rt_alloc", Type(Type::Kind::Ptr), {Type(Type::Kind::I64)});

    Function &f = b.startFunction("main", Type(Type::Kind::I64), {});
    func = &f;

    b.addBlock(f, "entry");

    std::vector<int> lines;
    lines.reserve(prog.statements.size());
    for (const auto &stmt : prog.statements)
    {
        b.addBlock(f, mangler.block("L" + std::to_string(stmt->line)));
        lines.push_back(stmt->line);
    }
    fnExit = f.blocks.size();
    b.addBlock(f, mangler.block("exit"));

    for (size_t i = 0; i < lines.size(); ++i)
        lineBlocks[lines[i]] = i + 1;

    vars.clear();
    arrays.clear();
    collectVars(prog);

    // allocate slots in entry
    BasicBlock *entry = &f.blocks.front();
    cur = entry;
    for (const auto &v : vars)
    {
        curLoc = {};
        Value slot = emitAlloca(8);
        varSlots[v] = slot.id; // Value::temp id
    }
    if (boundsChecks)
    {
        for (const auto &a : arrays)
        {
            curLoc = {};
            Value slot = emitAlloca(8);
            arrayLenSlots[a] = slot.id;
        }
    }
    if (!prog.statements.empty())
    {
        curLoc = {};
        emitBr(&f.blocks[lineBlocks[prog.statements.front()->line]]);
    }
    else
    {
        curLoc = {};
        emitRet(Value::constInt(0));
    }

    // lower statements sequentially
    for (size_t i = 0; i < prog.statements.size(); ++i)
    {
        cur = &f.blocks[lineBlocks[prog.statements[i]->line]];
        lowerStmt(*prog.statements[i]);
        if (!cur->terminated)
        {
            BasicBlock *next = (i + 1 < prog.statements.size())
                                   ? &f.blocks[lineBlocks[prog.statements[i + 1]->line]]
                                   : &f.blocks[fnExit];
            curLoc = prog.statements[i]->loc;
            emitBr(next);
        }
    }

    cur = &f.blocks[fnExit];
    curLoc = {};
    emitRet(Value::constInt(0));

    if (usedStrEq)
        b.addExtern(
            "rt_str_eq", Type(Type::Kind::I1), {Type(Type::Kind::Str), Type(Type::Kind::Str)});

    return m;
}

void Lowerer::collectVars(const Program &prog)
{
    std::function<void(const Expr &)> ex = [&](const Expr &e)
    {
        if (auto *v = dynamic_cast<const VarExpr *>(&e))
        {
            vars.insert(v->name);
        }
        else if (auto *u = dynamic_cast<const UnaryExpr *>(&e))
        {
            ex(*u->expr);
        }
        else if (auto *b = dynamic_cast<const BinaryExpr *>(&e))
        {
            ex(*b->lhs);
            ex(*b->rhs);
        }
        else if (auto *c = dynamic_cast<const CallExpr *>(&e))
        {
            for (auto &a : c->args)
                ex(*a);
        }
        else if (auto *a = dynamic_cast<const ArrayExpr *>(&e))
        {
            vars.insert(a->name);
            arrays.insert(a->name);
            ex(*a->index);
        }
    };
    std::function<void(const Stmt &)> st = [&](const Stmt &s)
    {
        if (auto *lst = dynamic_cast<const StmtList *>(&s))
        {
            for (const auto &sub : lst->stmts)
                st(*sub);
        }
        else if (auto *p = dynamic_cast<const PrintStmt *>(&s))
        {
            for (const auto &it : p->items)
                if (it.kind == PrintItem::Kind::Expr)
                    ex(*it.expr);
        }
        else if (auto *l = dynamic_cast<const LetStmt *>(&s))
        {
            ex(*l->target);
            ex(*l->expr);
        }
        else if (auto *i = dynamic_cast<const IfStmt *>(&s))
        {
            ex(*i->cond);
            if (i->then_branch)
                st(*i->then_branch);
            for (const auto &e : i->elseifs)
            {
                ex(*e.cond);
                if (e.then_branch)
                    st(*e.then_branch);
            }
            if (i->else_branch)
                st(*i->else_branch);
        }
        else if (auto *w = dynamic_cast<const WhileStmt *>(&s))
        {
            ex(*w->cond);
            for (auto &bs : w->body)
                st(*bs);
        }
        else if (auto *f = dynamic_cast<const ForStmt *>(&s))
        {
            vars.insert(f->var);
            ex(*f->start);
            ex(*f->end);
            if (f->step)
                ex(*f->step);
            for (auto &bs : f->body)
                st(*bs);
        }
        else if (auto *inp = dynamic_cast<const InputStmt *>(&s))
        {
            vars.insert(inp->var);
        }
        else if (auto *d = dynamic_cast<const DimStmt *>(&s))
        {
            vars.insert(d->name);
            arrays.insert(d->name);
            ex(*d->size);
        }
    };
    for (auto &s : prog.statements)
        st(*s);
}

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
        lowerDim(*d);
}

Lowerer::RVal Lowerer::lowerExpr(const Expr &expr)
{
    curLoc = expr.loc;
    if (auto *i = dynamic_cast<const IntExpr *>(&expr))
    {
        return {Value::constInt(i->value), Type(Type::Kind::I64)};
    }
    else if (auto *s = dynamic_cast<const StringExpr *>(&expr))
    {
        std::string lbl = getStringLabel(s->value);
        Value tmp = emitConstStr(lbl);
        return {tmp, Type(Type::Kind::Str)};
    }
    else if (auto *v = dynamic_cast<const VarExpr *>(&expr))
    {
        auto it = varSlots.find(v->name);
        assert(it != varSlots.end());
        Value ptr = Value::temp(it->second);
        bool isStr = !v->name.empty() && v->name.back() == '$';
        Type ty = isStr ? Type(Type::Kind::Str) : Type(Type::Kind::I64);
        Value val = emitLoad(ty, ptr);
        return {val, ty};
    }
    else if (auto *u = dynamic_cast<const UnaryExpr *>(&expr))
    {
        RVal val = lowerExpr(*u->expr);
        curLoc = expr.loc;
        Value b1 = val.value;
        if (val.type.kind != Type::Kind::I1)
            b1 = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), val.value);
        Value b64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), b1);
        Value x = emitBinary(Opcode::Xor, Type(Type::Kind::I64), b64, Value::constInt(1));
        Value res = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), x);
        return {res, Type(Type::Kind::I1)};
    }
    else if (auto *b = dynamic_cast<const BinaryExpr *>(&expr))
    {
        if (b->op == BinaryExpr::Op::And || b->op == BinaryExpr::Op::Or)
        {
            RVal lhs = lowerExpr(*b->lhs);
            curLoc = expr.loc;
            Value addr = emitAlloca(1);
            if (b->op == BinaryExpr::Op::And)
            {
                BasicBlock *rhsBB = &builder->addBlock(*func, mangler.block("and_rhs"));
                BasicBlock *falseBB = &builder->addBlock(*func, mangler.block("and_false"));
                BasicBlock *doneBB = &builder->addBlock(*func, mangler.block("and_done"));
                curLoc = expr.loc;
                emitCBr(lhs.value, rhsBB, falseBB);
                cur = rhsBB;
                RVal rhs = lowerExpr(*b->rhs);
                curLoc = expr.loc;
                emitStore(Type(Type::Kind::I1), addr, rhs.value);
                curLoc = expr.loc;
                emitBr(doneBB);
                cur = falseBB;
                curLoc = expr.loc;
                emitStore(Type(Type::Kind::I1), addr, Value::constInt(0));
                curLoc = expr.loc;
                emitBr(doneBB);
                cur = doneBB;
            }
            else
            {
                BasicBlock *trueBB = &builder->addBlock(*func, mangler.block("or_true"));
                BasicBlock *rhsBB = &builder->addBlock(*func, mangler.block("or_rhs"));
                BasicBlock *doneBB = &builder->addBlock(*func, mangler.block("or_done"));
                curLoc = expr.loc;
                emitCBr(lhs.value, trueBB, rhsBB);
                cur = trueBB;
                curLoc = expr.loc;
                emitStore(Type(Type::Kind::I1), addr, Value::constInt(1));
                curLoc = expr.loc;
                emitBr(doneBB);
                cur = rhsBB;
                RVal rhs = lowerExpr(*b->rhs);
                curLoc = expr.loc;
                emitStore(Type(Type::Kind::I1), addr, rhs.value);
                curLoc = expr.loc;
                emitBr(doneBB);
                cur = doneBB;
            }
            curLoc = expr.loc;
            Value res = emitLoad(Type(Type::Kind::I1), addr);
            return {res, Type(Type::Kind::I1)};
        }
        else if (b->op == BinaryExpr::Op::IDiv || b->op == BinaryExpr::Op::Mod)
        {
            RVal lhs = lowerExpr(*b->lhs);
            RVal rhs = lowerExpr(*b->rhs);
            curLoc = expr.loc;
            Value cond =
                emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), rhs.value, Value::constInt(0));
            BasicBlock *trapBB = &builder->addBlock(*func, mangler.block("div0"));
            BasicBlock *okBB = &builder->addBlock(*func, mangler.block("divok"));
            emitCBr(cond, trapBB, okBB);
            cur = trapBB;
            curLoc = expr.loc;
            emitTrap();
            cur = okBB;
            curLoc = expr.loc;
            Opcode op = (b->op == BinaryExpr::Op::IDiv) ? Opcode::SDiv : Opcode::SRem;
            Value res = emitBinary(op, Type(Type::Kind::I64), lhs.value, rhs.value);
            return {res, Type(Type::Kind::I64)};
        }
        RVal lhs = lowerExpr(*b->lhs);
        RVal rhs = lowerExpr(*b->rhs);
        curLoc = expr.loc;
        if ((b->op == BinaryExpr::Op::Eq || b->op == BinaryExpr::Op::Ne) &&
            lhs.type.kind == Type::Kind::Str && rhs.type.kind == Type::Kind::Str)
        {
            usedStrEq = true;
            Value eq = emitCallRet(Type(Type::Kind::I1), "rt_str_eq", {lhs.value, rhs.value});
            if (b->op == BinaryExpr::Op::Ne)
            {
                Value z = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), eq);
                Value x = emitBinary(Opcode::Xor, Type(Type::Kind::I64), z, Value::constInt(1));
                Value res = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), x);
                return {res, Type(Type::Kind::I1)};
            }
            return {eq, Type(Type::Kind::I1)};
        }
        Opcode op = Opcode::Add;
        Type ty(Type::Kind::I64);
        switch (b->op)
        {
            case BinaryExpr::Op::Add:
                op = Opcode::Add;
                break;
            case BinaryExpr::Op::Sub:
                op = Opcode::Sub;
                break;
            case BinaryExpr::Op::Mul:
                op = Opcode::Mul;
                break;
            case BinaryExpr::Op::Div:
                op = Opcode::SDiv;
                break;
            case BinaryExpr::Op::IDiv:
                op = Opcode::SDiv;
                break;
            case BinaryExpr::Op::Mod:
                op = Opcode::SRem;
                break;
            case BinaryExpr::Op::Eq:
                op = Opcode::ICmpEq;
                ty = Type(Type::Kind::I1);
                break;
            case BinaryExpr::Op::Ne:
                op = Opcode::ICmpNe;
                ty = Type(Type::Kind::I1);
                break;
            case BinaryExpr::Op::Lt:
                op = Opcode::SCmpLT;
                ty = Type(Type::Kind::I1);
                break;
            case BinaryExpr::Op::Le:
                op = Opcode::SCmpLE;
                ty = Type(Type::Kind::I1);
                break;
            case BinaryExpr::Op::Gt:
                op = Opcode::SCmpGT;
                ty = Type(Type::Kind::I1);
                break;
            case BinaryExpr::Op::Ge:
                op = Opcode::SCmpGE;
                ty = Type(Type::Kind::I1);
                break;
            case BinaryExpr::Op::And:
            case BinaryExpr::Op::Or:
                break; // handled above
        }
        Value res = emitBinary(op, ty, lhs.value, rhs.value);
        return {res, ty};
    }

    else if (auto *c = dynamic_cast<const CallExpr *>(&expr))
    {
        if (c->builtin == CallExpr::Builtin::Len)
        {
            RVal s = lowerExpr(*c->args[0]);
            curLoc = expr.loc;
            Value res = emitCallRet(Type(Type::Kind::I64), "rt_len", {s.value});
            return {res, Type(Type::Kind::I64)};
        }
        else if (c->builtin == CallExpr::Builtin::Mid)
        {
            RVal s = lowerExpr(*c->args[0]);
            RVal i = lowerExpr(*c->args[1]);
            Value start0 =
                emitBinary(Opcode::Add, Type(Type::Kind::I64), i.value, Value::constInt(-1));
            Value count = (c->args.size() >= 3)
                              ? lowerExpr(*c->args[2]).value
                              : Value::constInt(std::numeric_limits<int64_t>::max());
            curLoc = expr.loc;
            Value res = emitCallRet(Type(Type::Kind::Str), "rt_substr", {s.value, start0, count});
            return {res, Type(Type::Kind::Str)};
        }
        else if (c->builtin == CallExpr::Builtin::Left)
        {
            RVal s = lowerExpr(*c->args[0]);
            RVal n = lowerExpr(*c->args[1]);
            curLoc = expr.loc;
            Value res = emitCallRet(
                Type(Type::Kind::Str), "rt_substr", {s.value, Value::constInt(0), n.value});
            return {res, Type(Type::Kind::Str)};
        }
        else if (c->builtin == CallExpr::Builtin::Right)
        {
            RVal s = lowerExpr(*c->args[0]);
            RVal n = lowerExpr(*c->args[1]);
            curLoc = expr.loc;
            Value len = emitCallRet(Type(Type::Kind::I64), "rt_len", {s.value});
            Value negN =
                emitBinary(Opcode::Mul, Type(Type::Kind::I64), n.value, Value::constInt(-1));
            Value start = emitBinary(Opcode::Add, Type(Type::Kind::I64), len, negN);
            Value res = emitCallRet(Type(Type::Kind::Str), "rt_substr", {s.value, start, n.value});
            return {res, Type(Type::Kind::Str)};
        }
    }
    else if (auto *a = dynamic_cast<const ArrayExpr *>(&expr))
    {
        Value ptr = lowerArrayAddr(*a);
        curLoc = expr.loc;
        Value val = emitLoad(Type(Type::Kind::I64), ptr);
        return {val, Type(Type::Kind::I64)};
    }
    curLoc = expr.loc;
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

void Lowerer::lowerLet(const LetStmt &stmt)
{
    RVal v = lowerExpr(*stmt.expr);
    if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
    {
        auto it = varSlots.find(var->name);
        assert(it != varSlots.end());
        bool isStr = !var->name.empty() && var->name.back() == '$';
        if (!isStr && v.type.kind == Type::Kind::I1)
        {
            curLoc = stmt.loc;
            Value z = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), v.value);
            v.value = z;
            v.type = Type(Type::Kind::I64);
        }
        curLoc = stmt.loc;
        Type ty = isStr ? Type(Type::Kind::Str) : Type(Type::Kind::I64);
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

void Lowerer::lowerIf(const IfStmt &stmt)
{
    size_t conds = 1 + stmt.elseifs.size();
    size_t curIdx = cur - &func->blocks[0];
    size_t start = func->blocks.size();
    for (size_t i = 0; i < conds; ++i)
    {
        builder->addBlock(*func, mangler.block("if_test_" + std::to_string(i)));
        builder->addBlock(*func, mangler.block("if_then_" + std::to_string(i)));
    }
    builder->addBlock(*func, mangler.block("if_else"));
    builder->addBlock(*func, mangler.block("if_exit"));
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

    // jump to first test
    curLoc = stmt.loc;
    emitBr(&func->blocks[testIdx[0]]);

    // initial IF
    std::vector<const Expr *> condExprs;
    std::vector<const Stmt *> thenStmts;
    condExprs.push_back(stmt.cond.get());
    thenStmts.push_back(stmt.then_branch.get());
    for (const auto &e : stmt.elseifs)
    {
        condExprs.push_back(e.cond.get());
        thenStmts.push_back(e.then_branch.get());
    }
    for (size_t i = 0; i < conds; ++i)
    {
        cur = &func->blocks[testIdx[i]];
        RVal c = lowerExpr(*condExprs[i]);
        if (c.type.kind != Type::Kind::I1)
        {
            curLoc = stmt.loc;
            Value b1 = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), c.value);
            c = {b1, Type(Type::Kind::I1)};
        }
        BasicBlock *f = (i + 1 < conds) ? &func->blocks[testIdx[i + 1]] : elseBlk;
        emitCBr(c.value, &func->blocks[thenIdx[i]], f);

        cur = &func->blocks[thenIdx[i]];
        if (thenStmts[i])
            lowerStmt(*thenStmts[i]);
        if (!cur->terminated)
        {
            curLoc = stmt.loc;
            emitBr(exitBlk);
        }
    }

    // else block
    cur = elseBlk;
    if (stmt.else_branch)
        lowerStmt(*stmt.else_branch);
    if (!cur->terminated)
    {
        curLoc = stmt.loc;
        emitBr(exitBlk);
    }

    cur = exitBlk;
}

void Lowerer::lowerWhile(const WhileStmt &stmt)
{
    // Adding blocks may reallocate the function's block list; capture index and
    // reacquire pointers to guarantee stability.
    size_t start = func->blocks.size();
    builder->addBlock(*func, mangler.block("loop_head"));
    builder->addBlock(*func, mangler.block("loop_body"));
    builder->addBlock(*func, mangler.block("done"));
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
    if (!cur->terminated)
    {
        curLoc = stmt.loc;
        emitBr(head);
    }

    cur = done;
}

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
    long stepConst = 1;
    if (stmt.step)
    {
        if (auto *ie = dynamic_cast<const IntExpr *>(stmt.step.get()))
            stepConst = ie->value;
    }
    if (constStep)
    {
        size_t curIdx = cur - &func->blocks[0];
        size_t base = func->blocks.size();
        builder->addBlock(*func, mangler.block("for_head"));
        builder->addBlock(*func, mangler.block("for_body"));
        builder->addBlock(*func, mangler.block("for_inc"));
        builder->addBlock(*func, mangler.block("for_done"));
        size_t headIdx = base;
        size_t bodyIdx = base + 1;
        size_t incIdx = base + 2;
        size_t doneIdx = base + 3;
        cur = &func->blocks[curIdx];
        curLoc = stmt.loc;
        emitBr(&func->blocks[headIdx]);
        cur = &func->blocks[headIdx];
        curLoc = stmt.loc;
        Value curVal = emitLoad(Type(Type::Kind::I64), slot);
        Opcode cmp = stepConst >= 0 ? Opcode::SCmpLE : Opcode::SCmpGE;
        curLoc = stmt.loc;
        Value cond = emitBinary(cmp, Type(Type::Kind::I1), curVal, end.value);
        curLoc = stmt.loc;
        emitCBr(cond, &func->blocks[bodyIdx], &func->blocks[doneIdx]);
        cur = &func->blocks[bodyIdx];
        for (auto &s : stmt.body)
        {
            lowerStmt(*s);
            if (cur->terminated)
                break;
        }
        if (!cur->terminated)
        {
            curLoc = stmt.loc;
            emitBr(&func->blocks[incIdx]);
        }
        cur = &func->blocks[incIdx];
        curLoc = stmt.loc;
        Value load = emitLoad(Type(Type::Kind::I64), slot);
        curLoc = stmt.loc;
        Value add = emitBinary(Opcode::Add, Type(Type::Kind::I64), load, step.value);
        curLoc = stmt.loc;
        emitStore(Type(Type::Kind::I64), slot, add);
        curLoc = stmt.loc;
        emitBr(&func->blocks[headIdx]);
        cur = &func->blocks[doneIdx];
    }
    else
    {
        curLoc = stmt.loc;
        Value stepNonNeg =
            emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), step.value, Value::constInt(0));
        size_t curIdx = cur - &func->blocks[0];
        size_t base = func->blocks.size();
        builder->addBlock(*func, mangler.block("for_head_pos"));
        builder->addBlock(*func, mangler.block("for_head_neg"));
        builder->addBlock(*func, mangler.block("for_body"));
        builder->addBlock(*func, mangler.block("for_inc"));
        builder->addBlock(*func, mangler.block("for_done"));
        size_t headPosIdx = base;
        size_t headNegIdx = base + 1;
        size_t bodyIdx = base + 2;
        size_t incIdx = base + 3;
        size_t doneIdx = base + 4;
        cur = &func->blocks[curIdx];
        curLoc = stmt.loc;
        emitCBr(stepNonNeg, &func->blocks[headPosIdx], &func->blocks[headNegIdx]);
        cur = &func->blocks[headPosIdx];
        curLoc = stmt.loc;
        Value curVal = emitLoad(Type(Type::Kind::I64), slot);
        curLoc = stmt.loc;
        Value cmpPos = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), curVal, end.value);
        curLoc = stmt.loc;
        emitCBr(cmpPos, &func->blocks[bodyIdx], &func->blocks[doneIdx]);
        cur = &func->blocks[headNegIdx];
        curLoc = stmt.loc;
        curVal = emitLoad(Type(Type::Kind::I64), slot);
        curLoc = stmt.loc;
        Value cmpNeg = emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), curVal, end.value);
        curLoc = stmt.loc;
        emitCBr(cmpNeg, &func->blocks[bodyIdx], &func->blocks[doneIdx]);
        cur = &func->blocks[bodyIdx];
        for (auto &s : stmt.body)
        {
            lowerStmt(*s);
            if (cur->terminated)
                break;
        }
        if (!cur->terminated)
        {
            curLoc = stmt.loc;
            emitBr(&func->blocks[incIdx]);
        }
        cur = &func->blocks[incIdx];
        curLoc = stmt.loc;
        Value load = emitLoad(Type(Type::Kind::I64), slot);
        curLoc = stmt.loc;
        Value add = emitBinary(Opcode::Add, Type(Type::Kind::I64), load, step.value);
        curLoc = stmt.loc;
        emitStore(Type(Type::Kind::I64), slot, add);
        curLoc = stmt.loc;
        emitCBr(stepNonNeg, &func->blocks[headPosIdx], &func->blocks[headNegIdx]);
        cur = &func->blocks[doneIdx];
    }
}

void Lowerer::lowerNext(const NextStmt &) {}

void Lowerer::lowerGoto(const GotoStmt &stmt)
{
    auto it = lineBlocks.find(stmt.target);
    if (it != lineBlocks.end())
    {
        curLoc = stmt.loc;
        emitBr(&func->blocks[it->second]);
    }
}

void Lowerer::lowerEnd(const EndStmt &stmt)
{
    curLoc = stmt.loc;
    emitBr(&func->blocks[fnExit]);
}

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

Value Lowerer::lowerArrayAddr(const ArrayExpr &expr)
{
    auto it = varSlots.find(expr.name);
    assert(it != varSlots.end());
    Value slot = Value::temp(it->second);
    Value base = emitLoad(Type(Type::Kind::Ptr), slot);
    RVal idx = lowerExpr(*expr.index);
    curLoc = expr.loc;
    if (boundsChecks)
    {
        auto lenIt = arrayLenSlots.find(expr.name);
        assert(lenIt != arrayLenSlots.end());
        Value len = emitLoad(Type(Type::Kind::I64), Value::temp(lenIt->second));
        Value neg = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), idx.value, Value::constInt(0));
        Value ge = emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), idx.value, len);
        Value neg64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), neg);
        Value ge64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), ge);
        Value or64 = emitBinary(Opcode::Or, Type(Type::Kind::I64), neg64, ge64);
        Value cond = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), or64);
        size_t curIdx = static_cast<size_t>(cur - &func->blocks[0]);
        size_t okIdx = func->blocks.size();
        builder->addBlock(*func, mangler.block("bc_ok" + std::to_string(boundsCheckId)));
        size_t failIdx = func->blocks.size();
        builder->addBlock(*func, mangler.block("bc_fail" + std::to_string(boundsCheckId)));
        BasicBlock *ok = &func->blocks[okIdx];
        BasicBlock *fail = &func->blocks[failIdx];
        cur = &func->blocks[curIdx];
        ++boundsCheckId;
        emitCBr(cond, fail, ok);
        cur = fail;
        std::string msg = "bounds check failed: " + expr.name + "[i]";
        Value s = emitConstStr(getStringLabel(msg));
        emitCall("rt_trap", {s});
        emitTrap();
        cur = ok;
    }
    Value off = emitBinary(Opcode::Shl, Type(Type::Kind::I64), idx.value, Value::constInt(3));
    Value ptr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), base, off);
    return ptr;
}

Value Lowerer::emitAlloca(int bytes)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Alloca;
    in.type = Type(Type::Kind::Ptr);
    in.operands.push_back(Value::constInt(bytes));
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

Value Lowerer::emitLoad(Type ty, Value addr)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Load;
    in.type = ty;
    in.operands.push_back(addr);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

void Lowerer::emitStore(Type ty, Value addr, Value val)
{
    Instr in;
    in.op = Opcode::Store;
    in.type = ty;
    in.operands = {addr, val};
    in.loc = curLoc;
    cur->instructions.push_back(in);
}

Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {lhs, rhs};
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

Value Lowerer::emitUnary(Opcode op, Type ty, Value val)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = op;
    in.type = ty;
    in.operands = {val};
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

void Lowerer::emitBr(BasicBlock *target)
{
    Instr in;
    in.op = Opcode::Br;
    in.type = Type(Type::Kind::Void);
    in.labels.push_back(target->label);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

void Lowerer::emitCBr(Value cond, BasicBlock *t, BasicBlock *f)
{
    Instr in;
    in.op = Opcode::CBr;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(cond);
    in.labels.push_back(t->label);
    in.labels.push_back(f->label);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    Instr in;
    in.op = Opcode::Call;
    in.type = Type(Type::Kind::Void);
    in.callee = callee;
    in.operands = args;
    in.loc = curLoc;
    cur->instructions.push_back(in);
}

Value Lowerer::emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::Call;
    in.type = ty;
    in.callee = callee;
    in.operands = args;
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

Value Lowerer::emitConstStr(const std::string &globalName)
{
    unsigned id = nextTempId();
    Instr in;
    in.result = id;
    in.op = Opcode::ConstStr;
    in.type = Type(Type::Kind::Str);
    in.operands.push_back(Value::global(globalName));
    in.loc = curLoc;
    cur->instructions.push_back(in);
    return Value::temp(id);
}

void Lowerer::emitRet(Value v)
{
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(v);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

void Lowerer::emitTrap()
{
    Instr in;
    in.op = Opcode::Trap;
    in.type = Type(Type::Kind::Void);
    in.loc = curLoc;
    cur->instructions.push_back(in);
    cur->terminated = true;
}

std::string Lowerer::getStringLabel(const std::string &s)
{
    auto it = strings.find(s);
    if (it != strings.end())
        return it->second;
    std::string name = ".L" + std::to_string(strings.size());
    builder->addGlobalStr(name, s);
    strings[s] = name;
    return name;
}

unsigned Lowerer::nextTempId()
{
    std::string name = mangler.nextTemp();
    return static_cast<unsigned>(std::stoul(name.substr(2)));
}

} // namespace il::frontends::basic
