//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/oop/Lower_OOP_RuntimeHelpers.cpp
// Purpose: Implementation of consolidated OOP runtime emission helpers.
// Key invariants: Centralizes patterns for parameter initialization, array field
//                 allocation, and method epilogue. (BUG-056, BUG-073, etc.)
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/oop/Lower_OOP_RuntimeHelpers.hpp"
#include "frontends/basic/ILTypeUtils.hpp"
#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

namespace
{
using AstType = ::il::frontends::basic::Type;
using IlType = il::core::Type;
using Value = il::core::Value;
using Opcode = il::core::Opcode;
} // namespace

OopEmitHelper::OopEmitHelper(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

// -------------------------------------------------------------------------
// Parameter Initialization
// -------------------------------------------------------------------------

void OopEmitHelper::emitParamInit(const Param &param,
                                  il::core::Function &fn,
                                  std::size_t paramIdx,
                                  std::unordered_set<std::string> &paramNames)
{
    paramNames.insert(param.name);
    lowerer_.curLoc = param.loc;

    // Allocate slot: BOOLEAN uses 1 byte, everything else 8 bytes
    Value slot = lowerer_.emitAlloca((!param.is_array && param.type == AstType::Bool) ? 1 : 8);

    if (param.is_array)
    {
        lowerer_.markArray(param.name);
        lowerer_.emitStore(IlType(IlType::Kind::Ptr), slot, Value::null());
    }

    // Preserve object-class typing for parameters so member calls resolve. (BUG-073)
    if (!param.objectClass.empty())
        lowerer_.setSymbolObjectType(param.name, lowerer_.qualify(param.objectClass));
    else
        lowerer_.setSymbolType(param.name, param.type);

    lowerer_.markSymbolReferenced(param.name);
    auto &info = lowerer_.ensureSymbol(param.name);
    info.slotId = slot.id;

    // Determine IL type for the parameter
    IlType ilParamTy = (!param.objectClass.empty() || param.is_array)
                           ? IlType(IlType::Kind::Ptr)
                           : type_conv::astToIlType(param.type);

    Value incoming = Value::temp(fn.params[paramIdx].id);
    if (param.is_array)
    {
        // Object arrays require distinct runtime calls. (BUG-OOP-038)
        bool isObjectArray = !param.objectClass.empty();
        lowerer_.storeArray(slot, incoming, param.type, isObjectArray);
    }
    else
        lowerer_.emitStore(ilParamTy, slot, incoming);
}

void OopEmitHelper::emitAllParamInits(const std::vector<Param> &params,
                                      il::core::Function &fn,
                                      std::size_t selfOffset,
                                      std::unordered_set<std::string> &paramNames)
{
    for (std::size_t i = 0; i < params.size(); ++i)
    {
        emitParamInit(params[i], fn, selfOffset + i, paramNames);
    }
}

// -------------------------------------------------------------------------
// Array Field Initialization
// -------------------------------------------------------------------------

void OopEmitHelper::emitArrayFieldInits(const ClassDecl &klass, unsigned selfSlotId)
{
    const ClassLayout *layout = lowerer_.findClassLayout(klass.name);
    if (!layout)
        return;

    Value selfPtr = lowerer_.loadSelfPointer(selfSlotId);

    for (const auto &field : klass.fields)
    {
        if (!field.isArray || field.arrayExtents.empty())
            continue;

        // Compute total length as the product of inclusive extents
        // BASIC DIM uses inclusive upper bounds (e.g., DIM a(7) => 8 elements)
        long long total = 1;
        for (long long e : field.arrayExtents)
            total *= (e + 1);
        Value length = Value::constInt(total);

        // Find field offset in layout
        const auto *fi = layout->findField(field.name);
        if (!fi)
            continue;

        // Allocate appropriate array type
        Value handle;
        if (field.type == AstType::Str)
        {
            lowerer_.requireArrayStrAlloc();
            handle = lowerer_.emitCallRet(IlType(IlType::Kind::Ptr), "rt_arr_str_alloc", {length});
        }
        else if (!field.objectClassName.empty())
        {
            // Object-typed fields use object array allocation. (BUG-089)
            lowerer_.requireArrayObjNew();
            handle = lowerer_.emitCallRet(IlType(IlType::Kind::Ptr), "rt_arr_obj_new", {length});
        }
        else
        {
            lowerer_.requireArrayI64New();
            handle = lowerer_.emitCallRet(IlType(IlType::Kind::Ptr), "rt_arr_i64_new", {length});
        }

        // Store handle into object field
        Value fieldPtr = lowerer_.emitBinary(Opcode::GEP,
                                             IlType(IlType::Kind::Ptr),
                                             selfPtr,
                                             Value::constInt(static_cast<long long>(fi->offset)));
        lowerer_.emitStore(IlType(IlType::Kind::Ptr), fieldPtr, handle);
    }
}

// -------------------------------------------------------------------------
// Method Epilogue
// -------------------------------------------------------------------------

void OopEmitHelper::emitMethodEpilogue(const std::unordered_set<std::string> &paramNames,
                                       const std::unordered_set<std::string> &excludeFromObjRelease)
{
    lowerer_.curLoc = {};
    lowerer_.releaseDeferredTemps();
    lowerer_.releaseObjectLocals(excludeFromObjRelease);
    // Borrowed parameters are not released; caller owns their lifetime. (BUG-105)
    lowerer_.releaseArrayLocals(paramNames);
}

// -------------------------------------------------------------------------
// Body Statement Lowering
// -------------------------------------------------------------------------

void OopEmitHelper::emitBodyAndBranchToExit(const std::vector<const Stmt *> &bodyStmts,
                                            std::size_t exitIdx)
{
    auto &ctx = lowerer_.context();

    if (bodyStmts.empty())
    {
        lowerer_.curLoc = {};
        il::core::Function *func = ctx.function();
        il::core::BasicBlock *exitBlock = &func->blocks[exitIdx];
        lowerer_.emitBr(exitBlock);
    }
    else
    {
        lowerer_.lowerStatementSequence(bodyStmts, /*stopOnTerminated=*/true);
        if (ctx.current() && !ctx.current()->terminated)
        {
            il::core::Function *func = ctx.function();
            il::core::BasicBlock *exitBlock = &func->blocks[exitIdx];
            lowerer_.emitBr(exitBlock);
        }
    }
}

// -------------------------------------------------------------------------
// VTable/ITable Population (duplicated logic consolidated)
// -------------------------------------------------------------------------

std::string OopEmitHelper::findImplementorClass(const OopIndex &oopIndex,
                                                const std::string &startQClass,
                                                const std::string &methodName)
{
    const ClassInfo *cur = oopIndex.findClass(startQClass);
    while (cur)
    {
        auto itM = cur->methods.find(methodName);
        if (itM != cur->methods.end())
        {
            if (!itM->second.isAbstract)
                return cur->qualifiedName;
        }
        if (cur->baseQualified.empty())
            break;
        cur = oopIndex.findClass(cur->baseQualified);
    }
    return startQClass; // fallback
}

std::vector<std::string> OopEmitHelper::buildVtableSlotMap(const OopIndex &oopIndex,
                                                           const std::string &classQName,
                                                           std::size_t &maxSlot)
{
    maxSlot = 0;
    bool hasAnyVirtual = false;

    // First pass: compute max slot
    {
        const ClassInfo *cur = oopIndex.findClass(classQName);
        while (cur)
        {
            for (const auto &mp : cur->methods)
            {
                const auto &mi = mp.second;
                if (!mi.isVirtual || mi.slot < 0)
                    continue;
                hasAnyVirtual = true;
                maxSlot = std::max<std::size_t>(maxSlot, static_cast<std::size_t>(mi.slot));
            }
            if (cur->baseQualified.empty())
                break;
            cur = oopIndex.findClass(cur->baseQualified);
        }
    }

    const std::size_t slotCount = hasAnyVirtual ? (maxSlot + 1) : 0;
    std::vector<std::string> slotToName(slotCount);

    // Second pass: build slot-to-name mapping
    {
        const ClassInfo *cur = oopIndex.findClass(classQName);
        while (cur)
        {
            for (const auto &mp : cur->methods)
            {
                const auto &mname = mp.first;
                const auto &mi = mp.second;
                if (!mi.isVirtual || mi.slot < 0)
                    continue;
                const std::size_t s = static_cast<std::size_t>(mi.slot);
                if (s < slotToName.size())
                    slotToName[s] = mname; // prefer most-derived assignment first in walk
            }
            if (cur->baseQualified.empty())
                break;
            cur = oopIndex.findClass(cur->baseQualified);
        }
    }

    return slotToName;
}

} // namespace il::frontends::basic
