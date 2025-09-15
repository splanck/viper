// File: src/frontends/basic/Lowerer.cpp
// Purpose: Lowers BASIC AST to IL with control-flow helpers and centralized
// runtime declarations.
// Key invariants: Block names inside procedures are deterministic via BlockNamer.
// Ownership/Lifetime: Uses contexts managed externally.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/io/Serializer.hpp" // might not needed but fine
#include <cassert>
#include <functional>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

// Purpose: lowerer.
// Parameters: bool boundsChecks.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
Lowerer::Lowerer(bool boundsChecks) : boundsChecks(boundsChecks) {}

// Purpose: lower program.
// Parameters: const Program &prog.
// Returns: Module.
// Side effects: may modify lowering state or emit IL.
Module Lowerer::lowerProgram(const Program &prog)
{
    // Procs first, then a synthetic @main for top-level statements.
    Module m;
    mod = &m;
    build::IRBuilder b(m);
    builder = &b;

    mangler = NameMangler();
    lineBlocks.clear();
    varSlots.clear();
    arrayLenSlots.clear();
    strings.clear();
    arrays.clear();
    boundsCheckId = 0;

    needInputLine = false;
    needRtToInt = false;
    needRtIntToStr = false;
    needRtF64ToStr = false;
    needAlloc = false;
    needRtStrEq = false;
    needRtConcat = false;
    needRtLeft = false;
    needRtRight = false;
    needRtMid2 = false;
    needRtMid3 = false;
    needRtInstr2 = false;
    needRtInstr3 = false;
    needRtLtrim = false;
    needRtRtrim = false;
    needRtTrim = false;
    needRtUcase = false;
    needRtLcase = false;
    needRtChr = false;
    needRtAsc = false;
    runtimeOrder.clear();
    runtimeSet.clear();

    scanProgram(prog);
    declareRequiredRuntime(b);
    emitProgram(prog);

    return m;
}

// Purpose: lower.
// Parameters: const Program &prog.
// Returns: Module.
// Side effects: may modify lowering state or emit IL.
Module Lowerer::lower(const Program &prog)
{
    return lowerProgram(prog);
}

// Purpose: collect vars.
// Parameters: const std::vector<const Stmt *> &stmts.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::collectVars(const std::vector<const Stmt *> &stmts)
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
        else if (auto *c = dynamic_cast<const BuiltinCallExpr *>(&e))
        {
            for (auto &a : c->args)
                ex(*a);
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
            vars.insert(d->name); // DIM locals become stack slots in entry
            arrays.insert(d->name);
            ex(*d->size);
        }
    };
    for (auto &s : stmts)
        st(*s);
}

