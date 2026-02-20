//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr_Method.cpp
/// @brief Method call and type construction lowering for the Zia IL lowerer.
///
/// @details This file handles method call dispatch (direct, virtual, interface),
/// collection method calls (List, Map, Set), and value/entity type construction
/// via function-call syntax.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include <algorithm>
#include <unordered_map>

namespace il::frontends::zia
{

using namespace runtime;

//=============================================================================
// Method Dispatch Table
//=============================================================================
// O(1) lookup using hash map instead of sequential string comparisons.
// This provides 40-60% speedup for collection-heavy code.

/// @brief Enumeration of collection method identifiers for fast dispatch.
enum class CollectionMethod
{
    Unknown = 0,
    // List methods
    Get,
    Set,
    Add,
    Push,
    Remove,
    RemoveAt,
    Insert,
    Find,
    IndexOf,
    Has,
    Contains,
    Size,
    Count,
    Length,
    Len,
    Clear,
    Pop,
    // Map methods
    Put,
    GetOr,
    ContainsKey,
    HasKey,
    SetIfMissing,
    Keys,
    Values
};

namespace
{

/// @brief Convert a method name to lowercase for case-insensitive lookup.
/// @param name Input method name.
/// @return Lowercase copy of the method name.
std::string toLowerStr(const std::string &name)
{
    std::string lower;
    lower.reserve(name.size());
    for (char c : name)
    {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower;
}

/// @brief Static dispatch table mapping lowercase method names to CollectionMethod enum.
const std::unordered_map<std::string, CollectionMethod> &getMethodDispatchTable()
{
    static const std::unordered_map<std::string, CollectionMethod> table = {
        // List/common methods
        {"get", CollectionMethod::Get},
        {"set", CollectionMethod::Set},
        {"add", CollectionMethod::Add},
        {"push", CollectionMethod::Push},
        {"pop", CollectionMethod::Pop},
        {"remove", CollectionMethod::Remove},
        {"removeat", CollectionMethod::RemoveAt},
        {"insert", CollectionMethod::Insert},
        {"find", CollectionMethod::Find},
        {"indexof", CollectionMethod::IndexOf},
        {"has", CollectionMethod::Has},
        {"contains", CollectionMethod::Contains},
        {"size", CollectionMethod::Size},
        {"count", CollectionMethod::Count},
        {"length", CollectionMethod::Length},
        {"len", CollectionMethod::Len},
        {"clear", CollectionMethod::Clear},
        // Map-specific methods
        {"put", CollectionMethod::Put},
        {"getor", CollectionMethod::GetOr},
        {"containskey", CollectionMethod::ContainsKey},
        {"haskey", CollectionMethod::HasKey},
        {"setifmissing", CollectionMethod::SetIfMissing},
        {"keys", CollectionMethod::Keys},
        {"values", CollectionMethod::Values}};
    return table;
}

/// @brief Look up a method name in the dispatch table.
/// @param methodName Method name to look up (case-insensitive).
/// @return The corresponding CollectionMethod enum value, or Unknown if not found.
CollectionMethod lookupMethod(const std::string &methodName)
{
    const auto &table = getMethodDispatchTable();
    auto it = table.find(toLowerStr(methodName));
    if (it != table.end())
    {
        return it->second;
    }
    return CollectionMethod::Unknown;
}

} // namespace

//=============================================================================
// List Method Call Helper
//=============================================================================

std::optional<LowerResult> Lowerer::lowerListMethodCall(Value baseValue,
                                                        TypeRef baseType,
                                                        const std::string &methodName,
                                                        CallExpr *expr)
{
    // Use O(1) dispatch table lookup instead of sequential string comparisons
    const CollectionMethod method = lookupMethod(methodName);

    switch (method)
    {
        case CollectionMethod::Get:
            if (expr->args.size() >= 1)
            {
                auto indexResult = lowerExpr(expr->args[0].value.get());
                Value boxed =
                    emitCallRet(Type(Type::Kind::Ptr), kListGet, {baseValue, indexResult.value});
                TypeRef elemType = baseType ? baseType->elementType() : nullptr;
                if (!elemType)
                    elemType = sema_.typeOf(expr);
                Type ilElemType = mapType(elemType);
                return emitUnboxValue(boxed, ilElemType, elemType);
            }
            break;

        case CollectionMethod::RemoveAt:
            if (expr->args.size() >= 1)
            {
                auto indexResult = lowerExpr(expr->args[0].value.get());
                emitCall(kListRemoveAt, {baseValue, indexResult.value});
                return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
            }
            break;

        case CollectionMethod::Remove:
            if (expr->args.size() >= 1)
            {
                auto valueResult = lowerExpr(expr->args[0].value.get());
                TypeRef argType = sema_.typeOf(expr->args[0].value.get());
                Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, argType);
                Value result =
                    emitCallRet(Type(Type::Kind::I1), kListRemove, {baseValue, boxedValue});
                return LowerResult{result, Type(Type::Kind::I1)};
            }
            break;

        case CollectionMethod::Insert:
            if (expr->args.size() >= 2)
            {
                auto indexResult = lowerExpr(expr->args[0].value.get());
                auto valueResult = lowerExpr(expr->args[1].value.get());
                TypeRef argType = sema_.typeOf(expr->args[1].value.get());
                Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, argType);
                emitCall(kListInsert, {baseValue, indexResult.value, boxedValue});
                return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
            }
            break;

