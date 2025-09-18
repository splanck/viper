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
#include <cassert>
#include <functional>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

namespace
{

il::core::Type coreTypeForAstType(::il::frontends::basic::Type ty)
{
    using il::core::Type;
    switch (ty)
    {
        case ::il::frontends::basic::Type::I64:
            return Type(Type::Kind::I64);
        case ::il::frontends::basic::Type::F64:
            return Type(Type::Kind::F64);
        case ::il::frontends::basic::Type::Str:
            return Type(Type::Kind::Str);
        case ::il::frontends::basic::Type::Bool:
            return Type(Type::Kind::I1);
    }
    return Type(Type::Kind::I64);
}

} // namespace

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
    varTypes.clear();
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
            varTypes[d->name] = d->type;
            if (d->isArray)
            {
                arrays.insert(d->name);
                if (d->size)
                    ex(*d->size);
            }
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

    std::vector<const Stmt *> bodyPtrs;
    for (const auto &s : decl.body)
        bodyPtrs.push_back(s.get());
    collectVars(bodyPtrs);
    varTypes[decl.name] = decl.ret;

    std::unordered_set<std::string> paramNames;
    std::vector<il::core::Param> params;
    for (const auto &p : decl.params)
    {
        paramNames.insert(p.name);
        il::core::Type ty =
            p.is_array ? il::core::Type(il::core::Type::Kind::Ptr) : coreTypeForAstType(p.type);
        params.push_back({p.name, ty});
    }

    il::core::Type retTy = coreTypeForAstType(decl.ret);

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
        if (arrays.count(v))
        {
            Value slot = emitAlloca(8);
            varSlots[v] = slot.id;
            continue;
        }
        bool isBoolVar = false;
        auto itType = varTypes.find(v);
        if (itType != varTypes.end() && itType->second == AstType::Bool)
            isBoolVar = true;
        Value slot = emitAlloca(isBoolVar ? 1 : 8);
        varSlots[v] = slot.id;
        if (isBoolVar)
            emitStore(ilBoolTy(), slot, emitBoolConst(false));
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
            case ::il::frontends::basic::Type::I64:
                return Value::constInt(0);
            case ::il::frontends::basic::Type::F64:
                return Value::constFloat(0.0);
            case ::il::frontends::basic::Type::Str:
                return emitConstStr(getStringLabel(""));
            case ::il::frontends::basic::Type::Bool:
                return emitBoolConst(false);
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
            p.is_array ? il::core::Type(il::core::Type::Kind::Ptr) : coreTypeForAstType(p.type);
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
        if (arrays.count(v))
        {
            Value slot = emitAlloca(8);
            varSlots[v] = slot.id;
            continue;
        }
        bool isBoolVar = false;
        auto itType = varTypes.find(v);
        if (itType != varTypes.end() && itType->second == AstType::Bool)
            isBoolVar = true;
        Value slot = emitAlloca(isBoolVar ? 1 : 8);
        varSlots[v] = slot.id;
        if (isBoolVar)
            emitStore(ilBoolTy(), slot, emitBoolConst(false));
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
    varTypes.clear();
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
        bool isBoolParam = !p.is_array && p.type == AstType::Bool;
        Value slot = emitAlloca(isBoolParam ? 1 : 8);
        varSlots[p.name] = slot.id;
        varTypes[p.name] = p.type;
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


} // namespace il::frontends::basic