/// @brief Lower FUNCTION body into an IL function.
// Purpose: lower function decl.
// Parameters: const FunctionDecl &decl.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::lowerFunctionDecl(const FunctionDecl &decl)
{
    resetLoweringState();

    using ASTType = ::il::frontends::basic::Type;
    std::vector<const Stmt *> bodyPtrs;
    for (const auto &s : decl.body)
        bodyPtrs.push_back(s.get());
    collectVars(bodyPtrs);

    std::unordered_set<std::string> paramNames;
    std::vector<il::core::Param> params;
    for (const auto &p : decl.params)
    {
        paramNames.insert(p.name);
        il::core::Type ty =
            p.is_array
                ? il::core::Type(il::core::Type::Kind::Ptr)
                : (p.type == ASTType::I64
                       ? il::core::Type(il::core::Type::Kind::I64)
                       : (p.type == ASTType::F64 ? il::core::Type(il::core::Type::Kind::F64)
                                                 : il::core::Type(il::core::Type::Kind::Str)));
        params.push_back({p.name, ty});
    }

    il::core::Type retTy =
        decl.ret == ASTType::I64
            ? il::core::Type(il::core::Type::Kind::I64)
            : (decl.ret == ASTType::F64 ? il::core::Type(il::core::Type::Kind::F64)
                                        : il::core::Type(il::core::Type::Kind::Str));

    Function &f = builder->startFunction(decl.name, retTy, params);
    func = &f;

    blockNamer = std::make_unique<BlockNamer>(decl.name);

    builder->addBlock(f, blockNamer->entry());

    for (size_t i = 0; i < decl.params.size(); ++i)
        mangler.nextTemp();

    std::vector<int> lines;
    lines.reserve(decl.body.size());
    for (const auto &stmt : decl.body)
    {
        if (blockNamer)
            builder->addBlock(f, blockNamer->line(stmt->line));
        else
            builder->addBlock(f, mangler.block("L" + std::to_string(stmt->line) + "_" + decl.name));
        lines.push_back(stmt->line);
    }
    fnExit = f.blocks.size();
    if (blockNamer)
        builder->addBlock(f, blockNamer->ret());
    else
        builder->addBlock(f, mangler.block("ret_" + decl.name));

    for (size_t i = 0; i < lines.size(); ++i)
        lineBlocks[lines[i]] = i + 1;

    BasicBlock *entry = &f.blocks.front();
    cur = entry;
    materializeParams(decl.params);

    // allocate slots for locals (including DIM declarations) in entry
    for (const auto &v : vars)
    {
        if (paramNames.count(v))
            continue;
        curLoc = {};
        Value slot = emitAlloca(8);
        varSlots[v] = slot.id;
    }
    if (boundsChecks)
    {
        for (const auto &a : arrays)
        {
            if (paramNames.count(a))
                continue;
            curLoc = {};
            Value slot = emitAlloca(8);
            arrayLenSlots[a] = slot.id;
        }
    }

    auto defaultRet = [&]()
    {
        switch (decl.ret)
        {
            case ASTType::I64:
                return Value::constInt(0);
            case ASTType::F64:
                return Value::constFloat(0.0);
            case ASTType::Str:
                return emitConstStr(getStringLabel(""));
        }
        return Value::constInt(0);
    };

    if (!lowerFunctionBody(decl, defaultRet))
    {
        blockNamer.reset();
        return;
    }

    finalizeFunction(defaultRet);
}

/// @brief Lower SUB body into an IL function.
// Purpose: lower sub decl.
// Parameters: const SubDecl &decl.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::lowerSubDecl(const SubDecl &decl)
{
    resetLoweringState();

    using ASTType = ::il::frontends::basic::Type;
    std::vector<const Stmt *> bodyPtrs;
    for (const auto &s : decl.body)
        bodyPtrs.push_back(s.get());
    collectVars(bodyPtrs);

    std::unordered_set<std::string> paramNames;
    std::vector<il::core::Param> params;
    for (const auto &p : decl.params)
    {
        paramNames.insert(p.name);
        il::core::Type ty =
            p.is_array
                ? il::core::Type(il::core::Type::Kind::Ptr)
                : (p.type == ASTType::I64
                       ? il::core::Type(il::core::Type::Kind::I64)
                       : (p.type == ASTType::F64 ? il::core::Type(il::core::Type::Kind::F64)
                                                 : il::core::Type(il::core::Type::Kind::Str)));
        params.push_back({p.name, ty});
    }

    Function &f =
        builder->startFunction(decl.name, il::core::Type(il::core::Type::Kind::Void), params);
    func = &f;

    blockNamer = std::make_unique<BlockNamer>(decl.name);

    builder->addBlock(f, blockNamer->entry());

    for (size_t i = 0; i < decl.params.size(); ++i)
        mangler.nextTemp();

    std::vector<int> lines;
    lines.reserve(decl.body.size());
    for (const auto &stmt : decl.body)
    {
        if (blockNamer)
            builder->addBlock(f, blockNamer->line(stmt->line));
        else
            builder->addBlock(f, mangler.block("L" + std::to_string(stmt->line) + "_" + decl.name));
        lines.push_back(stmt->line);
    }
    fnExit = f.blocks.size();
    if (blockNamer)
        builder->addBlock(f, blockNamer->ret());
    else
        builder->addBlock(f, mangler.block("ret_" + decl.name));

    for (size_t i = 0; i < lines.size(); ++i)
        lineBlocks[lines[i]] = i + 1;

    BasicBlock *entry = &f.blocks.front();
    cur = entry;
    materializeParams(decl.params);

    // allocate slots for locals (including DIM declarations) in entry
    for (const auto &v : vars)
    {
        if (paramNames.count(v))
            continue;
        curLoc = {};
        Value slot = emitAlloca(8);
        varSlots[v] = slot.id;
    }
    if (boundsChecks)
    {
        for (const auto &a : arrays)
        {
            if (paramNames.count(a))
                continue;
            curLoc = {};
            Value slot = emitAlloca(8);
            arrayLenSlots[a] = slot.id;
        }
    }

    if (!decl.body.empty())
    {
        curLoc = {};
        emitBr(&f.blocks[lineBlocks[decl.body.front()->line]]);
    }
    else
    {
        curLoc = {};
        emitRetVoid();
        return;
    }

    for (size_t i = 0; i < decl.body.size(); ++i)
    {
        cur = &f.blocks[lineBlocks[decl.body[i]->line]];
        lowerStmt(*decl.body[i]);
        if (cur->terminated)
            break;
        BasicBlock *next = (i + 1 < decl.body.size())
                               ? &f.blocks[lineBlocks[decl.body[i + 1]->line]]
                               : &f.blocks[fnExit];
        emitBr(next);
    }

    cur = &f.blocks[fnExit];
    curLoc = {};
    emitRetVoid();

    blockNamer.reset();
}

