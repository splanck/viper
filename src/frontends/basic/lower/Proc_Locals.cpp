//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Proc_Locals.cpp
// Purpose: Implements procedure-local symbol tracking and metadata gathering
//          utilities used by BASIC lowering.
// Key invariants: Symbol table entries are created on demand and remain scoped
//                 to the active lowering run; boolean flags are cleared when
//                 array semantics apply.
// Ownership/Lifetime: Operates on Lowerer-owned symbol tables and context
//                     without taking ownership of AST nodes.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include <memory>
#include <unordered_set>
#include <utility>

namespace viper::basic::lower::locals
{

void prepare(::il::frontends::basic::ProcedureLowering &lowering,
             ::il::frontends::basic::ProcedureLowering::LoweringContext &ctx)
{
    lowering.resetContext(ctx);
    lowering.collectProcedureInfo(ctx);
}

} // namespace viper::basic::lower::locals

using namespace il::core;

namespace il::frontends::basic
{

namespace
{

class VarCollectWalker final : public BasicAstWalker<VarCollectWalker>
{
  public:
    explicit VarCollectWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void after(const VarExpr &expr)
    {
        if (!expr.name.empty())
            lowerer_.markSymbolReferenced(expr.name);
    }

    void after(const ArrayExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    void before(const DimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
            lowerer_.markArray(stmt.name);
    }

    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
    }

    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    void before(const NextStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    void before(const InputStmt &stmt)
    {
        for (const auto &name : stmt.vars)
        {
            if (!name.empty())
                lowerer_.markSymbolReferenced(name);
        }
    }

  private:
    Lowerer &lowerer_;
};

} // namespace

Lowerer::SymbolInfo &Lowerer::ensureSymbol(std::string_view name)
{
    std::string key(name);
    auto [it, inserted] = symbols.emplace(std::move(key), SymbolInfo{});
    if (inserted)
    {
        it->second.type = AstType::I64;
        it->second.hasType = false;
        it->second.isArray = false;
        it->second.isBoolean = false;
        it->second.referenced = false;
        it->second.isObject = false;
        it->second.objectClass.clear();
    }
    return it->second;
}

Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name)
{
    auto it = symbols.find(std::string(name));
    if (it == symbols.end())
        return nullptr;
    return &it->second;
}

const Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name) const
{
    auto it = symbols.find(std::string(name));
    if (it == symbols.end())
        return nullptr;
    return &it->second;
}

void Lowerer::setSymbolType(std::string_view name, AstType type)
{
    auto &info = ensureSymbol(name);
    info.type = type;
    info.hasType = true;
    info.isBoolean = !info.isArray && type == AstType::Bool;
}

void Lowerer::setSymbolObjectType(std::string_view name, std::string className)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isObject = true;
    info.objectClass = std::move(className);
    info.hasType = true;
}

void Lowerer::markSymbolReferenced(std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    if (!info.hasType)
    {
        info.type = inferAstTypeFromName(name);
        info.hasType = true;
        info.isBoolean = !info.isArray && info.type == AstType::Bool;
    }
    info.referenced = true;
}

void Lowerer::markArray(std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(name);
    info.isArray = true;
    if (info.isBoolean)
        info.isBoolean = false;
}

void Lowerer::resetSymbolState()
{
    for (auto it = symbols.begin(); it != symbols.end();)
    {
        SymbolInfo &info = it->second;
        if (!info.stringLabel.empty())
        {
            info.type = AstType::I64;
            info.hasType = false;
            info.isArray = false;
            info.isBoolean = false;
            info.referenced = false;
            info.isObject = false;
            info.objectClass.clear();
            info.slotId.reset();
            info.arrayLengthSlot.reset();
            ++it;
            continue;
        }
        it = symbols.erase(it);
    }
}

