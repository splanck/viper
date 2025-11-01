//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Proc_Locals.cpp
// Purpose: Hosts helpers that manage symbol discovery, metadata collection, and
//          stack allocation for BASIC procedure lowering.
// Key invariants: Helpers mutate symbol state owned by Lowerer while preserving
//                 default BASIC typing semantics.
// Ownership/Lifetime: All helpers borrow state from Lowerer/ProcedureLowering
//                     and never allocate persistent resources.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/TypeSuffix.hpp"

#include "il/core/Function.hpp"
#include "il/core/Type.hpp"

#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace viper::basic::lower::locals
{

using il::frontends::basic::ArrayExpr;
using il::frontends::basic::DimStmt;
using il::frontends::basic::ForStmt;
using il::frontends::basic::FunctionDecl;
using il::frontends::basic::InputStmt;
using il::frontends::basic::LBoundExpr;
using il::frontends::basic::Lowerer;
using il::frontends::basic::Param;
using il::frontends::basic::ProcedureLowering;
using il::frontends::basic::Program;
using il::frontends::basic::ReDimStmt;
using il::frontends::basic::Stmt;
using il::frontends::basic::StmtPtr;
using il::frontends::basic::SubDecl;
using il::frontends::basic::UBoundExpr;
using il::frontends::basic::VarExpr;
using il::frontends::basic::pipeline_detail::coreTypeForAstType;
using il::frontends::basic::inferAstTypeFromName;
using il::frontends::basic::Type;

struct Access
{
    static il::frontends::basic::Lowerer::SymbolInfo &ensureSymbol(il::frontends::basic::Lowerer &lowerer,
                                                                  std::string_view name);
    static il::frontends::basic::Lowerer::SymbolInfo *findSymbol(il::frontends::basic::Lowerer &lowerer,
                                                                std::string_view name);
    static const il::frontends::basic::Lowerer::SymbolInfo *findSymbol(const il::frontends::basic::Lowerer &lowerer,
                                                                      std::string_view name);
    static void setSymbolType(il::frontends::basic::Lowerer &lowerer, std::string_view name, Type type);
    static void setSymbolObjectType(il::frontends::basic::Lowerer &lowerer,
                                    std::string_view name,
                                    std::string className);
    static void markSymbolReferenced(il::frontends::basic::Lowerer &lowerer, std::string_view name);
    static void markArray(il::frontends::basic::Lowerer &lowerer, std::string_view name);
    static void resetSymbolState(il::frontends::basic::Lowerer &lowerer);
    static il::frontends::basic::Lowerer::SlotType getSlotType(const il::frontends::basic::Lowerer &lowerer,
                                                              std::string_view name);
    static void collectVars(il::frontends::basic::ProcedureLowering &self,
                            const std::vector<const il::frontends::basic::Stmt *> &stmts);
    static void collectVars(il::frontends::basic::ProcedureLowering &self,
                            const il::frontends::basic::Program &prog);
    static il::frontends::basic::Lowerer::ProcedureMetadata
    collectProcedureMetadata(il::frontends::basic::Lowerer &lowerer,
                             const std::vector<il::frontends::basic::Param> &params,
                             const std::vector<il::frontends::basic::StmtPtr> &body,
                             const il::frontends::basic::Lowerer::ProcedureConfig &config);
    static void allocateLocalSlots(il::frontends::basic::Lowerer &lowerer,
                                   const std::unordered_set<std::string> &paramNames,
                                   bool includeParams);
    static void materializeParams(il::frontends::basic::Lowerer &lowerer,
                                  const std::vector<il::frontends::basic::Param> &params);
};

namespace
{

class VarCollectWalker final : public il::frontends::basic::BasicAstWalker<VarCollectWalker>
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

    void before(const il::frontends::basic::NextStmt &stmt)
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

Lowerer::SymbolInfo &Access::ensureSymbol(Lowerer &lowerer, std::string_view name)
{
    std::string key(name);
    auto [it, inserted] = lowerer.symbols.emplace(std::move(key), Lowerer::SymbolInfo{});
    if (inserted)
    {
        auto &info = it->second;
        info.type = Type::I64;
        info.hasType = false;
        info.isArray = false;
        info.isBoolean = false;
        info.referenced = false;
        info.isObject = false;
        info.objectClass.clear();
    }
    return it->second;
}

Lowerer::SymbolInfo *Access::findSymbol(Lowerer &lowerer, std::string_view name)
{
    auto it = lowerer.symbols.find(std::string(name));
    if (it == lowerer.symbols.end())
        return nullptr;
    return &it->second;
}

const Lowerer::SymbolInfo *Access::findSymbol(const Lowerer &lowerer, std::string_view name)
{
    auto it = lowerer.symbols.find(std::string(name));
    if (it == lowerer.symbols.end())
        return nullptr;
    return &it->second;
}

void Access::setSymbolType(Lowerer &lowerer, std::string_view name, Type type)
{
    auto &info = ensureSymbol(lowerer, name);
    info.type = type;
    info.hasType = true;
    info.isBoolean = !info.isArray && type == Type::Bool;
}

void Access::setSymbolObjectType(Lowerer &lowerer, std::string_view name, std::string className)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(lowerer, name);
    info.isObject = true;
    info.objectClass = std::move(className);
    info.hasType = true;
}

void Access::markSymbolReferenced(Lowerer &lowerer, std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(lowerer, name);
    if (!info.hasType)
    {
        info.type = inferAstTypeFromName(name);
        info.hasType = true;
        info.isBoolean = !info.isArray && info.type == Type::Bool;
    }
    info.referenced = true;
}

void Access::markArray(Lowerer &lowerer, std::string_view name)
{
    if (name.empty())
        return;
    auto &info = ensureSymbol(lowerer, name);
    info.isArray = true;
    if (info.isBoolean)
        info.isBoolean = false;
}

void Access::resetSymbolState(Lowerer &lowerer)
{
    for (auto it = lowerer.symbols.begin(); it != lowerer.symbols.end();)
    {
        auto &info = it->second;
        if (!info.stringLabel.empty())
        {
            info.type = Type::I64;
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
        it = lowerer.symbols.erase(it);
    }
}

Lowerer::SlotType Access::getSlotType(const Lowerer &lowerer, std::string_view name)
{
    Lowerer::SlotType slot;
    Type astTy = inferAstTypeFromName(name);
    if (const auto *sym = findSymbol(lowerer, name))
    {
        if (sym->isObject)
        {
            slot.type = il::core::Type(il::core::Type::Kind::Ptr);
            slot.isArray = false;
            slot.isBoolean = false;
            slot.isObject = true;
            slot.objectClass = sym->objectClass;
            return slot;
        }
        if (sym->hasType)
            astTy = sym->type;
        slot.isArray = sym->isArray;
        if (sym->isBoolean && !slot.isArray)
            slot.isBoolean = true;
        else if (!sym->hasType && !slot.isArray)
            slot.isBoolean = (astTy == Type::Bool);
        else
            slot.isBoolean = false;
    }
    else
    {
        slot.isArray = false;
        slot.isBoolean = (astTy == Type::Bool);
    }

    if (slot.isArray)
        slot.type = il::core::Type(il::core::Type::Kind::Ptr);
    else
        slot.type = coreTypeForAstType(slot.isBoolean ? Type::Bool : astTy);
    return slot;
}

void Access::collectVars(ProcedureLowering &self, const std::vector<const Stmt *> &stmts)
{
    Lowerer &lowerer = self.lowerer;
    VarCollectWalker walker(lowerer);
    for (const auto *stmt : stmts)
        if (stmt)
            walker.walkStmt(*stmt);
}

void Access::collectVars(ProcedureLowering &self, const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    ptrs.reserve(prog.procs.size() + prog.main.size());
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(self, ptrs);
}

Lowerer::ProcedureMetadata Access::collectProcedureMetadata(Lowerer &lowerer,
                                                            const std::vector<Param> &params,
                                                            const std::vector<StmtPtr> &body,
                                                            const Lowerer::ProcedureConfig &config)
{
    Lowerer::ProcedureMetadata metadata;
    metadata.paramCount = params.size();
    metadata.bodyStmts.reserve(body.size());
    for (const auto &stmt : body)
        metadata.bodyStmts.push_back(stmt.get());

    if (lowerer.procedureLowering)
        collectVars(*lowerer.procedureLowering, metadata.bodyStmts);

    if (config.postCollect)
        config.postCollect();

    metadata.irParams.reserve(params.size());
    for (const auto &p : params)
    {
        metadata.paramNames.insert(p.name);
        il::core::Type ty = p.is_array ? il::core::Type(il::core::Type::Kind::Ptr)
                                       : coreTypeForAstType(p.type);
        metadata.irParams.push_back({p.name, ty});
        if (p.is_array)
        {
            lowerer.requireArrayI32Retain();
            lowerer.requireArrayI32Release();
        }
    }

    return metadata;
}

void Access::allocateLocalSlots(Lowerer &lowerer,
                                const std::unordered_set<std::string> &paramNames,
                                bool includeParams)
{
    for (auto &[name, info] : lowerer.symbols)
    {
        if (!info.referenced)
            continue;
        bool isParam = paramNames.find(name) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (info.slotId)
            continue;
        lowerer.curLoc = {};
        auto slotInfo = getSlotType(lowerer, name);
        if (slotInfo.isArray)
        {
            il::core::Value slot = lowerer.emitAlloca(8);
            info.slotId = slot.id;
            lowerer.emitStore(il::core::Type(il::core::Type::Kind::Ptr), slot, il::core::Value::null());
            continue;
        }
        il::core::Value slot = lowerer.emitAlloca(slotInfo.isBoolean ? 1 : 8);
        info.slotId = slot.id;
        if (slotInfo.isBoolean)
        {
            lowerer.emitStore(lowerer.ilBoolTy(), slot, lowerer.emitBoolConst(false));
        }
        else if (slotInfo.type.kind == il::core::Type::Kind::Str)
        {
            il::core::Value empty = lowerer.emitCallRet(slotInfo.type, "rt_str_empty", {});
            lowerer.emitStore(slotInfo.type, slot, empty);
        }
    }

    if (!lowerer.boundsChecks)
        return;

    for (auto &[name, info] : lowerer.symbols)
    {
        if (!info.referenced || !info.isArray)
            continue;
        bool isParam = paramNames.find(name) != paramNames.end();
        if (isParam && !includeParams)
            continue;
        if (info.arrayLengthSlot)
            continue;
        lowerer.curLoc = {};
        il::core::Value slot = lowerer.emitAlloca(8);
        info.arrayLengthSlot = slot.id;
    }
}

void Access::materializeParams(Lowerer &lowerer, const std::vector<Param> &params)
{
    auto &procCtx = lowerer.context();
    auto *func = procCtx.function();
    if (!func)
        return;

    for (std::size_t i = 0; i < params.size(); ++i)
    {
        const auto &p = params[i];
        bool isBoolParam = !p.is_array && p.type == Type::Bool;
        lowerer.curLoc = {};
        il::core::Value slot = lowerer.emitAlloca(isBoolParam ? 1 : 8);
        if (p.is_array)
        {
            markArray(lowerer, p.name);
            lowerer.emitStore(il::core::Type(il::core::Type::Kind::Ptr), slot, il::core::Value::null());
        }
        setSymbolType(lowerer, p.name, p.type);
        markSymbolReferenced(lowerer, p.name);
        auto &info = ensureSymbol(lowerer, p.name);
        info.slotId = slot.id;
        il::core::Type ty = func->params[i].type;
        il::core::Value incoming = il::core::Value::temp(func->params[i].id);
        if (p.is_array)
        {
            lowerer.storeArray(slot, incoming);
        }
        else
        {
            lowerer.emitStore(ty, slot, incoming);
        }
    }
}

} // namespace viper::basic::lower::locals