// Purpose: reset lowering state.
// Parameters: none.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::resetLoweringState()
{
    vars.clear();
    arrays.clear();
    varSlots.clear();
    arrayLenSlots.clear();
    lineBlocks.clear();
    boundsCheckId = 0;
}

// Purpose: lower function body.
// Parameters: const FunctionDecl &decl, const std::function<Value(.
// Returns: bool.
// Side effects: may modify lowering state or emit IL.
bool Lowerer::lowerFunctionBody(const FunctionDecl &decl, const std::function<Value()> &defaultRet)
{
    Function &f = *func;
    if (!decl.body.empty())
    {
        curLoc = {};
        emitBr(&f.blocks[lineBlocks[decl.body.front()->line]]);
    }
    else
    {
        curLoc = {};
        emitRet(defaultRet());
        return false;
    }

    for (size_t i = 0; i < decl.body.size(); ++i)
    {
        cur = &f.blocks[lineBlocks[decl.body[i]->line]];
        lowerStmt(*decl.body[i]);
        if (cur->terminated)
            break;
        BasicBlock *next = (i + 1 < decl.body.size())
                               ? &f.blocks[lineBlocks[decl.body[i + 1]->line]]
                               : &f.blocks[fnExit];
        emitBr(next);
    }

    return true;
}

// Purpose: finalize function.
// Parameters: const std::function<Value(.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::finalizeFunction(const std::function<Value()> &defaultRet)
{
    cur = &func->blocks[fnExit];
    curLoc = {};
    emitRet(defaultRet());
    blockNamer.reset();
}

/// @brief Allocate stack slots for parameters and store incoming values. Array
/// parameters keep their pointer/handle without copying.
// Purpose: materialize params.
// Parameters: const std::vector<Param> &params.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::materializeParams(const std::vector<Param> &params)
{
    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto &p = params[i];
        Value slot = emitAlloca(8);
        varSlots[p.name] = slot.id;
        il::core::Type ty = func->params[i].type;
        Value incoming = Value::temp(func->params[i].id);
        emitStore(ty, slot, incoming);
        if (p.is_array)
            arrays.insert(p.name);
    }
}

// Purpose: collect vars.
// Parameters: const Program &prog.
// Returns: void.
// Side effects: may modify lowering state or emit IL.
void Lowerer::collectVars(const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(ptrs);
}

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
        lowerDim(*d);
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

