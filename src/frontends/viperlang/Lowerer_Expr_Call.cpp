//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr_Call.cpp
/// @brief Call expression lowering for the ViperLang IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"
#include "frontends/viperlang/RuntimeNames.hpp"

namespace il::frontends::viperlang
{

using namespace runtime;

//=============================================================================
// List Method Call Helper
//=============================================================================

std::optional<LowerResult> Lowerer::lowerListMethodCall(Value baseValue,
                                                         TypeRef baseType,
                                                         const std::string &methodName,
                                                         CallExpr *expr)
{
    if (equalsIgnoreCase(methodName, "get"))
    {
        if (expr->args.size() >= 1)
        {
            auto indexResult = lowerExpr(expr->args[0].value.get());
            Value boxed = emitCallRet(
                Type(Type::Kind::Ptr), kListGet, {baseValue, indexResult.value});
            TypeRef elemType = baseType->elementType();
            if (elemType)
            {
                Type ilElemType = mapType(elemType);
                return emitUnbox(boxed, ilElemType);
            }
            return LowerResult{boxed, Type(Type::Kind::Ptr)};
        }
    }

    if (equalsIgnoreCase(methodName, "removeAt"))
    {
        if (expr->args.size() >= 1)
        {
            auto indexResult = lowerExpr(expr->args[0].value.get());
            emitCall(kListRemoveAt, {baseValue, indexResult.value});
            return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
        }
    }

    // Bug #022 fix: Add remove (by value) method handling
    if (equalsIgnoreCase(methodName, "remove"))
    {
        if (expr->args.size() >= 1)
        {
            auto valueResult = lowerExpr(expr->args[0].value.get());
            Value boxedValue = emitBox(valueResult.value, valueResult.type);
            Value result = emitCallRet(
                Type(Type::Kind::I1), kListRemove, {baseValue, boxedValue});
            return LowerResult{result, Type(Type::Kind::I1)};
        }
    }

    if (equalsIgnoreCase(methodName, "insert"))
    {
        if (expr->args.size() >= 2)
        {
            auto indexResult = lowerExpr(expr->args[0].value.get());
            auto valueResult = lowerExpr(expr->args[1].value.get());
            Value boxedValue = emitBox(valueResult.value, valueResult.type);
            emitCall(kListInsert, {baseValue, indexResult.value, boxedValue});
            return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
        }
    }

    if (equalsIgnoreCase(methodName, "find") || equalsIgnoreCase(methodName, "indexOf"))
    {
        if (expr->args.size() >= 1)
        {
            auto valueResult = lowerExpr(expr->args[0].value.get());
            Value boxedValue = emitBox(valueResult.value, valueResult.type);
            Value result = emitCallRet(
                Type(Type::Kind::I64), kListFind, {baseValue, boxedValue});
            return LowerResult{result, Type(Type::Kind::I64)};
        }
    }

    if (equalsIgnoreCase(methodName, "has") || equalsIgnoreCase(methodName, "contains"))
    {
        if (expr->args.size() >= 1)
        {
            auto valueResult = lowerExpr(expr->args[0].value.get());
            Value boxedValue = emitBox(valueResult.value, valueResult.type);
            Value result = emitCallRet(
                Type(Type::Kind::I1), kListContains, {baseValue, boxedValue});
            return LowerResult{result, Type(Type::Kind::I1)};
        }
    }

    if (equalsIgnoreCase(methodName, "set"))
    {
        if (expr->args.size() >= 2)
        {
            auto indexResult = lowerExpr(expr->args[0].value.get());
            auto valueResult = lowerExpr(expr->args[1].value.get());
            Value boxedValue = emitBox(valueResult.value, valueResult.type);
            emitCall(kListSet, {baseValue, indexResult.value, boxedValue});
            return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
        }
    }

    // Lower arguments with boxing for remaining methods
    std::vector<Value> args;
    args.reserve(expr->args.size() + 1);
    args.push_back(baseValue);

    for (auto &arg : expr->args)
    {
        auto result = lowerExpr(arg.value.get());
        args.push_back(emitBox(result.value, result.type));
    }

    // Map method names to runtime functions (case-insensitive)
    const char *runtimeFunc = nullptr;
    Type returnType = Type(Type::Kind::Void);

    if (equalsIgnoreCase(methodName, "add"))
    {
        runtimeFunc = kListAdd;
    }
    else if (equalsIgnoreCase(methodName, "size") ||
             equalsIgnoreCase(methodName, "count") ||
             equalsIgnoreCase(methodName, "length"))
    {
        runtimeFunc = kListCount;
        returnType = Type(Type::Kind::I64);
    }
    else if (equalsIgnoreCase(methodName, "clear"))
    {
        runtimeFunc = kListClear;
    }

    if (runtimeFunc != nullptr)
    {
        if (returnType.kind == Type::Kind::Void)
        {
            emitCall(runtimeFunc, args);
            return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
        }
        else if (returnType.kind == Type::Kind::Ptr)
        {
            Value boxed = emitCallRet(returnType, runtimeFunc, args);
            TypeRef elemType = baseType->elementType();
            if (elemType)
            {
                Type ilElemType = mapType(elemType);
                return emitUnbox(boxed, ilElemType);
            }
            return LowerResult{boxed, Type(Type::Kind::Ptr)};
        }
        else
        {
            Value result = emitCallRet(returnType, runtimeFunc, args);
            return LowerResult{result, returnType};
        }
    }

    return std::nullopt;
}

//=============================================================================
// Map Method Call Helper
//=============================================================================

std::optional<LowerResult> Lowerer::lowerMapMethodCall(Value baseValue,
                                                        TypeRef baseType,
                                                        const std::string &methodName,
                                                        CallExpr *expr)
{
    TypeRef valueType = baseType->typeArgs.size() > 1 ? baseType->typeArgs[1] : nullptr;

    if (equalsIgnoreCase(methodName, "set") || equalsIgnoreCase(methodName, "put"))
    {
        if (expr->args.size() >= 2)
        {
            auto keyResult = lowerExpr(expr->args[0].value.get());
            auto valueResult = lowerExpr(expr->args[1].value.get());
            Value boxedValue = emitBox(valueResult.value, valueResult.type);
            emitCall(kMapSet, {baseValue, keyResult.value, boxedValue});
            return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
        }
    }
    else if (equalsIgnoreCase(methodName, "get"))
    {
        if (expr->args.size() >= 1)
        {
            auto keyResult = lowerExpr(expr->args[0].value.get());
            Value boxed = emitCallRet(
                Type(Type::Kind::Ptr), kMapGet, {baseValue, keyResult.value});
            if (valueType)
            {
                Type ilValueType = mapType(valueType);
                return emitUnbox(boxed, ilValueType);
            }
            return LowerResult{boxed, Type(Type::Kind::Ptr)};
        }
    }
    else if (equalsIgnoreCase(methodName, "getOr"))
    {
        if (expr->args.size() >= 2)
        {
            auto keyResult = lowerExpr(expr->args[0].value.get());
            auto defaultResult = lowerExpr(expr->args[1].value.get());
            Value boxedDefault = emitBox(defaultResult.value, defaultResult.type);
            Value boxed = emitCallRet(Type(Type::Kind::Ptr),
                                      kMapGetOr,
                                      {baseValue, keyResult.value, boxedDefault});
            if (valueType)
            {
                Type ilValueType = mapType(valueType);
                return emitUnbox(boxed, ilValueType);
            }
            return LowerResult{boxed, Type(Type::Kind::Ptr)};
        }
    }
    else if (equalsIgnoreCase(methodName, "containsKey") ||
             equalsIgnoreCase(methodName, "hasKey") ||
             equalsIgnoreCase(methodName, "has"))
    {
        if (expr->args.size() >= 1)
        {
            auto keyResult = lowerExpr(expr->args[0].value.get());
            Value result = emitCallRet(Type(Type::Kind::I1),
                                       kMapContainsKey,
                                       {baseValue, keyResult.value});
            return LowerResult{result, Type(Type::Kind::I1)};
        }
    }
    else if (equalsIgnoreCase(methodName, "size") ||
             equalsIgnoreCase(methodName, "count") ||
             equalsIgnoreCase(methodName, "length"))
    {
        Value result = emitCallRet(Type(Type::Kind::I64), kMapCount, {baseValue});
        return LowerResult{result, Type(Type::Kind::I64)};
    }
    else if (equalsIgnoreCase(methodName, "remove"))
    {
        if (expr->args.size() >= 1)
        {
            auto keyResult = lowerExpr(expr->args[0].value.get());
            Value result = emitCallRet(
                Type(Type::Kind::I1), kMapRemove, {baseValue, keyResult.value});
            return LowerResult{result, Type(Type::Kind::I1)};
        }
    }
    else if (equalsIgnoreCase(methodName, "setIfMissing"))
    {
        if (expr->args.size() >= 2)
        {
            auto keyResult = lowerExpr(expr->args[0].value.get());
            auto valueResult = lowerExpr(expr->args[1].value.get());
            Value boxedValue = emitBox(valueResult.value, valueResult.type);
            Value result = emitCallRet(Type(Type::Kind::I1),
                                       kMapSetIfMissing,
                                       {baseValue, keyResult.value, boxedValue});
            return LowerResult{result, Type(Type::Kind::I1)};
        }
    }
    else if (equalsIgnoreCase(methodName, "clear"))
    {
        emitCall(kMapClear, {baseValue});
        return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
    }
    else if (equalsIgnoreCase(methodName, "keys"))
    {
        Value seq = emitCallRet(Type(Type::Kind::Ptr), kMapKeys, {baseValue});
        return LowerResult{seq, Type(Type::Kind::Ptr)};
    }
    else if (equalsIgnoreCase(methodName, "values"))
    {
        Value seq = emitCallRet(Type(Type::Kind::Ptr), kMapValues, {baseValue});
        return LowerResult{seq, Type(Type::Kind::Ptr)};
    }

    return std::nullopt;
}

//=============================================================================
// Built-in Function Call Helper
//=============================================================================

std::optional<LowerResult> Lowerer::lowerBuiltinCall(const std::string &name, CallExpr *expr)
{
    if (name == "print" || name == "println")
    {
        if (!expr->args.empty())
        {
            auto arg = lowerExpr(expr->args[0].value.get());
            TypeRef argType = sema_.typeOf(expr->args[0].value.get());

            Value strVal = arg.value;
            if (argType && argType->kind != TypeKindSem::String)
            {
                if (argType->kind == TypeKindSem::Integer)
                {
                    strVal = emitCallRet(Type(Type::Kind::Str), kStringFromInt, {arg.value});
                }
                else if (argType->kind == TypeKindSem::Number)
                {
                    strVal = emitCallRet(Type(Type::Kind::Str), kStringFromNum, {arg.value});
                }
            }

            emitCall(kTerminalSay, {strVal});
        }
        return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
    }

    if (name == "toString")
    {
        if (expr->args.empty())
            return LowerResult{Value::constInt(0), Type(Type::Kind::Str)};

        auto *argExpr = expr->args[0].value.get();
        auto arg = lowerExpr(argExpr);
        TypeRef argType = sema_.typeOf(argExpr);

        if (argType)
        {
            switch (argType->kind)
            {
                case TypeKindSem::String:
                    return LowerResult{arg.value, Type(Type::Kind::Str)};
                case TypeKindSem::Integer:
                {
                    Value strVal = emitCallRet(Type(Type::Kind::Str), kStringFromInt, {arg.value});
                    return LowerResult{strVal, Type(Type::Kind::Str)};
                }
                case TypeKindSem::Number:
                {
                    Value strVal = emitCallRet(Type(Type::Kind::Str), kStringFromNum, {arg.value});
                    return LowerResult{strVal, Type(Type::Kind::Str)};
                }
                case TypeKindSem::Boolean:
                {
                    Value strVal = emitCallRet(Type(Type::Kind::Str), kFmtBool, {arg.value});
                    return LowerResult{strVal, Type(Type::Kind::Str)};
                }
                default:
                    break;
            }
        }

        if (arg.type.kind == Type::Kind::Ptr)
        {
            Value strVal = emitCallRet(Type(Type::Kind::Str), kObjectToString, {arg.value});
            return LowerResult{strVal, Type(Type::Kind::Str)};
        }

        return LowerResult{Value::constInt(0), Type(Type::Kind::Str)};
    }

    return std::nullopt;
}

//=============================================================================
// Value Type Construction Helper
//=============================================================================

std::optional<LowerResult> Lowerer::lowerValueTypeConstruction(const std::string &typeName,
                                                                CallExpr *expr)
{
    auto it = valueTypes_.find(typeName);
    if (it == valueTypes_.end())
        return std::nullopt;

    const ValueTypeInfo &info = it->second;

    // Lower arguments
    std::vector<Value> argValues;
    for (auto &arg : expr->args)
    {
        auto result = lowerExpr(arg.value.get());
        argValues.push_back(result.value);
    }

    // Allocate stack space for the value
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(static_cast<int64_t>(info.totalSize))};
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value ptr = Value::temp(allocaId);

