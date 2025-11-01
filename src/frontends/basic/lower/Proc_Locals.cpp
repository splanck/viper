//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Proc_Locals.cpp
// Purpose: Implement symbol discovery and local slot management helpers used by
//          the BASIC procedure lowering pipeline.
// Key invariants: Symbol metadata is reset between procedures and array/object
//                 markers remain in sync with the allocation strategy.
// Ownership/Lifetime: Operates on the owning Lowerer instance without taking
//                     ownership of AST nodes or runtime handles.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/LoweringPipeline.hpp"
#include "frontends/basic/TypeSuffix.hpp"

#include <cassert>
#include <utility>
#include <vector>

using namespace il::core;

namespace viper::basic::lower::locals
{

using il::frontends::basic::ArrayExpr;
using il::frontends::basic::BasicAstWalker;
using il::frontends::basic::DimStmt;
using il::frontends::basic::ForStmt;
using il::frontends::basic::InputStmt;
using il::frontends::basic::LBoundExpr;
using il::frontends::basic::Lowerer;
using il::frontends::basic::NextStmt;
using il::frontends::basic::Param;
using il::frontends::basic::ProcedureLowering;
using il::frontends::basic::Program;
using il::frontends::basic::ReDimStmt;
using il::frontends::basic::Stmt;
using il::frontends::basic::StmtPtr;
using il::frontends::basic::UBoundExpr;
using il::frontends::basic::VarExpr;
using il::frontends::basic::pipeline_detail::coreTypeForAstType;
using il::frontends::basic::inferAstTypeFromName;

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

struct API
{
    static Lowerer::SymbolInfo &ensureSymbol(Lowerer &lowerer, std::string_view name)
    {
        std::string key(name);
        auto [it, inserted] = lowerer.symbols.emplace(std::move(key), Lowerer::SymbolInfo{});
        if (inserted)
        {
            it->second.type = Lowerer::AstType::I64;
            it->second.hasType = false;
            it->second.isArray = false;
            it->second.isBoolean = false;
            it->second.referenced = false;
            it->second.isObject = false;
            it->second.objectClass.clear();
        }
        return it->second;
    }

    static Lowerer::SymbolInfo *findSymbol(Lowerer &lowerer, std::string_view name)
    {
        auto it = lowerer.symbols.find(std::string(name));
        if (it == lowerer.symbols.end())
            return nullptr;
        return &it->second;
    }

    static const Lowerer::SymbolInfo *findSymbol(const Lowerer &lowerer, std::string_view name)
    {
        auto it = lowerer.symbols.find(std::string(name));
        if (it == lowerer.symbols.end())
            return nullptr;
        return &it->second;
    }

    static void setSymbolType(Lowerer &lowerer, std::string_view name, Lowerer::AstType type)
    {
        auto &info = ensureSymbol(lowerer, name);
        info.type = type;
        info.hasType = true;
        info.isBoolean = !info.isArray && type == Lowerer::AstType::Bool;
    }

    static void setSymbolObjectType(Lowerer &lowerer, std::string_view name, std::string className)
    {
        if (name.empty())
            return;
        auto &info = ensureSymbol(lowerer, name);
        info.isObject = true;
        info.objectClass = std::move(className);
        info.hasType = true;
    }

    static void markSymbolReferenced(Lowerer &lowerer, std::string_view name)
    {
        if (name.empty())
            return;
        auto &info = ensureSymbol(lowerer, name);
        if (!info.hasType)
        {
            info.type = inferAstTypeFromName(name);
            info.hasType = true;
            info.isBoolean = !info.isArray && info.type == Lowerer::AstType::Bool;
        }
        info.referenced = true;
    }

    static void markArray(Lowerer &lowerer, std::string_view name)
    {
        if (name.empty())
            return;
        auto &info = ensureSymbol(lowerer, name);
        info.isArray = true;
        if (info.isBoolean)
            info.isBoolean = false;
    }