// Purpose: lower var expr.
// Parameters: const VarExpr &v.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerVarExpr(const VarExpr &v)
{
    curLoc = v.loc;
    auto it = varSlots.find(v.name);
    assert(it != varSlots.end());
    Value ptr = Value::temp(it->second);
    bool isArray = arrays.count(v.name);
    bool isStr = !v.name.empty() && v.name.back() == '$';
    bool isF64 = !v.name.empty() && v.name.back() == '#';
    Type ty = isArray ? Type(Type::Kind::Ptr)
                      : (isStr ? Type(Type::Kind::Str)
                               : (isF64 ? Type(Type::Kind::F64) : Type(Type::Kind::I64)));
    Value val = emitLoad(ty, ptr);
    return {val, ty};
}

// Purpose: lower unary expr.
// Parameters: const UnaryExpr &u.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerUnaryExpr(const UnaryExpr &u)
{
    RVal val = lowerExpr(*u.expr);
    curLoc = u.loc;
    Value b1 = val.value;
    if (val.type.kind != Type::Kind::I1)
        b1 = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), val.value);
    Value b64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), b1);
    Value x = emitBinary(Opcode::Xor, Type(Type::Kind::I64), b64, Value::constInt(1));
    Value res = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), x);
    return {res, Type(Type::Kind::I1)};
}

// Purpose: lower logical binary.
// Parameters: const BinaryExpr &b.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerLogicalBinary(const BinaryExpr &b)
{
    RVal lhs = lowerExpr(*b.lhs);
    curLoc = b.loc;
    Value addr = emitAlloca(1);
    if (b.op == BinaryExpr::Op::And)
    {
        std::string rhsLbl = blockNamer ? blockNamer->generic("and_rhs") : mangler.block("and_rhs");
        std::string falseLbl =
            blockNamer ? blockNamer->generic("and_false") : mangler.block("and_false");
        std::string doneLbl =
            blockNamer ? blockNamer->generic("and_done") : mangler.block("and_done");
        BasicBlock *rhsBB = &builder->addBlock(*func, rhsLbl);
        BasicBlock *falseBB = &builder->addBlock(*func, falseLbl);
        BasicBlock *doneBB = &builder->addBlock(*func, doneLbl);
        curLoc = b.loc;
        emitCBr(lhs.value, rhsBB, falseBB);
        cur = rhsBB;
        RVal rhs = lowerExpr(*b.rhs);
        curLoc = b.loc;
        emitStore(Type(Type::Kind::I1), addr, rhs.value);
        curLoc = b.loc;
        emitBr(doneBB);
        cur = falseBB;
        curLoc = b.loc;
        emitStore(Type(Type::Kind::I1), addr, Value::constInt(0));
        curLoc = b.loc;
        emitBr(doneBB);
        cur = doneBB;
    }
    else
    {
        std::string trueLbl =
            blockNamer ? blockNamer->generic("or_true") : mangler.block("or_true");
        std::string rhsLbl = blockNamer ? blockNamer->generic("or_rhs") : mangler.block("or_rhs");
        std::string doneLbl =
            blockNamer ? blockNamer->generic("or_done") : mangler.block("or_done");
        BasicBlock *trueBB = &builder->addBlock(*func, trueLbl);
        BasicBlock *rhsBB = &builder->addBlock(*func, rhsLbl);
        BasicBlock *doneBB = &builder->addBlock(*func, doneLbl);
        curLoc = b.loc;
        emitCBr(lhs.value, trueBB, rhsBB);
        cur = trueBB;
        curLoc = b.loc;
        emitStore(Type(Type::Kind::I1), addr, Value::constInt(1));
        curLoc = b.loc;
        emitBr(doneBB);
        cur = rhsBB;
        RVal rhs = lowerExpr(*b.rhs);
        curLoc = b.loc;
        emitStore(Type(Type::Kind::I1), addr, rhs.value);
        curLoc = b.loc;
        emitBr(doneBB);
        cur = doneBB;
    }
    curLoc = b.loc;
    Value res = emitLoad(Type(Type::Kind::I1), addr);
    return {res, Type(Type::Kind::I1)};
}