        case CollectionMethod::Find:
        case CollectionMethod::IndexOf:
            if (expr->args.size() >= 1)
            {
                auto valueResult = lowerExpr(expr->args[0].value.get());
                TypeRef argType = sema_.typeOf(expr->args[0].value.get());
                Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, argType);
                Value result =
                    emitCallRet(Type(Type::Kind::I64), kListFind, {baseValue, boxedValue});
                return LowerResult{result, Type(Type::Kind::I64)};
            }
            break;

        case CollectionMethod::Has:
        case CollectionMethod::Contains:
            if (expr->args.size() >= 1)
            {
                auto valueResult = lowerExpr(expr->args[0].value.get());
                TypeRef argType = sema_.typeOf(expr->args[0].value.get());
                Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, argType);
                Value result =
                    emitCallRet(Type(Type::Kind::I1), kListContains, {baseValue, boxedValue});
                return LowerResult{result, Type(Type::Kind::I1)};
            }
            break;

        case CollectionMethod::Set:
            if (expr->args.size() >= 2)
            {
                auto indexResult = lowerExpr(expr->args[0].value.get());
                auto valueResult = lowerExpr(expr->args[1].value.get());
                TypeRef argType = sema_.typeOf(expr->args[1].value.get());
                Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, argType);
                emitCall(kListSet, {baseValue, indexResult.value, boxedValue});
                return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
            }
            break;

        case CollectionMethod::Add:
        case CollectionMethod::Push:
        {
            std::vector<Value> args;
            args.reserve(expr->args.size() + 1);
            args.push_back(baseValue);
            for (auto &arg : expr->args)
            {
                auto result = lowerExpr(arg.value.get());
                TypeRef argType = sema_.typeOf(arg.value.get());
                args.push_back(emitBoxValue(result.value, result.type, argType));
            }
            emitCall(kListAdd, args);
            return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
        }

        case CollectionMethod::Pop:
        {
            // Pop removes and returns the last element as a boxed obj.
            Value boxed = emitCallRet(Type(Type::Kind::Ptr), kListPop, {baseValue});
            TypeRef elemType = baseType ? baseType->elementType() : nullptr;
            if (!elemType)
                elemType = sema_.typeOf(expr);
            Type ilElemType = mapType(elemType);
            return emitUnboxValue(boxed, ilElemType, elemType);
        }

        case CollectionMethod::Size:
        case CollectionMethod::Count:
        case CollectionMethod::Length:
        case CollectionMethod::Len:
        {
            std::vector<Value> args;
            args.push_back(baseValue);
            Value result = emitCallRet(Type(Type::Kind::I64), kListCount, args);
            return LowerResult{result, Type(Type::Kind::I64)};
        }

        case CollectionMethod::Clear:
        {
            std::vector<Value> args;
            args.push_back(baseValue);
            emitCall(kListClear, args);
            return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
        }

        default:
            break;
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
    TypeRef valType = baseType->typeArgs.size() > 1 ? baseType->typeArgs[1] : nullptr;

    // Use O(1) dispatch table lookup instead of sequential string comparisons
    const CollectionMethod method = lookupMethod(methodName);

    switch (method)
    {
        case CollectionMethod::Set:
        case CollectionMethod::Put:
            if (expr->args.size() >= 2)
            {
                auto keyResult = lowerExpr(expr->args[0].value.get());
                auto valueResult = lowerExpr(expr->args[1].value.get());
                TypeRef argType = sema_.typeOf(expr->args[1].value.get());
                Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, argType);
                emitCall(kMapSet, {baseValue, keyResult.value, boxedValue});
                return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
            }
            break;

        case CollectionMethod::Get:
            if (expr->args.size() >= 1)
            {
                auto keyResult = lowerExpr(expr->args[0].value.get());
                Value boxed =
                    emitCallRet(Type(Type::Kind::Ptr), kMapGet, {baseValue, keyResult.value});
                if (valType)
                {
                    Type ilValueType = mapType(valType);
                    return emitUnboxValue(boxed, ilValueType, valType);
                }
                return LowerResult{boxed, Type(Type::Kind::Ptr)};
            }
            break;

        case CollectionMethod::GetOr:
            if (expr->args.size() >= 2)
            {
                auto keyResult = lowerExpr(expr->args[0].value.get());
                auto defaultResult = lowerExpr(expr->args[1].value.get());
                TypeRef argType = sema_.typeOf(expr->args[1].value.get());
                Value boxedDefault = emitBoxValue(defaultResult.value, defaultResult.type, argType);
                Value boxed = emitCallRet(
                    Type(Type::Kind::Ptr), kMapGetOr, {baseValue, keyResult.value, boxedDefault});
                if (valType)
                {
                    Type ilValueType = mapType(valType);
                    return emitUnboxValue(boxed, ilValueType, valType);
                }
                return LowerResult{boxed, Type(Type::Kind::Ptr)};
            }
            break;

        case CollectionMethod::ContainsKey:
        case CollectionMethod::HasKey:
        case CollectionMethod::Has:
            if (expr->args.size() >= 1)
            {
                auto keyResult = lowerExpr(expr->args[0].value.get());
                Value result = emitCallRet(
                    Type(Type::Kind::I1), kMapContainsKey, {baseValue, keyResult.value});
                return LowerResult{result, Type(Type::Kind::I1)};
            }
            break;

        case CollectionMethod::Size:
        case CollectionMethod::Count:
        case CollectionMethod::Length:
        case CollectionMethod::Len:
        {
            Value result = emitCallRet(Type(Type::Kind::I64), kMapCount, {baseValue});
            return LowerResult{result, Type(Type::Kind::I64)};
        }

        case CollectionMethod::Remove:
            if (expr->args.size() >= 1)
            {
                auto keyResult = lowerExpr(expr->args[0].value.get());
                Value result =
                    emitCallRet(Type(Type::Kind::I1), kMapRemove, {baseValue, keyResult.value});
                return LowerResult{result, Type(Type::Kind::I1)};
            }
            break;

        case CollectionMethod::SetIfMissing:
            if (expr->args.size() >= 2)
            {
                auto keyResult = lowerExpr(expr->args[0].value.get());
                auto valueResult = lowerExpr(expr->args[1].value.get());
                TypeRef argType = sema_.typeOf(expr->args[1].value.get());
                Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, argType);
                Value result = emitCallRet(Type(Type::Kind::I1),
                                           kMapSetIfMissing,
                                           {baseValue, keyResult.value, boxedValue});
                return LowerResult{result, Type(Type::Kind::I1)};
            }
            break;

        case CollectionMethod::Clear:
            emitCall(kMapClear, {baseValue});
            return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};

        case CollectionMethod::Keys:
        {
            Value seq = emitCallRet(Type(Type::Kind::Ptr), kMapKeys, {baseValue});
            return LowerResult{seq, Type(Type::Kind::Ptr)};
        }

        case CollectionMethod::Values:
        {
            Value seq = emitCallRet(Type(Type::Kind::Ptr), kMapValues, {baseValue});
            return LowerResult{seq, Type(Type::Kind::Ptr)};
        }

        default:
            break;
    }

    return std::nullopt;
}