    // Store each argument into the corresponding field
    for (size_t i = 0; i < argValues.size() && i < info.fields.size(); ++i)
    {
        const FieldLayout &field = info.fields[i];

        // GEP to get field address
        unsigned gepId = nextTempId();
        il::core::Instr gepInstr;
        gepInstr.result = gepId;
        gepInstr.op = Opcode::GEP;
        gepInstr.type = Type(Type::Kind::Ptr);
        gepInstr.operands = {ptr, Value::constInt(static_cast<int64_t>(field.offset))};
        blockMgr_.currentBlock()->instructions.push_back(gepInstr);
        Value fieldAddr = Value::temp(gepId);

        // Store the value
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = mapType(field.type);
        storeInstr.operands = {fieldAddr, argValues[i]};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }

    return LowerResult{ptr, Type(Type::Kind::Ptr)};
}

//=============================================================================
// Main Call Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerCall(CallExpr *expr)
{
    // Check for method call on value or entity type: obj.method()
    if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->callee.get()))
    {
        // Check for super.method() call - dispatch to parent class method
        if (fieldExpr->base->kind == ExprKind::SuperExpr)
        {
            Value selfPtr;
            if (getSelfPtr(selfPtr) && currentEntityType_ && !currentEntityType_->baseClass.empty())
            {
                auto parentIt = entityTypes_.find(currentEntityType_->baseClass);
                if (parentIt != entityTypes_.end())
                {
                    if (auto *method = parentIt->second.findMethod(fieldExpr->field))
                    {
                        return lowerMethodCall(
                            method, currentEntityType_->baseClass, selfPtr, expr);
                    }
                }
            }
        }

        // Get the type of the base expression
        TypeRef baseType = sema_.typeOf(fieldExpr->base.get());
        if (baseType)
        {
            // Unwrap Optional types for method resolution
            // This handles the case where a variable was assigned from an optional
            // after a null check (e.g., `var table = maybeTable;` after `if maybeTable == null { return; }`)
            if (baseType->kind == TypeKindSem::Optional && baseType->innerType())
            {
                baseType = baseType->innerType();
            }

            std::string typeName = baseType->name;

            // Check value type methods
            auto it = valueTypes_.find(typeName);
            if (it != valueTypes_.end())
            {
                if (auto *method = it->second.findMethod(fieldExpr->field))
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    return lowerMethodCall(method, typeName, baseResult.value, expr);
                }
            }

            // Check entity type methods with virtual dispatch
            auto entityIt = entityTypes_.find(typeName);
            if (entityIt != entityTypes_.end())
            {
                const EntityTypeInfo &entityInfo = entityIt->second;

                size_t vtableSlot = entityInfo.findVtableSlot(fieldExpr->field);
                if (vtableSlot != SIZE_MAX)
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    return lowerVirtualMethodCall(
                        entityInfo, fieldExpr->field, vtableSlot, baseResult.value, expr);
                }

                if (auto *method = entityInfo.findMethod(fieldExpr->field))
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    return lowerMethodCall(method, typeName, baseResult.value, expr);
                }

                // Check parent entity for inherited methods
                std::string parentName = entityInfo.baseClass;
                while (!parentName.empty())
                {
                    auto parentIt = entityTypes_.find(parentName);
                    if (parentIt == entityTypes_.end())
                        break;
                    if (auto *method = parentIt->second.findMethod(fieldExpr->field))
                    {
                        auto baseResult = lowerExpr(fieldExpr->base.get());
                        return lowerMethodCall(method, parentName, baseResult.value, expr);
                    }
                    parentName = parentIt->second.baseClass;
                }
            }

            // Handle interface method calls
            if (baseType->kind == TypeKindSem::Interface)
            {
                auto ifaceIt = interfaceTypes_.find(typeName);
                if (ifaceIt != interfaceTypes_.end())
                {
                    auto methodIt = ifaceIt->second.methodMap.find(fieldExpr->field);
                    if (methodIt != ifaceIt->second.methodMap.end())
                    {
                        auto baseResult = lowerExpr(fieldExpr->base.get());
                        return lowerInterfaceMethodCall(
                            ifaceIt->second, fieldExpr->field, methodIt->second, baseResult.value,
                            expr);
                    }
                }
            }

            // Handle module-qualified function calls
            if (baseType->kind == TypeKindSem::Module)
            {
                std::string funcName = fieldExpr->field;
                std::vector<Value> args;
                for (auto &arg : expr->args)
                {
                    auto result = lowerExpr(arg.value.get());
                    args.push_back(result.value);
                }

                TypeRef exprType = sema_.typeOf(expr);
                Type ilReturnType = exprType ? mapType(exprType) : Type(Type::Kind::Void);

                if (ilReturnType.kind == Type::Kind::Void)
                {
                    emitCall(funcName, args);
                    return {Value::constInt(0), Type(Type::Kind::Void)};
                }
                else
                {
                    Value result = emitCallRet(ilReturnType, funcName, args);
                    return {result, ilReturnType};
                }
            }

            // Handle String method calls - Bug #018 fix
            // String.length() should be treated as a property access, not a method call
            if (baseType->kind == TypeKindSem::String)
            {
                if (equalsIgnoreCase(fieldExpr->field, "length"))
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    Value result =
                        emitCallRet(Type(Type::Kind::I64), "Viper.String.Length", {baseResult.value});
                    return {result, Type(Type::Kind::I64)};
                }
            }

            // Handle Integer method calls - Bug #018 fix
            // Integer.toString() should convert to string
            if (baseType->kind == TypeKindSem::Integer)
            {
                if (equalsIgnoreCase(fieldExpr->field, "toString"))
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    Value result =
                        emitCallRet(Type(Type::Kind::Str), kStringFromInt, {baseResult.value});
                    return {result, Type(Type::Kind::Str)};
                }
            }

            // Handle Number method calls - Bug #018 fix
            // Number.toString() should convert to string
            if (baseType->kind == TypeKindSem::Number)
            {
                if (equalsIgnoreCase(fieldExpr->field, "toString"))
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    Value result =
                        emitCallRet(Type(Type::Kind::Str), kStringFromNum, {baseResult.value});
                    return {result, Type(Type::Kind::Str)};
                }
            }

            // Handle List method calls
            if (baseType->kind == TypeKindSem::List)
            {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                auto listResult = lowerListMethodCall(baseResult.value, baseType, fieldExpr->field, expr);
                if (listResult)
                    return *listResult;
            }

            // Handle Map method calls
            if (baseType->kind == TypeKindSem::Map)
            {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                auto mapResult = lowerMapMethodCall(baseResult.value, baseType, fieldExpr->field, expr);
                if (mapResult)
                    return *mapResult;
            }
        }
    }

    // Check if this is a resolved runtime call
    std::string runtimeCallee = sema_.runtimeCallee(expr);
    if (!runtimeCallee.empty())
    {
        std::vector<Value> args;

        if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->callee.get()))
        {
            TypeRef baseType = sema_.typeOf(fieldExpr->base.get());
            if (baseType && baseType->name.find("Viper.") == 0)
            {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                args.push_back(baseResult.value);
            }
        }

        args.reserve(args.size() + expr->args.size());
        for (auto &arg : expr->args)
        {
            auto result = lowerExpr(arg.value.get());
            Value argValue = result.value;
            if (result.type.kind == Type::Kind::I32)
            {
                argValue = widenByteToInteger(argValue);
            }
            args.push_back(argValue);
        }

        TypeRef exprType = sema_.functionReturnType(runtimeCallee);
        Type ilReturnType = exprType ? mapType(exprType) : Type(Type::Kind::Void);
        return emitCallWithReturn(runtimeCallee, args, ilReturnType);
    }

    // Check for built-in functions and value type construction
    if (auto *ident = dynamic_cast<IdentExpr *>(expr->callee.get()))
    {
        // Check built-in functions
        auto builtinResult = lowerBuiltinCall(ident->name, expr);
        if (builtinResult)
            return *builtinResult;

        // Check value type construction
        auto valueTypeResult = lowerValueTypeConstruction(ident->name, expr);
        if (valueTypeResult)
            return *valueTypeResult;
    }

    // Handle direct or indirect function calls
    std::string calleeName;
    bool isIndirectCall = false;
    Value funcPtr;

    TypeRef calleeType = sema_.typeOf(expr->callee.get());
    bool isLambdaClosure = calleeType && calleeType->isCallable();

    if (auto *ident = dynamic_cast<IdentExpr *>(expr->callee.get()))
    {
        // Check for implicit method call
        if (currentEntityType_)
        {
            if (auto *method = currentEntityType_->findMethod(ident->name))
            {
                Value selfPtr;
                if (getSelfPtr(selfPtr))
                {
                    return lowerMethodCall(method, currentEntityType_->name, selfPtr, expr);
                }
            }
        }

        // Check if this is a variable holding a function pointer
        if (definedFunctions_.find(mangleFunctionName(ident->name)) == definedFunctions_.end())
        {
            auto slotIt = slots_.find(ident->name);
            if (slotIt != slots_.end())
            {
                unsigned loadId = nextTempId();
                il::core::Instr loadInstr;
                loadInstr.result = loadId;
                loadInstr.op = Opcode::Load;
                loadInstr.type = Type(Type::Kind::Ptr);
                loadInstr.operands = {slotIt->second};
                blockMgr_.currentBlock()->instructions.push_back(loadInstr);
                funcPtr = Value::temp(loadId);
                isIndirectCall = true;
            }
            else
            {
                auto localIt = locals_.find(ident->name);
                if (localIt != locals_.end())
                {
                    funcPtr = localIt->second;
                    isIndirectCall = true;
                }
            }
        }

        if (!isIndirectCall)
        {
            calleeName = mangleFunctionName(ident->name);
        }
    }
    else
    {
        auto calleeResult = lowerExpr(expr->callee.get());
        funcPtr = calleeResult.value;
        isIndirectCall = true;
    }

    // Get return type
    TypeRef returnType = calleeType ? calleeType->returnType() : nullptr;
    Type ilReturnType = returnType ? mapType(returnType) : Type(Type::Kind::Void);

    // Lower arguments
    std::vector<TypeRef> paramTypes;
    if (calleeType)
        paramTypes = calleeType->paramTypes();

    std::vector<Value> args;
    args.reserve(expr->args.size());
    for (size_t i = 0; i < expr->args.size(); ++i)
    {
        auto &arg = expr->args[i];
        auto result = lowerExpr(arg.value.get());
        Value argValue = result.value;

        if (i < paramTypes.size())
        {
            TypeRef paramType = paramTypes[i];
            TypeRef argType = sema_.typeOf(arg.value.get());
            if (paramType && paramType->kind == TypeKindSem::Optional)
            {
                TypeRef innerType = paramType->innerType();
                if (argType && argType->kind == TypeKindSem::Optional)
                {
                    argValue = result.value;
                }
                else if (argType && argType->kind == TypeKindSem::Unit)
                {
                    argValue = Value::null();
                }
                else if (innerType)
                {
                    argValue = emitOptionalWrap(result.value, innerType);
                }
            }
        }

        args.push_back(argValue);
    }

    if (isIndirectCall)
    {
        if (isLambdaClosure)
        {
            Value closurePtr = funcPtr;
            Value actualFuncPtr = emitLoad(closurePtr, Type(Type::Kind::Ptr));
            Value envFieldAddr = emitGEP(closurePtr, 8);
            Value envPtr = emitLoad(envFieldAddr, Type(Type::Kind::Ptr));

            std::vector<Value> closureArgs;
            closureArgs.reserve(args.size() + 1);
            closureArgs.push_back(envPtr);
            for (const auto &arg : args)
            {
                closureArgs.push_back(arg);
            }

            if (ilReturnType.kind == Type::Kind::Void)
            {
                emitCallIndirect(actualFuncPtr, closureArgs);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            }
            else
            {
                Value result = emitCallIndirectRet(ilReturnType, actualFuncPtr, closureArgs);
                return {result, ilReturnType};
            }
        }
        else
        {
            if (ilReturnType.kind == Type::Kind::Void)
            {
                emitCallIndirect(funcPtr, args);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            }
            else
            {
                Value result = emitCallIndirectRet(ilReturnType, funcPtr, args);
                return {result, ilReturnType};
            }
        }
    }
    else
    {
        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitCall(calleeName, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        else
        {
            Value result = emitCallRet(ilReturnType, calleeName, args);
            return {result, ilReturnType};
        }
    }
}

