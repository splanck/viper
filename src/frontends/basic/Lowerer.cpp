// File: src/frontends/basic/Lowerer.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Lowers BASIC AST to IL with control-flow helpers and centralized
//          runtime declarations.
// Key invariants: Block names inside procedures are deterministic via BlockNamer.
// Ownership/Lifetime: Uses contexts managed externally.
// Links: docs/class-catalog.md

#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <cassert>
#include <functional>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

namespace
{

/// @brief Translate a BASIC AST scalar type into the IL core representation.
/// @param ty BASIC semantic type sourced from the front-end AST.
/// @return Corresponding IL type used for stack slots and temporaries.
/// @note BOOL lowers to the IL `i1` type while string scalars lower to the
///       canonical string handle type. Unrecognized enumerators fall back to
///       I64, although the parser should prevent that from happening.
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

Lowerer::BlockNamer::BlockNamer(std::string p) : proc(std::move(p)) {}

std::string Lowerer::BlockNamer::entry() const
{
    return "entry_" + proc;
}

std::string Lowerer::BlockNamer::ret() const
{
    return "ret_" + proc;
}

std::string Lowerer::BlockNamer::line(int line) const
{
    return "L" + std::to_string(line) + "_" + proc;
}

unsigned Lowerer::BlockNamer::nextIf()
{
    return ifCounter++;
}

std::string Lowerer::BlockNamer::ifTest(unsigned id) const
{
    return "if_test_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::ifThen(unsigned id) const
{
    return "if_then_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::ifElse(unsigned id) const
{
    return "if_else_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::ifEnd(unsigned id) const
{
    return "if_end_" + std::to_string(id) + "_" + proc;
}

unsigned Lowerer::BlockNamer::nextWhile()
{
    return loopCounter++;
}

std::string Lowerer::BlockNamer::whileHead(unsigned id) const
{
    return "while_head_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::whileBody(unsigned id) const
{
    return "while_body_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::whileEnd(unsigned id) const
{
    return "while_end_" + std::to_string(id) + "_" + proc;
}

unsigned Lowerer::BlockNamer::nextFor()
{
    return loopCounter++;
}

unsigned Lowerer::BlockNamer::nextCall()
{
    return loopCounter++;
}

std::string Lowerer::BlockNamer::forHead(unsigned id) const
{
    return "for_head_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::forBody(unsigned id) const
{
    return "for_body_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::forInc(unsigned id) const
{
    return "for_inc_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::forEnd(unsigned id) const
{
    return "for_end_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::callCont(unsigned id) const
{
    return "call_cont_" + std::to_string(id) + "_" + proc;
}

std::string Lowerer::BlockNamer::generic(const std::string &hint)
{
    auto &n = genericCounters[hint];
    std::string label = hint + "_" + std::to_string(n++) + "_" + proc;
    return label;
}

std::string Lowerer::BlockNamer::tag(const std::string &base) const
{
    return base + "_" + proc;
}

/// @brief Construct a lowering context.
/// @param boundsChecks When true, enable allocation of auxiliary slots used to
///        emit runtime array bounds checks during lowering.
/// @note The constructor merely stores configuration; transient lowering state
///       is reset each time a program or procedure is processed.
Lowerer::Lowerer(bool boundsChecks) : boundsChecks(boundsChecks) {}

/// @brief Lower a full BASIC program into an IL module.
/// @param prog Parsed program containing procedures and top-level statements.
/// @return Newly constructed module with all runtime declarations and lowered
///         procedures.
/// @details The method resets every per-run cache (name mangler, variable
///          tracking, runtime requirements) and performs a three stage pipeline:
///          (1) scan to identify runtime helpers, (2) declare those helpers in
///          the module, and (3) emit procedure bodies plus a synthetic @main.
///          `mod`, `builder`, and numerous tracking maps are updated in-place
///          while the temporary `Module m` owns the resulting IR.
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

/// @brief Backward-compatible alias for @ref lowerProgram.
/// @param prog Program lowered into a fresh module instance.
/// @return Result from delegating to @ref lowerProgram.
Module Lowerer::lower(const Program &prog)
{
    return lowerProgram(prog);
}

/// @brief Discover variable usage within a statement list.
/// @param stmts Statements whose expressions are analyzed.
/// @details Populates `vars`, `arrays`, and `varTypes` so subsequent lowering can
///          materialize storage for every name. Array metadata is captured so
///          bounds slots can be emitted when enabled. The traversal preserves
///          existing set entries, allowing incremental accumulation across
///          different program regions.
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

/// @brief Lower a single BASIC procedure using the provided configuration.
/// @param name Symbol name used for both mangling and emitted function labels.
/// @param params Formal parameters describing incoming values.
/// @param body Statements comprising the procedure body.
/// @param config Callbacks and metadata controlling return emission and
///        post-collection bookkeeping.
/// @details Clears any state from prior procedures, collects variable
///          references from @p body, and then constructs the IL function
///          skeleton: entry block, per-line blocks, and exit block. Parameter
///          and local stack slots are materialized before walking statements.
///          The helper drives `lowerStmt` for each statement and finally invokes
///          the configured return generator. Numerous members (`func`, `cur`,
///          `lineBlocks`, `varSlots`, `arrays`, `blockNamer`) are mutated to
///          reflect the active procedure.
void Lowerer::lowerProcedure(const std::string &name,
                             const std::vector<Param> &params,
                             const std::vector<StmtPtr> &body,
                             const ProcedureConfig &config)
{
    resetLoweringState();

    std::vector<const Stmt *> bodyPtrs;
    bodyPtrs.reserve(body.size());
    for (const auto &stmt : body)
        bodyPtrs.push_back(stmt.get());
    collectVars(bodyPtrs);

    if (config.postCollect)
        config.postCollect();

    std::unordered_set<std::string> paramNames;
    std::vector<il::core::Param> irParams;
    irParams.reserve(params.size());
    for (const auto &p : params)
    {
        paramNames.insert(p.name);
        Type ty = p.is_array ? Type(Type::Kind::Ptr) : coreTypeForAstType(p.type);
        irParams.push_back({p.name, ty});
    }

    assert(config.emitEmptyBody && "Missing empty body return handler");
    assert(config.emitFinalReturn && "Missing final return handler");
    if (!config.emitEmptyBody || !config.emitFinalReturn)
        return;

    Function &f = builder->startFunction(name, config.retType, irParams);
    func = &f;

    blockNamer = std::make_unique<BlockNamer>(name);

    builder->addBlock(f, blockNamer->entry());

    for (size_t i = 0; i < params.size(); ++i)
        mangler.nextTemp();

    std::vector<int> lines;
    lines.reserve(body.size());
    for (const auto &stmt : body)
    {
        if (blockNamer)
            builder->addBlock(f, blockNamer->line(stmt->line));
        else
            builder->addBlock(f, mangler.block("L" + std::to_string(stmt->line) + "_" + name));
        lines.push_back(stmt->line);
    }
    fnExit = f.blocks.size();
    if (blockNamer)
        builder->addBlock(f, blockNamer->ret());
    else
        builder->addBlock(f, mangler.block("ret_" + name));

    for (size_t i = 0; i < lines.size(); ++i)
        lineBlocks[lines[i]] = i + 1;

    cur = &f.blocks.front();
    materializeParams(params);

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

    if (body.empty())
    {
        curLoc = {};
        config.emitEmptyBody();
        blockNamer.reset();
        return;
    }

    curLoc = {};
    emitBr(&f.blocks[lineBlocks[body.front()->line]]);

    for (size_t i = 0; i < body.size(); ++i)
    {
        cur = &f.blocks[lineBlocks[body[i]->line]];
        lowerStmt(*body[i]);
        if (cur->terminated)
            break;
        BasicBlock *next = (i + 1 < body.size())
                               ? &f.blocks[lineBlocks[body[i + 1]->line]]
                               : &f.blocks[fnExit];
        emitBr(next);
    }

    cur = &f.blocks[fnExit];
    curLoc = {};
    config.emitFinalReturn();

    blockNamer.reset();
}

/// @brief Lower a FUNCTION declaration into an IL function definition.
/// @param decl BASIC FUNCTION metadata and body.
/// @details Configures a @ref ProcedureConfig that materializes default return
///          values when the body falls through. The function result type is
///          cached in `varTypes` so the caller may bind its slot when invoking
///          the function.
void Lowerer::lowerFunctionDecl(const FunctionDecl &decl)
{
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

    ProcedureConfig config;
    config.retType = coreTypeForAstType(decl.ret);
    config.postCollect = [&]() { varTypes[decl.name] = decl.ret; };
    config.emitEmptyBody = [&]() { emitRet(defaultRet()); };
    config.emitFinalReturn = [&]() { emitRet(defaultRet()); };

    lowerProcedure(decl.name, decl.params, decl.body, config);
}

/// @brief Lower a SUB declaration into an IL procedure.
/// @param decl BASIC SUB metadata and body.
/// @details SUBs have no return value; the configured lowering emits void
///          returns for empty bodies and at the exit block.
void Lowerer::lowerSubDecl(const SubDecl &decl)
{
    ProcedureConfig config;
    config.retType = Type(Type::Kind::Void);
    config.emitEmptyBody = [&]() { emitRetVoid(); };
    config.emitFinalReturn = [&]() { emitRetVoid(); };

    lowerProcedure(decl.name, decl.params, decl.body, config);
}

/// @brief Clear per-procedure lowering caches.
/// @details Invoked before lowering each procedure to drop variable maps, array
///          bookkeeping, and line-to-block mappings. The bounds-check counter is
///          reset so diagnostics and helper labels remain deterministic.
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

/// @brief Allocate stack storage for incoming parameters and record their types.
/// @param params BASIC formal parameters for the current procedure.
/// @details Emits an alloca per parameter and stores the incoming SSA value into
///          the slot. Array parameters are remembered in `arrays` to avoid
///          copying the referenced buffer, while boolean parameters request a
///          single-byte allocation. Side effects update `varSlots`, `varTypes`,
///          and potentially `arrays`.
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

/// @brief Collect variable usage for every procedure and the main body.
/// @param prog Program whose statements are scanned.
/// @details Aggregates pointers to all statements and forwards to the granular
///          collector so that `vars` and `arrays` contain every referenced name
///          before lowering begins.
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