// Purpose: lower div or mod.
// Parameters: const BinaryExpr &b.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerDivOrMod(const BinaryExpr &b)
{
    RVal lhs = lowerExpr(*b.lhs);
    RVal rhs = lowerExpr(*b.rhs);
    curLoc = b.loc;
    Value cond = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), rhs.value, Value::constInt(0));
    std::string trapLbl = blockNamer ? blockNamer->generic("div0") : mangler.block("div0");
    std::string okLbl = blockNamer ? blockNamer->generic("divok") : mangler.block("divok");
    BasicBlock *trapBB = &builder->addBlock(*func, trapLbl);
    BasicBlock *okBB = &builder->addBlock(*func, okLbl);
    emitCBr(cond, trapBB, okBB);
    cur = trapBB;
    curLoc = b.loc;
    emitTrap();
    cur = okBB;
    curLoc = b.loc;
    Opcode op = (b.op == BinaryExpr::Op::IDiv) ? Opcode::SDiv : Opcode::SRem;
    Value res = emitBinary(op, Type(Type::Kind::I64), lhs.value, rhs.value);
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower string binary.
// Parameters: const BinaryExpr &b, RVal lhs, RVal rhs.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerStringBinary(const BinaryExpr &b, RVal lhs, RVal rhs)
{
    curLoc = b.loc;
    if (b.op == BinaryExpr::Op::Add)
    {
        Value res = emitCallRet(Type(Type::Kind::Str), "rt_concat", {lhs.value, rhs.value});
        return {res, Type(Type::Kind::Str)};
    }
    Value eq = emitCallRet(Type(Type::Kind::I1), "rt_str_eq", {lhs.value, rhs.value});
    if (b.op == BinaryExpr::Op::Ne)
    {
        Value z = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), eq);
        Value x = emitBinary(Opcode::Xor, Type(Type::Kind::I64), z, Value::constInt(1));
        Value res = emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), x);
        return {res, Type(Type::Kind::I1)};
    }
    return {eq, Type(Type::Kind::I1)};
}

// Purpose: lower numeric binary.
// Parameters: const BinaryExpr &b, RVal lhs, RVal rhs.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerNumericBinary(const BinaryExpr &b, RVal lhs, RVal rhs)
{
    curLoc = b.loc;
    if (lhs.type.kind == Type::Kind::I64 && rhs.type.kind == Type::Kind::F64)
    {
        lhs.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), lhs.value);
        lhs.type = Type(Type::Kind::F64);
    }
    else if (lhs.type.kind == Type::Kind::F64 && rhs.type.kind == Type::Kind::I64)
    {
        rhs.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), rhs.value);
        rhs.type = Type(Type::Kind::F64);
    }
    bool isFloat = lhs.type.kind == Type::Kind::F64;
    Opcode op = Opcode::Add;
    Type ty = isFloat ? Type(Type::Kind::F64) : Type(Type::Kind::I64);
    switch (b.op)
    {
        case BinaryExpr::Op::Add:
            op = isFloat ? Opcode::FAdd : Opcode::Add;
            break;
        case BinaryExpr::Op::Sub:
            op = isFloat ? Opcode::FSub : Opcode::Sub;
            break;
        case BinaryExpr::Op::Mul:
            op = isFloat ? Opcode::FMul : Opcode::Mul;
            break;
        case BinaryExpr::Op::Div:
            op = isFloat ? Opcode::FDiv : Opcode::SDiv;
            break;
        case BinaryExpr::Op::Eq:
            op = isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq;
            ty = Type(Type::Kind::I1);
            break;
        case BinaryExpr::Op::Ne:
            op = isFloat ? Opcode::FCmpNE : Opcode::ICmpNe;
            ty = Type(Type::Kind::I1);
            break;
        case BinaryExpr::Op::Lt:
            op = isFloat ? Opcode::FCmpLT : Opcode::SCmpLT;
            ty = Type(Type::Kind::I1);
            break;
        case BinaryExpr::Op::Le:
            op = isFloat ? Opcode::FCmpLE : Opcode::SCmpLE;
            ty = Type(Type::Kind::I1);
            break;
        case BinaryExpr::Op::Gt:
            op = isFloat ? Opcode::FCmpGT : Opcode::SCmpGT;
            ty = Type(Type::Kind::I1);
            break;
        case BinaryExpr::Op::Ge:
            op = isFloat ? Opcode::FCmpGE : Opcode::SCmpGE;
            ty = Type(Type::Kind::I1);
            break;
        default:
            break; // other ops handled elsewhere
    }
    Value res = emitBinary(op, ty, lhs.value, rhs.value);
    return {res, ty};
}