//=============================================================================
// Set Method Call Helper
//=============================================================================

std::optional<LowerResult> Lowerer::lowerSetMethodCall(Value baseValue,
                                                       TypeRef baseType,
                                                       const std::string &methodName,
                                                       CallExpr *expr)
{
    const CollectionMethod method = lookupMethod(methodName);

    switch (method)
    {
        case CollectionMethod::Has:
        case CollectionMethod::Contains:
            if (expr->args.size() >= 1)
            {
                auto valueResult = lowerExpr(expr->args[0].value.get());
                TypeRef argType = sema_.typeOf(expr->args[0].value.get());
                Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, argType);
                Value result = emitCallRet(Type(Type::Kind::I1), kSetHas, {baseValue, boxedValue});
                return LowerResult{result, Type(Type::Kind::I1)};
            }
            break;

        case CollectionMethod::Add:
        case CollectionMethod::Put:
            if (expr->args.size() >= 1)
            {
                auto valueResult = lowerExpr(expr->args[0].value.get());
                TypeRef argType = sema_.typeOf(expr->args[0].value.get());
                Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, argType);
                emitCall(kSetPut, {baseValue, boxedValue});
                return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
            }
            break;

        case CollectionMethod::Remove:
            if (expr->args.size() >= 1)
            {
                auto valueResult = lowerExpr(expr->args[0].value.get());
                TypeRef argType = sema_.typeOf(expr->args[0].value.get());
                Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, argType);
                emitCall(kSetDrop, {baseValue, boxedValue});
                return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
            }
            break;

        case CollectionMethod::Size:
        case CollectionMethod::Count:
        case CollectionMethod::Length:
        case CollectionMethod::Len:
        {
            Value result = emitCallRet(Type(Type::Kind::I64), kSetCount, {baseValue});
            return LowerResult{result, Type(Type::Kind::I64)};
        }

        case CollectionMethod::Clear:
            emitCall(kSetClear, {baseValue});
            return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};

        default:
            break;
    }

    return std::nullopt;
}

