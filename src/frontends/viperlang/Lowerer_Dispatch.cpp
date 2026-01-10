//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Dispatch.cpp
/// @brief Virtual and interface method dispatch for the ViperLang IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"

namespace il::frontends::viperlang
{

using il::core::Value;
using il::core::Type;
using il::core::Opcode;

/// Dispatch table entry: (classId, qualifiedMethodName)
using DispatchEntry = std::pair<int, std::string>;

//=============================================================================
// Virtual Method Dispatch
//=============================================================================

LowerResult Lowerer::lowerVirtualMethodCall(const EntityTypeInfo &entityInfo,
                                             const std::string &methodName,
                                             size_t /*vtableSlot*/,
                                             Value selfValue,
                                             CallExpr *expr)
{
    // Get return type - search up inheritance chain if needed
    MethodDecl *method = entityInfo.findMethod(methodName);
    std::string parentName = entityInfo.baseClass;
    while (!method && !parentName.empty())
    {
        auto parentIt = entityTypes_.find(parentName);
        if (parentIt == entityTypes_.end()) break;
        method = parentIt->second.findMethod(methodName);
        parentName = parentIt->second.baseClass;
    }

    TypeRef returnType = types::voidType();
    if (method && method->returnType)
        returnType = sema_.resolveType(method->returnType.get());
    Type ilReturnType = mapType(returnType);

    // Build argument list: self + call args
    std::vector<Value> args;
    args.reserve(expr->args.size() + 1);
    args.push_back(selfValue);
    for (auto &arg : expr->args)
        args.push_back(lowerExpr(arg.value.get()).value);

    // Build dispatch table
    std::vector<DispatchEntry> dispatchTable;
    auto addEntry = [&](const EntityTypeInfo &info) {
        auto vtIt = info.vtableIndex.find(methodName);
        if (vtIt != info.vtableIndex.end())
            dispatchTable.emplace_back(info.classId, info.vtable[vtIt->second]);
    };

    addEntry(entityInfo);
    for (const auto &[name, info] : entityTypes_)
    {
        if (name == entityInfo.name) continue;
        std::string parent = info.baseClass;
        while (!parent.empty())
        {
            if (parent == entityInfo.name) { addEntry(info); break; }
            auto it = entityTypes_.find(parent);
            if (it == entityTypes_.end()) break;
            parent = it->second.baseClass;
        }
    }

    // Get runtime class_id
    Value classIdVal = emitCallRet(Type(Type::Kind::I64), "rt_obj_class_id", {selfValue});

    // Single implementation - direct call
    if (dispatchTable.size() <= 1)
    {
        std::string target = dispatchTable.empty()
            ? entityInfo.name + "." + methodName
            : dispatchTable[0].second;
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

    // Multiple implementations - dispatch switch
    size_t endBlock = createBlock("vdispatch_end");
    Value resultSlot;
    if (ilReturnType.kind != Type::Kind::Void)
    {
        unsigned id = nextTempId();
        il::core::Instr instr{id, Opcode::Alloca, Type(Type::Kind::Ptr), {Value::constInt(8)}};
        blockMgr_.currentBlock()->instructions.push_back(instr);
        resultSlot = Value::temp(id);
    }

    for (size_t i = 0; i < dispatchTable.size(); ++i)
    {
        const auto &[classId, targetMethod] = dispatchTable[i];
        bool isLast = (i == dispatchTable.size() - 1);

        if (isLast)
        {
            if (ilReturnType.kind == Type::Kind::Void)
                emitCall(targetMethod, args);
            else
                emitStore(resultSlot, emitCallRet(ilReturnType, targetMethod, args), ilReturnType);
            emitBr(endBlock);
        }
        else
        {
            size_t nextCheck = createBlock("vdispatch_check_" + std::to_string(i + 1));
            size_t callBlock = createBlock("vdispatch_call_" + std::to_string(i));

            emitCBr(emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), classIdVal,
                               Value::constInt(static_cast<int64_t>(classId))),
                    callBlock, nextCheck);

            setBlock(callBlock);
            if (ilReturnType.kind == Type::Kind::Void)
                emitCall(targetMethod, args);
            else
                emitStore(resultSlot, emitCallRet(ilReturnType, targetMethod, args), ilReturnType);
            emitBr(endBlock);
            setBlock(nextCheck);
        }
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
                                               const std::string &methodName,
                                               MethodDecl *method,
                                               Value selfValue,
                                               CallExpr *expr)
{
    TypeRef returnType = types::voidType();
    if (method && method->returnType)
        returnType = sema_.resolveType(method->returnType.get());
    Type ilReturnType = mapType(returnType);

    // Build argument list
    std::vector<Value> args;
    args.reserve(expr->args.size() + 1);
    args.push_back(selfValue);
    for (auto &arg : expr->args)
        args.push_back(lowerExpr(arg.value.get()).value);

    // Build dispatch table for all implementors
    std::vector<DispatchEntry> dispatchTable;
    for (const auto &[entityName, entityInfo] : entityTypes_)
    {
        if (entityInfo.implementedInterfaces.count(ifaceInfo.name))
        {
            auto it = entityInfo.vtableIndex.find(methodName);
            if (it != entityInfo.vtableIndex.end())
                dispatchTable.emplace_back(entityInfo.classId, entityInfo.vtable[it->second]);
        }
    }

    if (dispatchTable.empty())
        return {Value::constInt(0), ilReturnType};

    // Get runtime class_id
    Value classIdVal = emitCallRet(Type(Type::Kind::I64), "rt_obj_class_id", {selfValue});

    // Single implementation - direct call
    if (dispatchTable.size() == 1)
    {
        // Handle void return types correctly
        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitCall(dispatchTable[0].second, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        else
        {
            Value result = emitCallRet(ilReturnType, dispatchTable[0].second, args);
            return {result, ilReturnType};
        }
    }

    // Multiple implementations - dispatch switch
    size_t endBlock = createBlock("iface_dispatch_end");
    Value resultSlot;
    if (ilReturnType.kind != Type::Kind::Void)
    {
        unsigned id = nextTempId();
        il::core::Instr instr{id, Opcode::Alloca, Type(Type::Kind::Ptr), {Value::constInt(8)}};
        blockMgr_.currentBlock()->instructions.push_back(instr);
        resultSlot = Value::temp(id);
    }

    for (size_t i = 0; i < dispatchTable.size(); ++i)
    {
        const auto &[classId, targetMethod] = dispatchTable[i];
        bool isLast = (i == dispatchTable.size() - 1);

        if (isLast)
        {
            if (ilReturnType.kind == Type::Kind::Void)
                emitCall(targetMethod, args);
            else
                emitStore(resultSlot, emitCallRet(ilReturnType, targetMethod, args), ilReturnType);
            emitBr(endBlock);
        }
        else
        {
            size_t nextCheck = createBlock("iface_dispatch_check_" + std::to_string(i + 1));
            size_t callBlock = createBlock("iface_dispatch_call_" + std::to_string(i));

            emitCBr(emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), classIdVal,
                               Value::constInt(static_cast<int64_t>(classId))),
                    callBlock, nextCheck);

            setBlock(callBlock);
            if (ilReturnType.kind == Type::Kind::Void)
                emitCall(targetMethod, args);
            else
                emitStore(resultSlot, emitCallRet(ilReturnType, targetMethod, args), ilReturnType);
            emitBr(endBlock);
            setBlock(nextCheck);
        }
    }

    setBlock(endBlock);
    if (ilReturnType.kind == Type::Kind::Void)
        return {Value::constInt(0), Type(Type::Kind::Void)};
    return {emitLoad(resultSlot, ilReturnType), ilReturnType};
}

} // namespace il::frontends::viperlang