    static void resetSymbolState(Lowerer &lowerer)
    {
        for (auto it = lowerer.symbols.begin(); it != lowerer.symbols.end();)
        {
            Lowerer::SymbolInfo &info = it->second;
            if (!info.stringLabel.empty())
            {
                info.type = Lowerer::AstType::I64;
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

    static Lowerer::SlotType getSlotType(const Lowerer &lowerer, std::string_view name)
    {
        Lowerer::SlotType info;
        auto astTy = inferAstTypeFromName(name);
        if (const auto *sym = findSymbol(lowerer, name))
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
                info.isBoolean = (astTy == Lowerer::AstType::Bool);
            else
                info.isBoolean = false;
        }
        else
        {
            info.isArray = false;
            info.isBoolean = (astTy == Lowerer::AstType::Bool);
        }
        if (info.isArray)
            info.type = Type(Type::Kind::Ptr);
        else
            info.type = coreTypeForAstType(info.isBoolean ? Lowerer::AstType::Bool : astTy);
        return info;
    }

    static Lowerer::ProcedureMetadata collectProcedureMetadata(Lowerer &lowerer,
                                                                const std::vector<Param> &params,
                                                                const std::vector<StmtPtr> &body,
                                                                const Lowerer::ProcedureConfig &config)
    {
        Lowerer::ProcedureMetadata metadata;
        metadata.paramCount = params.size();
        metadata.bodyStmts.reserve(body.size());
        for (const auto &stmt : body)
            metadata.bodyStmts.push_back(stmt.get());

        lowerer.collectVars(metadata.bodyStmts);

        if (config.postCollect)
            config.postCollect();

        metadata.irParams.reserve(params.size());
        for (const auto &p : params)
        {
            metadata.paramNames.insert(p.name);
            Type ty = p.is_array ? Type(Type::Kind::Ptr) : coreTypeForAstType(p.type);
            metadata.irParams.push_back(il::core::Param{p.name, ty});
            if (p.is_array)
            {
                lowerer.requireArrayI32Retain();
                lowerer.requireArrayI32Release();
            }
        }

        return metadata;
    }

    static void allocateLocalSlots(Lowerer &lowerer,
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
            Lowerer::SlotType slotInfo = getSlotType(lowerer, name);
            if (slotInfo.isArray)
            {
                Value slot = lowerer.emitAlloca(8);
                info.slotId = slot.id;
                lowerer.emitStore(Type(Type::Kind::Ptr), slot, Value::null());
                continue;
            }
            Value slot = lowerer.emitAlloca(slotInfo.isBoolean ? 1 : 8);
            info.slotId = slot.id;
            if (slotInfo.isBoolean)
                lowerer.emitStore(lowerer.ilBoolTy(), slot, lowerer.emitBoolConst(false));
            else if (slotInfo.type.kind == Type::Kind::Str)
            {
                Value empty = lowerer.emitCallRet(slotInfo.type, "rt_str_empty", {});
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
            Value slot = lowerer.emitAlloca(8);
            info.arrayLengthSlot = slot.id;
            lowerer.emitStore(Type(Type::Kind::I64), slot, Value::null());
        }
    }

    static void materializeParams(Lowerer &lowerer, const std::vector<Param> &params)
    {
        auto &ctx = lowerer.context();
        auto *func = ctx.function();
        assert(func && "materializeParams requires an active function");
        for (size_t i = 0; i < params.size(); ++i)
        {
            const auto &p = params[i];
            bool isBoolParam = !p.is_array && p.type == Lowerer::AstType::Bool;
            Value slot = lowerer.emitAlloca(isBoolParam ? 1 : 8);
            if (p.is_array)
            {
                lowerer.markArray(p.name);
                lowerer.emitStore(Type(Type::Kind::Ptr), slot, Value::null());
            }
            lowerer.setSymbolType(p.name, p.type);
            lowerer.markSymbolReferenced(p.name);
            auto &info = lowerer.ensureSymbol(p.name);
            info.slotId = slot.id;
            il::core::Type ty = func->params[i].type;
            Value incoming = Value::temp(func->params[i].id);
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

    static void collectVars(ProcedureLowering &lowering, const std::vector<const Stmt *> &stmts)
    {
        VarCollectWalker walker(lowering.lowerer);
        for (const auto *stmt : stmts)
            if (stmt)
                walker.walkStmt(*stmt);
    }

    static void collectVars(ProcedureLowering &lowering, const Program &prog)
    {
        std::vector<const Stmt *> ptrs;
        ptrs.reserve(prog.procs.size() + prog.main.size());
        for (const auto &s : prog.procs)
            ptrs.push_back(s.get());
        for (const auto &s : prog.main)
            ptrs.push_back(s.get());
        collectVars(lowering, ptrs);
    }
};

} // namespace viper::basic::lower::locals

namespace il::frontends::basic
{

Lowerer::SymbolInfo &Lowerer::ensureSymbol(std::string_view name)
{
    return viper::basic::lower::locals::API::ensureSymbol(*this, name);
}

Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name)
{
    return viper::basic::lower::locals::API::findSymbol(*this, name);
}

const Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name) const
{
    return viper::basic::lower::locals::API::findSymbol(*this, name);
}

void Lowerer::setSymbolType(std::string_view name, AstType type)
{
    viper::basic::lower::locals::API::setSymbolType(*this, name, type);
}

void Lowerer::setSymbolObjectType(std::string_view name, std::string className)
{
    viper::basic::lower::locals::API::setSymbolObjectType(*this, name, std::move(className));
}

void Lowerer::markSymbolReferenced(std::string_view name)
{
    viper::basic::lower::locals::API::markSymbolReferenced(*this, name);
}

void Lowerer::markArray(std::string_view name)
{
    viper::basic::lower::locals::API::markArray(*this, name);
}

void Lowerer::resetSymbolState()
{
    viper::basic::lower::locals::API::resetSymbolState(*this);
}

Lowerer::SlotType Lowerer::getSlotType(std::string_view name) const
{
    return viper::basic::lower::locals::API::getSlotType(*this, name);
}

Lowerer::ProcedureMetadata Lowerer::collectProcedureMetadata(const std::vector<Param> &params,
                                                              const std::vector<StmtPtr> &body,
                                                              const ProcedureConfig &config)
{
    return viper::basic::lower::locals::API::collectProcedureMetadata(*this, params, body, config);
}

void Lowerer::allocateLocalSlots(const std::unordered_set<std::string> &paramNames, bool includeParams)
{
    viper::basic::lower::locals::API::allocateLocalSlots(*this, paramNames, includeParams);
}

void Lowerer::materializeParams(const std::vector<Param> &params)
{
    viper::basic::lower::locals::API::materializeParams(*this, params);
}

void ProcedureLowering::collectVars(const std::vector<const Stmt *> &stmts)
{
    viper::basic::lower::locals::API::collectVars(*this, stmts);
}

void ProcedureLowering::collectVars(const Program &prog)
{
    viper::basic::lower::locals::API::collectVars(*this, prog);
}

void Lowerer::collectVars(const Program &prog)
{
    procedureLowering->collectVars(prog);
}

void Lowerer::collectVars(const std::vector<const Stmt *> &stmts)
{
    procedureLowering->collectVars(stmts);
}

} // namespace il::frontends::basic

