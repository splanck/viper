//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements object ownership emission for BASIC OOP features.
/// @details Helpers release retained object handles at scope exits. They
/// consult the ProcedureContext for active blocks and append control-flow as
/// required, leaving terminator management consistent with control helpers.
/// Temporary values remain owned by the lowerer while runtime helpers manage
/// the underlying reference counts.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

void Lowerer::releaseObjectLocals(const std::unordered_set<std::string> &paramNames)
{
    auto releaseSlot = [this](SymbolInfo &info)
    {
        if (!builder || !info.slotId)
            return;
        ProcedureContext &ctx = context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (!func || !origin)
            return;

        std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
        Value slot = Value::temp(*info.slotId);

        curLoc = {};
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);

        requestHelper(RuntimeFeature::ObjReleaseChk0);
        requestHelper(RuntimeFeature::ObjFree);

        Value shouldDestroy = emitCallRet(ilBoolTy(), "rt_obj_release_check0", {handle});

        BlockNamer *blockNamer = ctx.blockNames().namer();
        std::string destroyLabel = blockNamer ? blockNamer->generic("obj_epilogue_dtor")
                                              : mangler.block("obj_epilogue_dtor");
        std::size_t destroyIdx = func->blocks.size();
        builder->addBlock(*func, destroyLabel);

        std::string contLabel = blockNamer ? blockNamer->generic("obj_epilogue_cont")
                                           : mangler.block("obj_epilogue_cont");
        std::size_t contIdx = func->blocks.size();
        builder->addBlock(*func, contLabel);

        BasicBlock *destroyBlk = &func->blocks[destroyIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[originIdx]);
        curLoc = {};
        emitCBr(shouldDestroy, destroyBlk, contBlk);

        ctx.setCurrent(destroyBlk);
        curLoc = {};
        if (!info.objectClass.empty())
            emitCall(mangleClassDtor(info.objectClass), {handle});
        emitCall("rt_obj_free", {handle});
        emitBr(contBlk);

        ctx.setCurrent(contBlk);
        curLoc = {};
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    };

    for (auto &[name, info] : symbols)
    {
        if (!info.referenced || !info.isObject)
            continue;
        if (name == "ME")
            continue;
        if (paramNames.contains(name))
            continue;
        if (!info.slotId)
            continue;
        releaseSlot(info);
    }
}

void Lowerer::releaseObjectParams(const std::unordered_set<std::string> &paramNames)
{
    if (paramNames.empty())
        return;

    auto releaseSlot = [this](SymbolInfo &info)
    {
        if (!builder || !info.slotId)
            return;
        ProcedureContext &ctx = context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (!func || !origin)
            return;

        std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
        Value slot = Value::temp(*info.slotId);

        curLoc = {};
        Value handle = emitLoad(Type(Type::Kind::Ptr), slot);

        requestHelper(RuntimeFeature::ObjReleaseChk0);
        requestHelper(RuntimeFeature::ObjFree);

        Value shouldDestroy = emitCallRet(ilBoolTy(), "rt_obj_release_check0", {handle});

        BlockNamer *blockNamer = ctx.blockNames().namer();
        std::string destroyLabel = blockNamer ? blockNamer->generic("obj_epilogue_dtor")
                                              : mangler.block("obj_epilogue_dtor");
        std::size_t destroyIdx = func->blocks.size();
        builder->addBlock(*func, destroyLabel);

        std::string contLabel = blockNamer ? blockNamer->generic("obj_epilogue_cont")
                                           : mangler.block("obj_epilogue_cont");
        std::size_t contIdx = func->blocks.size();
        builder->addBlock(*func, contLabel);

        BasicBlock *destroyBlk = &func->blocks[destroyIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[originIdx]);
        curLoc = {};
        emitCBr(shouldDestroy, destroyBlk, contBlk);

        ctx.setCurrent(destroyBlk);
        curLoc = {};
        if (!info.objectClass.empty())
            emitCall(mangleClassDtor(info.objectClass), {handle});
        emitCall("rt_obj_free", {handle});
        emitBr(contBlk);

        ctx.setCurrent(contBlk);
        curLoc = {};
        emitStore(Type(Type::Kind::Ptr), slot, Value::null());
    };

    for (auto &[name, info] : symbols)
    {
        if (!info.referenced || !info.isObject)
            continue;
        if (name == "ME")
            continue;
        if (!paramNames.contains(name))
            continue;
        if (!info.slotId)
            continue;
        releaseSlot(info);
    }
}

} // namespace il::frontends::basic