// Purpose: lower binary expr.
// Parameters: const BinaryExpr &b.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerBinaryExpr(const BinaryExpr &b)
{
    if (b.op == BinaryExpr::Op::And || b.op == BinaryExpr::Op::Or)
        return lowerLogicalBinary(b);
    if (b.op == BinaryExpr::Op::IDiv || b.op == BinaryExpr::Op::Mod)
        return lowerDivOrMod(b);

    RVal lhs = lowerExpr(*b.lhs);
    RVal rhs = lowerExpr(*b.rhs);
    if ((b.op == BinaryExpr::Op::Add || b.op == BinaryExpr::Op::Eq || b.op == BinaryExpr::Op::Ne) &&
        lhs.type.kind == Type::Kind::Str && rhs.type.kind == Type::Kind::Str)
        return lowerStringBinary(b, lhs, rhs);
    return lowerNumericBinary(b, lhs, rhs);
}

// Purpose: lower arg.
// Parameters: const BuiltinCallExpr &c, size_t idx.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerArg(const BuiltinCallExpr &c, size_t idx)
{
    assert(idx < c.args.size() && c.args[idx]);
    return lowerExpr(*c.args[idx]);
}

// Purpose: ensure i64.
// Parameters: RVal v, il::support::SourceLoc loc.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::ensureI64(RVal v, il::support::SourceLoc loc)
{
    if (v.type.kind == Type::Kind::I1)
    {
        curLoc = loc;
        v.value = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), v.value);
        v.type = Type(Type::Kind::I64);
    }
    else if (v.type.kind == Type::Kind::F64)
    {
        curLoc = loc;
        v.value = emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), v.value);
        v.type = Type(Type::Kind::I64);
    }
    return v;
}

// Purpose: ensure f64.
// Parameters: RVal v, il::support::SourceLoc loc.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::ensureF64(RVal v, il::support::SourceLoc loc)
{
    if (v.type.kind == Type::Kind::F64)
        return v;
    v = ensureI64(std::move(v), loc);
    if (v.type.kind == Type::Kind::I64)
    {
        curLoc = loc;
        v.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), v.value);
        v.type = Type(Type::Kind::F64);
    }
    return v;
}