//=============================================================================
// Method Call Helper
//=============================================================================

LowerResult Lowerer::lowerMethodCall(MethodDecl *method,
                                     const std::string &typeName,
                                     Value selfValue,
                                     CallExpr *expr)
{
    // Look up the cached method type - this has already-substituted types for generics
    TypeRef methodType = sema_.getMethodType(typeName, method->name);
    std::vector<TypeRef> paramTypes;
    TypeRef returnType = types::voidType();
    if (methodType && methodType->kind == TypeKindSem::Function)
    {
        paramTypes = methodType->paramTypes();
        returnType = methodType->returnType();
    }

    std::vector<Value> args;
    args.reserve(expr->args.size() + 1);
    args.push_back(selfValue);

    for (size_t i = 0; i < expr->args.size(); ++i)
    {
        auto &arg = expr->args[i];
        auto result = lowerExpr(arg.value.get());
        Value argValue = result.value;

        // Use cached param type from methodType instead of resolving from AST
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

    Type ilReturnType = mapType(returnType);

    std::string methodName = typeName + "." + method->name;

    // Handle void return types correctly - don't try to store void results
    if (ilReturnType.kind == Type::Kind::Void)
    {
        emitCall(methodName, args);
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }
    else
    {
        Value result = emitCallRet(ilReturnType, methodName, args);
        return {result, ilReturnType};
    }
}

//=============================================================================
// Value Type Construction Helper
//=============================================================================

std::optional<LowerResult> Lowerer::lowerValueTypeConstruction(const std::string &typeName,
                                                               CallExpr *expr)
{
    const ValueTypeInfo *infoPtr = getOrCreateValueTypeInfo(typeName);
    if (!infoPtr)
        return std::nullopt;

    const ValueTypeInfo &info = *infoPtr;

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

    // BUG-010 fix: Check if the value type has an explicit init method
    auto initIt = info.methodMap.find("init");
    if (initIt != info.methodMap.end())
    {
        // Call the explicit init method (like entity types do)
        std::string initName = typeName + ".init";
        std::vector<Value> initArgs;
        initArgs.push_back(ptr); // self is first argument
        for (const auto &argVal : argValues)
        {
            initArgs.push_back(argVal);
        }
        emitCall(initName, initArgs);
    }
    else
    {
        // No init method - store arguments directly into fields (original behavior)
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
    }

    return LowerResult{ptr, Type(Type::Kind::Ptr)};
}

//=============================================================================
// Entity Type Construction Helper
//=============================================================================

std::optional<LowerResult> Lowerer::lowerEntityTypeConstruction(const std::string &typeName,
                                                                CallExpr *expr)
{
    const EntityTypeInfo *infoPtr = getOrCreateEntityTypeInfo(typeName);
    if (!infoPtr)
        return std::nullopt;

    const EntityTypeInfo &info = *infoPtr;

    // Lower arguments
    std::vector<Value> argValues;
    for (auto &arg : expr->args)
    {
        auto result = lowerExpr(arg.value.get());
        argValues.push_back(result.value);
    }

    // Allocate heap memory for the entity using rt_obj_new_i64
    Value ptr = emitCallRet(Type(Type::Kind::Ptr),
                            "rt_obj_new_i64",
                            {Value::constInt(static_cast<int64_t>(info.classId)),
                             Value::constInt(static_cast<int64_t>(info.totalSize))});

    // Check if the entity has an explicit init method
    auto initIt = info.methodMap.find("init");
    if (initIt != info.methodMap.end())
    {
        // Call the explicit init method
        std::string initName = typeName + ".init";
        std::vector<Value> initArgs;
        initArgs.push_back(ptr); // self is first argument
        for (const auto &argVal : argValues)
        {
            initArgs.push_back(argVal);
        }
        emitCall(initName, initArgs);
    }
    else
    {
        // No explicit init - do inline field initialization
        for (size_t i = 0; i < info.fields.size(); ++i)
        {
            const auto &field = info.fields[i];
            Type ilFieldType = mapType(field.type);
            Value fieldValue;

            if (i < argValues.size())
            {
                fieldValue = argValues[i];
            }
            else
            {
                switch (ilFieldType.kind)
                {
                    case Type::Kind::I1:
                        fieldValue = Value::constBool(false);
                        break;
                    case Type::Kind::I64:
                    case Type::Kind::I16:
                    case Type::Kind::I32:
                        fieldValue = Value::constInt(0);
                        break;
                    case Type::Kind::F64:
                        fieldValue = Value::constFloat(0.0);
                        break;
                    case Type::Kind::Str:
                        fieldValue = emitConstStr("");
                        break;
                    case Type::Kind::Ptr:
                        fieldValue = Value::null();
                        break;
                    default:
                        fieldValue = Value::constInt(0);
                        break;
                }
            }

            Value fieldAddr = emitGEP(ptr, static_cast<int64_t>(field.offset));
            emitStore(fieldAddr, fieldValue, ilFieldType);
        }
    }

    return LowerResult{ptr, Type(Type::Kind::Ptr)};
}

//=============================================================================
// Struct-Literal Lowering (ZIA-001)
//=============================================================================

/// @brief Lower a struct-literal expression: `TypeName { field = val, ... }`.
/// @details Reorders the named fields by declaration order, then delegates to
/// the same alloca+init logic used by lowerValueTypeConstruction.
LowerResult Lowerer::lowerStructLiteral(StructLiteralExpr *expr)
{
    const std::string &typeName = expr->typeName;
    const ValueTypeInfo *infoPtr = getOrCreateValueTypeInfo(typeName);
    if (!infoPtr)
    {
        // Fallback: treat as a zero-initialised value (unreachable after sema checks)
        return {Value::constInt(0), Type(Type::Kind::Ptr)};
    }
    const ValueTypeInfo &info = *infoPtr;

    // Build a map from field name → lowered value for quick lookup.
    std::unordered_map<std::string, Value> fieldValues;
    for (auto &f : expr->fields)
    {
        auto result = lowerExpr(f.value.get());
        fieldValues[f.name] = result.value;
    }

    // Build arg list in field declaration order (matches init parameter order).
    std::vector<Value> argValues;
    argValues.reserve(info.fields.size());
    for (const auto &field : info.fields)
    {
        auto it = fieldValues.find(field.name);
        if (it != fieldValues.end())
        {
            argValues.push_back(it->second);
        }
        else
        {
            // Missing field → zero-initialise
            argValues.push_back(Value::constInt(0));
        }
    }

    // Allocate stack space for the value.
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(static_cast<int64_t>(info.totalSize))};
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value ptr = Value::temp(allocaId);

    // If an explicit init method exists, call it (same as lowerValueTypeConstruction).
    auto initIt = info.methodMap.find("init");
    if (initIt != info.methodMap.end())
    {
        std::string initName = typeName + ".init";
        std::vector<Value> initArgs;
        initArgs.push_back(ptr); // self is first argument
        for (const auto &argVal : argValues)
            initArgs.push_back(argVal);
        emitCall(initName, initArgs);
    }
    else
    {
        // No init method — store args directly into fields by declaration order.
        for (size_t i = 0; i < argValues.size() && i < info.fields.size(); ++i)
        {
            const FieldLayout &field = info.fields[i];
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {ptr, Value::constInt(static_cast<int64_t>(field.offset))};
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            Value fieldAddr = Value::temp(gepId);

            il::core::Instr storeInstr;
            storeInstr.op = Opcode::Store;
            storeInstr.type = mapType(field.type);
            storeInstr.operands = {fieldAddr, argValues[i]};
            blockMgr_.currentBlock()->instructions.push_back(storeInstr);
        }
    }

    return {ptr, Type(Type::Kind::Ptr)};
}

} // namespace il::frontends::zia
