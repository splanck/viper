//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Dispatch.cpp
/// @brief Virtual and interface method dispatch for the Zia IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"

namespace il::frontends::zia
{

using il::core::Opcode;
using il::core::Type;
using il::core::Value;

/// Dispatch table entry: (classId, qualifiedMethodName)
using DispatchEntry = std::pair<int, std::string>;

//=============================================================================
// Virtual Method Dispatch
//=============================================================================

LowerResult Lowerer::lowerVirtualMethodCall(const EntityTypeInfo &entityInfo,
                                            const std::string &slotKey,
                                            const std::string &ownerType,
                                            MethodDecl *method,
                                            Value selfValue,
                                            CallExpr *expr)
{
    TypeRef methodType = sema_.getMethodType(ownerType, method);
    TypeRef returnType = methodType && methodType->kind == TypeKindSem::Function
                             ? methodType->returnType()
                             : types::voidType();
    Type ilReturnType = mapType(returnType);

    // Build argument list: self + call args
    std::vector<Value> args;
    args.reserve(expr->args.size() + 1);
    args.push_back(selfValue);
    for (auto &arg : expr->args)
        args.push_back(lowerExpr(arg.value.get()).value);

    // Build dispatch table
    std::vector<DispatchEntry> dispatchTable;
    auto addEntry = [&](const EntityTypeInfo &info)
    {
        auto vtIt = info.vtableIndex.find(slotKey);
        if (vtIt != info.vtableIndex.end())
            dispatchTable.emplace_back(info.classId, info.vtable[vtIt->second]);
    };

    addEntry(entityInfo);
    for (const auto &[name, info] : entityTypes_)
    {
        if (name == entityInfo.name)
            continue;
        std::string parent = info.baseClass;
        while (!parent.empty())
        {
            if (parent == entityInfo.name)
            {
                addEntry(info);
                break;
            }
            auto it = entityTypes_.find(parent);
            if (it == entityTypes_.end())
                break;
            parent = it->second.baseClass;
        }
    }

    // Get runtime class_id
    Value classIdVal = emitCallRet(Type(Type::Kind::I64), "rt_obj_class_id", {selfValue});

    // Single implementation - direct call
    if (dispatchTable.size() <= 1)
    {
        std::string target =
            dispatchTable.empty() ? sema_.loweredMethodName(ownerType, method) : dispatchTable[0].second;
        // Handle void return types correctly
        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitCall(target, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        else
        {
            Value result = emitCallRet(ilReturnType, target, args);
            return {result, ilReturnType};
        }
    }

    // Multiple implementations - SwitchI32 dispatch
    //
    // Narrow the I64 class ID to I32 for the switch scrutinee, then emit a
    // single SwitchI32 instruction that jumps directly to the right call block.
    // This replaces the previous O(N) if-else chain with O(1) dispatch.
    Value classIdI32 = emitUnary(Opcode::CastUiNarrowChk, Type(Type::Kind::I32), classIdVal);

    size_t endBlock = createBlock("vdispatch_end");
    Value resultSlot;
    if (ilReturnType.kind != Type::Kind::Void)
    {
        unsigned id = nextTempId();
        il::core::Instr instr{
            id, Opcode::Alloca, Type(Type::Kind::Ptr), {Value::constInt(8)}, {}, {}, {}, {}, {}};
        instr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(instr);
        resultSlot = Value::temp(id);
    }

    // Create a call block for each dispatch entry.
    std::vector<size_t> callBlocks;
    callBlocks.reserve(dispatchTable.size());
    for (size_t i = 0; i < dispatchTable.size(); ++i)
        callBlocks.push_back(createBlock("vdispatch_call_" + std::to_string(i)));

    // Emit SwitchI32: scrutinee is the I32 class ID, cases are class IDs.
    // Default target is the last entry (fallback).
    {
        il::core::Instr sw;
        sw.op = Opcode::SwitchI32;
        sw.type = Type(Type::Kind::Void);
        sw.operands.push_back(classIdI32);

        // Default case → last call block (fallback for unknown class IDs).
        sw.labels.push_back(currentFunc_->blocks[callBlocks.back()].label);
        sw.brArgs.push_back({});

        // One case per dispatch entry.
        for (size_t i = 0; i < dispatchTable.size(); ++i)
        {
            sw.operands.push_back(Value::constInt(static_cast<int64_t>(dispatchTable[i].first)));
            sw.labels.push_back(currentFunc_->blocks[callBlocks[i]].label);
            sw.brArgs.push_back({});
        }

        sw.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(std::move(sw));
        blockMgr_.currentBlock()->terminated = true;
    }

    // Emit each call block: call the target method, store result, branch to end.
    for (size_t i = 0; i < dispatchTable.size(); ++i)
    {
        const auto &[classId, targetMethod] = dispatchTable[i];
        setBlock(callBlocks[i]);
        if (ilReturnType.kind == Type::Kind::Void)
            emitCall(targetMethod, args);
        else
            emitStore(resultSlot, emitCallRet(ilReturnType, targetMethod, args), ilReturnType);
        emitBr(endBlock);
    }

    setBlock(endBlock);
    if (ilReturnType.kind == Type::Kind::Void)
        return {Value::constInt(0), Type(Type::Kind::Void)};
    return {emitLoad(resultSlot, ilReturnType), ilReturnType};
}

//=============================================================================
// Interface Method Dispatch
//=============================================================================

LowerResult Lowerer::lowerInterfaceMethodCall(const InterfaceTypeInfo &ifaceInfo,
                                              const std::string &slotKey,
                                              const std::string &ownerType,
                                              MethodDecl *method,
                                              Value selfValue,
                                              CallExpr *expr)
{
    TypeRef methodType = sema_.getMethodType(ownerType, method);
    TypeRef returnType =
        methodType && methodType->kind == TypeKindSem::Function ? methodType->returnType()
                                                                : types::voidType();
    Type ilReturnType = mapType(returnType);

    // Build argument list: self + call args
    std::vector<Value> args;
    args.reserve(expr->args.size() + 1);
    args.push_back(selfValue);
    for (auto &arg : expr->args)
        args.push_back(lowerExpr(arg.value.get()).value);

    // Look up the method's slot index in the interface
    size_t slotIdx = ifaceInfo.findSlot(slotKey);
    if (slotIdx == SIZE_MAX)
    {
        // Fallback: method not in interface slot map (shouldn't happen)
        return {Value::constInt(0), ilReturnType};
    }

    // Get object's class ID (from heap header, works for Zia objects)
    Value classId = emitCallRet(Type(Type::Kind::I64), "rt_obj_class_id", {selfValue});

    // Emit itable lookup: rt_get_interface_impl(classId, ifaceId) -> Ptr to itable
    Value itable = emitCallRet(Type(Type::Kind::Ptr),
                               "rt_get_interface_impl",
                               {classId, Value::constInt(static_cast<int64_t>(ifaceInfo.ifaceId))});

    // Load function pointer from itable[slotIdx]
    int64_t offset = static_cast<int64_t>(slotIdx * 8ULL);
    Value entryPtr =
        emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), itable, Value::constInt(offset));
    Value fnPtr = emitLoad(entryPtr, Type(Type::Kind::Ptr));

    // Emit indirect call through function pointer
    if (ilReturnType.kind == Type::Kind::Void)
    {
        emitCallIndirect(fnPtr, args);
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }
    Value result = emitCallIndirectRet(ilReturnType, fnPtr, args);
    return {result, ilReturnType};
}

} // namespace il::frontends::zia