// Purpose: lower rnd.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerRnd(const BuiltinCallExpr &c)
{
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_rnd", {});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower len.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerLen(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_len", {s.value});
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower mid.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerMid(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    RVal i = ensureI64(lowerArg(c, 1), c.loc);
    Value start0 = emitBinary(Opcode::Add, Type(Type::Kind::I64), i.value, Value::constInt(-1));
    curLoc = c.loc;
    if (c.args.size() >= 3 && c.args[2])
    {
        RVal n = ensureI64(lowerArg(c, 2), c.loc);
        Value res = emitCallRet(Type(Type::Kind::Str), "rt_mid3", {s.value, start0, n.value});
        needRtMid3 = true;
        return {res, Type(Type::Kind::Str)};
    }
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_mid2", {s.value, start0});
    needRtMid2 = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower left.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerLeft(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    RVal n = ensureI64(lowerArg(c, 1), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_left", {s.value, n.value});
    needRtLeft = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower right.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerRight(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    RVal n = ensureI64(lowerArg(c, 1), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_right", {s.value, n.value});
    needRtRight = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower str.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerStr(const BuiltinCallExpr &c)
{
    RVal v = lowerArg(c, 0);
    if (v.type.kind == Type::Kind::F64)
    {
        v = ensureF64(std::move(v), c.loc);
        curLoc = c.loc;
        Value res = emitCallRet(Type(Type::Kind::Str), "rt_f64_to_str", {v.value});
        return {res, Type(Type::Kind::Str)};
    }
    v = ensureI64(std::move(v), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_int_to_str", {v.value});
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower val.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerVal(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_to_int", {s.value});
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower int.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerInt(const BuiltinCallExpr &c)
{
    RVal f = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), f.value);
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower instr.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerInstr(const BuiltinCallExpr &c)
{
    curLoc = c.loc;
    if (c.args.size() >= 3 && c.args[0])
    {
        RVal start = ensureI64(lowerArg(c, 0), c.loc);
        Value start0 =
            emitBinary(Opcode::Add, Type(Type::Kind::I64), start.value, Value::constInt(-1));
        RVal hay = lowerArg(c, 1);
        RVal needle = lowerArg(c, 2);
        Value res =
            emitCallRet(Type(Type::Kind::I64), "rt_instr3", {start0, hay.value, needle.value});
        needRtInstr3 = true;
        return {res, Type(Type::Kind::I64)};
    }
    RVal hay = lowerArg(c, 0);
    RVal needle = lowerArg(c, 1);
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_instr2", {hay.value, needle.value});
    needRtInstr2 = true;
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower ltrim.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerLtrim(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_ltrim", {s.value});
    needRtLtrim = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower rtrim.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerRtrim(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_rtrim", {s.value});
    needRtRtrim = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower trim.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerTrim(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_trim", {s.value});
    needRtTrim = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower ucase.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerUcase(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_ucase", {s.value});
    needRtUcase = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower lcase.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerLcase(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_lcase", {s.value});
    needRtLcase = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower chr.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerChr(const BuiltinCallExpr &c)
{
    RVal code = ensureI64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::Str), "rt_chr", {code.value});
    needRtChr = true;
    return {res, Type(Type::Kind::Str)};
}

// Purpose: lower asc.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerAsc(const BuiltinCallExpr &c)
{
    RVal s = lowerArg(c, 0);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_asc", {s.value});
    needRtAsc = true;
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower sqr.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerSqr(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_sqrt", {v.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower abs.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerAbs(const BuiltinCallExpr &c)
{
    RVal v = lowerArg(c, 0);
    if (v.type.kind == Type::Kind::F64)
    {
        v = ensureF64(std::move(v), c.loc);
        curLoc = c.loc;
        Value res = emitCallRet(Type(Type::Kind::F64), "rt_abs_f64", {v.value});
        return {res, Type(Type::Kind::F64)};
    }
    v = ensureI64(std::move(v), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::I64), "rt_abs_i64", {v.value});
    return {res, Type(Type::Kind::I64)};
}

// Purpose: lower floor.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerFloor(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_floor", {v.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower ceil.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerCeil(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_ceil", {v.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower sin.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerSin(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_sin", {v.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower cos.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerCos(const BuiltinCallExpr &c)
{
    RVal v = ensureF64(lowerArg(c, 0), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_cos", {v.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower pow.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerPow(const BuiltinCallExpr &c)
{
    RVal a = ensureF64(lowerArg(c, 0), c.loc);
    RVal b = ensureF64(lowerArg(c, 1), c.loc);
    curLoc = c.loc;
    Value res = emitCallRet(Type(Type::Kind::F64), "rt_pow", {a.value, b.value});
    return {res, Type(Type::Kind::F64)};
}

// Purpose: lower builtin call.
// Parameters: const BuiltinCallExpr &c.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerBuiltinCall(const BuiltinCallExpr &c)
{
    using B = BuiltinCallExpr::Builtin;
    switch (c.builtin)
    {
        case B::Len:
            return lowerLen(c);
        case B::Mid:
            return lowerMid(c);
        case B::Left:
            return lowerLeft(c);
        case B::Right:
            return lowerRight(c);
        case B::Str:
            return lowerStr(c);
        case B::Val:
            return lowerVal(c);
        case B::Int:
            return lowerInt(c);
        case B::Instr:
            return lowerInstr(c);
        case B::Ltrim:
            return lowerLtrim(c);
        case B::Rtrim:
            return lowerRtrim(c);
        case B::Trim:
            return lowerTrim(c);
        case B::Ucase:
            return lowerUcase(c);
        case B::Lcase:
            return lowerLcase(c);
        case B::Chr:
            return lowerChr(c);
        case B::Asc:
            return lowerAsc(c);
        case B::Sqr:
            return lowerSqr(c);
        case B::Abs:
            return lowerAbs(c);
        case B::Floor:
            return lowerFloor(c);
        case B::Ceil:
            return lowerCeil(c);
        case B::Sin:
            return lowerSin(c);
        case B::Cos:
            return lowerCos(c);
        case B::Pow:
            return lowerPow(c);
        case B::Rnd:
            return lowerRnd(c);
    }
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

// Purpose: lower expr.
// Parameters: const Expr &expr.
// Returns: Lowerer::RVal.
// Side effects: may modify lowering state or emit IL.
Lowerer::RVal Lowerer::lowerExpr(const Expr &expr)
{
    curLoc = expr.loc;
    if (auto *i = dynamic_cast<const IntExpr *>(&expr))
    {
        return {Value::constInt(i->value), Type(Type::Kind::I64)};
    }
    else if (auto *f = dynamic_cast<const FloatExpr *>(&expr))
    {
        return {Value::constFloat(f->value), Type(Type::Kind::F64)};
    }
    else if (auto *s = dynamic_cast<const StringExpr *>(&expr))
    {
        std::string lbl = getStringLabel(s->value);
        Value tmp = emitConstStr(lbl);
        return {tmp, Type(Type::Kind::Str)};
    }
    else if (auto *v = dynamic_cast<const VarExpr *>(&expr))
    {
        return lowerVarExpr(*v);
    }
    else if (auto *u = dynamic_cast<const UnaryExpr *>(&expr))
    {
        return lowerUnaryExpr(*u);
    }
    else if (auto *b = dynamic_cast<const BinaryExpr *>(&expr))
    {
        return lowerBinaryExpr(*b);
    }

    else if (auto *c = dynamic_cast<const BuiltinCallExpr *>(&expr))
    {
        return lowerBuiltinCall(*c);
    }
    else if (auto *c = dynamic_cast<const CallExpr *>(&expr))
    {
        const Function *callee = nullptr;
        for (const auto &f : mod->functions)
            if (f.name == c->callee)
            {
                callee = &f;
                break;
            }
        std::vector<Value> args;
        for (size_t i = 0; i < c->args.size(); ++i)
        {
            RVal a = lowerExpr(*c->args[i]);
            if (callee && i < callee->params.size())
            {
                Type paramTy = callee->params[i].type;
                if (paramTy.kind == Type::Kind::F64 && a.type.kind == Type::Kind::I64)
                {
                    curLoc = expr.loc;
                    a.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), a.value);
                    a.type = Type(Type::Kind::F64);
                }
                else if (paramTy.kind == Type::Kind::F64 && a.type.kind == Type::Kind::I1)
                {
                    curLoc = expr.loc;
                    a.value = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), a.value);
                    a.value = emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), a.value);
                    a.type = Type(Type::Kind::F64);
                }
                else if (paramTy.kind == Type::Kind::I64 && a.type.kind == Type::Kind::I1)
                {
                    curLoc = expr.loc;
                    a.value = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), a.value);
                    a.type = Type(Type::Kind::I64);
                }
            }
            args.push_back(a.value);
        }
        curLoc = expr.loc;
        if (callee && callee->retType.kind != Type::Kind::Void)
        {
            Value res = emitCallRet(callee->retType, c->callee, args);
            return {res, callee->retType};
        }
        emitCall(c->callee, args);
        return {Value::constInt(0), Type(Type::Kind::I64)};
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
        if (!isStr && v.type.kind == Type::Kind::I1)
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
        else if (!isStr && !isF64 && v.type.kind == Type::Kind::F64)
        {
            curLoc = stmt.loc;
            v.value = emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), v.value);
            v.type = Type(Type::Kind::I64);
        }
        curLoc = stmt.loc;
        Type ty =
            isStr ? Type(Type::Kind::Str) : (isF64 ? Type(Type::Kind::F64) : Type(Type::Kind::I64));
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