//=============================================================================
// Method Call Helper
//=============================================================================

LowerResult Lowerer::lowerMethodCall(MethodDecl *method,
                                     const std::string &typeName,
                                     Value selfValue,
                                     CallExpr *expr)
{
    std::vector<Value> args;
    args.reserve(expr->args.size() + 1);
    args.push_back(selfValue);

    for (size_t i = 0; i < expr->args.size(); ++i)
    {
        auto &arg = expr->args[i];
        auto result = lowerExpr(arg.value.get());
        Value argValue = result.value;

        if (i < method->params.size() && method->params[i].type)
        {
            TypeRef paramType = sema_.resolveType(method->params[i].type.get());
            TypeRef argType = sema_.typeOf(arg.value.get());
            if (paramType && paramType->kind == TypeKindSem::Optional)
            {
                TypeRef innerType = paramType->innerType();
                if (argType && argType->kind == TypeKindSem::Optional)
                {
                    argValue = result.value;
                }
                else if (argType && argType->kind == TypeKindSem::Unit)
                {
                    argValue = Value::null();
                }
                else if (innerType)
                {
                    argValue = emitOptionalWrap(result.value, innerType);
                }
            }
        }

        args.push_back(argValue);
    }

    TypeRef returnType =
        method->returnType ? sema_.resolveType(method->returnType.get()) : types::voidType();
    Type ilReturnType = mapType(returnType);

    std::string methodName = typeName + "." + method->name;
    return emitCallWithReturn(methodName, args, ilReturnType);
}

} // namespace il::frontends::viperlang