Lowerer::SlotType Lowerer::getSlotType(std::string_view name) const
{
    SlotType info;
    AstType astTy = inferAstTypeFromName(name);
    if (const auto *sym = findSymbol(name))
    {
        if (sym->isObject)
        {
            info.type = Type(Type::Kind::Ptr);
            info.isArray = false;
            info.isBoolean = false;
            info.isObject = true;
            info.objectClass = sym->objectClass;
            return info;
        }
        if (sym->hasType)
            astTy = sym->type;
        info.isArray = sym->isArray;
        if (sym->isBoolean && !info.isArray)
            info.isBoolean = true;
        else if (!sym->hasType && !info.isArray)
            info.isBoolean = (astTy == AstType::Bool);
        else
            info.isBoolean = false;
    }
    else
    {
        info.isArray = false;
        info.isBoolean = (astTy == AstType::Bool);
    }
    if (info.isArray)
        info.type = Type(Type::Kind::Ptr);
    else
        info.type = pipeline_detail::coreTypeForAstType(info.isBoolean ? AstType::Bool : astTy);
    return info;
}

void Lowerer::resetLoweringState()
{
    resetSymbolState();
    context().reset();
    stmtVirtualLines_.clear();
    synthSeq_ = 0;
}

void ProcedureLowering::resetContext(LoweringContext &ctx)
{
    lowerer.resetLoweringState();
    (void)ctx;
}

void ProcedureLowering::collectProcedureInfo(LoweringContext &ctx)
{
    auto metadata = std::make_shared<Lowerer::ProcedureMetadata>(
        lowerer.collectProcedureMetadata(ctx.params, ctx.body, ctx.config));
    ctx.metadata = metadata;
    ctx.paramCount = metadata->paramCount;
    ctx.bodyStmts = metadata->bodyStmts;
    ctx.paramNames = metadata->paramNames;
    ctx.irParams = metadata->irParams;
}

Lowerer::ProcedureMetadata Lowerer::collectProcedureMetadata(const std::vector<Param> &params,
                                                             const std::vector<StmtPtr> &body,
                                                             const ProcedureConfig &config)
{
    ProcedureMetadata metadata;
    metadata.paramCount = params.size();
    metadata.bodyStmts.reserve(body.size());
    for (const auto &stmt : body)
        metadata.bodyStmts.push_back(stmt.get());

    collectVars(metadata.bodyStmts);

    if (config.postCollect)
        config.postCollect();

    metadata.irParams.reserve(params.size());
    for (const auto &p : params)
    {
        metadata.paramNames.insert(p.name);
        Type ty = p.is_array ? Type(Type::Kind::Ptr) : pipeline_detail::coreTypeForAstType(p.type);
        metadata.irParams.push_back({p.name, ty});
        if (p.is_array)
        {
            requireArrayI32Retain();
            requireArrayI32Release();
        }
    }

    return metadata;
}

void ProcedureLowering::collectVars(const std::vector<const Stmt *> &stmts)
{
    VarCollectWalker walker(lowerer);
    for (const auto *stmt : stmts)
        if (stmt)
            walker.walkStmt(*stmt);
}

void ProcedureLowering::collectVars(const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(ptrs);
}

void Lowerer::collectVars(const Program &prog)
{
    procedureLowering->collectVars(prog);
}

void Lowerer::collectVars(const std::vector<const Stmt *> &stmts)
{
    procedureLowering->collectVars(stmts);
}

void Lowerer::allocateLocalSlots(const std::unordered_set<std::string> &paramNames, bool includeParams)
{
    for (auto &[name, info] : symbols)
    {
        if (!info.referenced)
            continue;
        bool isParam = paramNames.find(name) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (info.slotId)
            continue;
        curLoc = {};
        SlotType slotInfo = getSlotType(name);
        if (slotInfo.isArray)
        {
            Value slot = emitAlloca(8);
            info.slotId = slot.id;
            emitStore(Type(Type::Kind::Ptr), slot, Value::null());
            continue;
        }
        Value slot = emitAlloca(slotInfo.isBoolean ? 1 : 8);
        info.slotId = slot.id;
        if (slotInfo.isBoolean)
            emitStore(ilBoolTy(), slot, emitBoolConst(false));
        else if (slotInfo.type.kind == Type::Kind::Str)
        {
            Value empty = emitCallRet(slotInfo.type, "rt_str_empty", {});
            emitStore(slotInfo.type, slot, empty);
        }
    }

    if (!boundsChecks)
        return;

    for (auto &[name, info] : symbols)
    {
        if (!info.referenced || !info.isArray)
            continue;
        bool isParam = paramNames.find(name) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (info.arrayLengthSlot)
            continue;
        curLoc = {};
        Value slot = emitAlloca(8);
        info.arrayLengthSlot = slot.id;
    }
}

} // namespace il::frontends::basic
