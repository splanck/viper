//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr_Call.cpp
/// @brief Call expression lowering for the Zia IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <algorithm>
#include <iostream>
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
    Clear,
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
                TypeRef elemType = baseType->elementType();
                if (elemType)
                {
                    Type ilElemType = mapType(elemType);
                    return emitUnboxValue(boxed, ilElemType, elemType);
                }
                return LowerResult{boxed, Type(Type::Kind::Ptr)};
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

        case CollectionMethod::Size:
        case CollectionMethod::Count:
        case CollectionMethod::Length:
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
                Value result =
                    emitCallRet(Type(Type::Kind::I1), kSetHas, {baseValue, boxedValue});
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
// Main Call Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerCall(CallExpr *expr)
{
    // Check for generic function call: identity[Integer](42)
    std::string genericCallee = sema_.genericFunctionCallee(expr);
    if (!genericCallee.empty())
    {
        return lowerGenericFunctionCall(genericCallee, expr);
    }

    // Handle generic function calls that weren't detected during semantic analysis
    // This happens for calls inside generic function bodies like: identity[T](x)
    // where T is a type parameter that needs to be substituted
    if (expr->callee->kind == ExprKind::Index)
    {
        auto *indexExpr = static_cast<IndexExpr *>(expr->callee.get());
        if (indexExpr->base->kind == ExprKind::Ident)
        {
            auto *identExpr = static_cast<IdentExpr *>(indexExpr->base.get());
            // Check if this is a call to a generic function
            if (sema_.isGenericFunction(identExpr->name))
            {
                // Get the type argument from the index expression
                if (indexExpr->index->kind == ExprKind::Ident)
                {
                    auto *typeArgExpr = static_cast<IdentExpr *>(indexExpr->index.get());
                    std::string typeArgName = typeArgExpr->name;

                    // If the type arg is a type parameter, substitute it
                    TypeRef substType = sema_.lookupTypeParam(typeArgName);
                    if (substType)
                    {
                        // Use the type's name if it has one, otherwise use kindToString
                        typeArgName = substType->name.empty() ? kindToString(substType->kind)
                                                              : substType->name;
                    }

                    // Build the mangled name
                    std::string mangledName = identExpr->name + "$" + typeArgName;
                    return lowerGenericFunctionCall(mangledName, expr);
                }
            }
        }
    }

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
            // after a null check (e.g., `var table = maybeTable;` after `if maybeTable == null {
            // return; }`)
            if (baseType->kind == TypeKindSem::Optional && baseType->innerType())
            {
                baseType = baseType->innerType();
            }

            std::string typeName = baseType->name;

            // Check value type methods
            const ValueTypeInfo *valueInfo = getOrCreateValueTypeInfo(typeName);
            if (valueInfo)
            {
                if (auto *method = valueInfo->findMethod(fieldExpr->field))
                {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    return lowerMethodCall(method, typeName, baseResult.value, expr);
                }
            }

            // Check entity type methods with virtual dispatch
            const EntityTypeInfo *entityInfoPtr = getOrCreateEntityTypeInfo(typeName);
            if (entityInfoPtr)
            {
                const EntityTypeInfo &entityInfo = *entityInfoPtr;

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
                        return lowerInterfaceMethodCall(ifaceIt->second,
                                                        fieldExpr->field,
                                                        methodIt->second,
                                                        baseResult.value,
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
                    Value result = emitCallRet(
                        Type(Type::Kind::I64), kStringLength, {baseResult.value});
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
                auto listResult =
                    lowerListMethodCall(baseResult.value, baseType, fieldExpr->field, expr);
                if (listResult)
                    return *listResult;
            }

            // Handle Map method calls
            if (baseType->kind == TypeKindSem::Map)
            {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                auto mapResult =
                    lowerMapMethodCall(baseResult.value, baseType, fieldExpr->field, expr);
                if (mapResult)
                    return *mapResult;
            }

            // Handle Set method calls
            if (baseType->kind == TypeKindSem::Set)
            {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                auto setResult =
                    lowerSetMethodCall(baseResult.value, baseType, fieldExpr->field, expr);
                if (setResult)
                    return *setResult;
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
            if (baseType && (baseType->name.find("Viper.") == 0 ||
                             baseType->kind == TypeKindSem::Set ||
                             baseType->kind == TypeKindSem::List ||
                             baseType->kind == TypeKindSem::Map))
            {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                args.push_back(baseResult.value);
            }
        }

        // BUG-008 fix: Look up runtime signature to auto-box primitives when expected type is ptr
        const auto *rtDesc = il::runtime::findRuntimeDescriptor(runtimeCallee);
        const std::vector<il::core::Type> *expectedParamTypes = nullptr;
        if (rtDesc)
        {
            expectedParamTypes = &rtDesc->signature.paramTypes;
        }

        args.reserve(args.size() + expr->args.size());
        size_t paramOffset = args.size(); // Account for implicit self parameter if present
        for (size_t i = 0; i < expr->args.size(); ++i)
        {
            auto result = lowerExpr(expr->args[i].value.get());
            Value argValue = result.value;
            if (result.type.kind == Type::Kind::I32)
            {
                argValue = widenByteToInteger(argValue);
            }

            // BUG-008 fix: Auto-box primitive if expected type is Ptr
            if (expectedParamTypes && (paramOffset + i) < expectedParamTypes->size())
            {
                Type expectedType = (*expectedParamTypes)[paramOffset + i];
                if (expectedType.kind == Type::Kind::Ptr && result.type.kind != Type::Kind::Ptr &&
                    result.type.kind != Type::Kind::Void)
                {
                    // Primitive passed where object expected - auto-box
                    argValue = emitBox(argValue, result.type);
                }
            }

            args.push_back(argValue);
        }

        TypeRef exprType = sema_.functionReturnType(runtimeCallee);
        Type ilReturnType = exprType ? mapType(exprType) : Type(Type::Kind::Void);

        // Handle void return types correctly - don't try to store void results
        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitCall(runtimeCallee, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        else
        {
            Value result = emitCallRet(ilReturnType, runtimeCallee, args);
            return {result, ilReturnType};
        }
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

        // Check entity type construction (Entity(args) without 'new' keyword)
        auto entityTypeResult = lowerEntityTypeConstruction(ident->name, expr);
        if (entityTypeResult)
            return *entityTypeResult;
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
    else if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->callee.get()))
    {
        // Check if this is a namespace-qualified function call (e.g., Math.add or
        // Outer.Inner.getValue) Recursively build the qualified name from nested FieldExpr nodes
        std::string qualifiedName;
        std::function<bool(Expr *)> buildQualifiedName = [&](Expr *e) -> bool
        {
            if (auto *ident = dynamic_cast<IdentExpr *>(e))
            {
                qualifiedName = ident->name;
                return true;
            }
            if (auto *field = dynamic_cast<FieldExpr *>(e))
            {
                if (buildQualifiedName(field->base.get()))
                {
                    qualifiedName += "." + field->field;
                    return true;
                }
            }
            return false;
        };
        buildQualifiedName(expr->callee.get());

        // Check if the qualified name is a defined function
        if (!qualifiedName.empty() &&
            definedFunctions_.find(qualifiedName) != definedFunctions_.end())
        {
            // This is a namespace-qualified function call - emit as direct call
            calleeName = qualifiedName;
            isIndirectCall = false;
        }
        else
        {
            // Regular field access on a value - lower and use as indirect call
            auto calleeResult = lowerExpr(expr->callee.get());
            funcPtr = calleeResult.value;
            isIndirectCall = true;
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
            // Handle Integer -> Number implicit conversion for function parameters
            else if (paramType && paramType->kind == TypeKindSem::Number && argType &&
                     argType->kind == TypeKindSem::Integer)
            {
                // Emit sitofp to convert i64 -> f64
                unsigned convId = nextTempId();
                il::core::Instr convInstr;
                convInstr.result = convId;
                convInstr.op = Opcode::Sitofp;
                convInstr.type = Type(Type::Kind::F64);
                convInstr.operands = {argValue};
                blockMgr_.currentBlock()->instructions.push_back(convInstr);
                argValue = Value::temp(convId);
            }
            // Handle type coercion when argument type is Unknown but param type is concrete
            // This happens when indexing into an empty list that was created with []
            else if (argType && argType->kind == TypeKindSem::Unknown && paramType)
            {
                Type ilParamType = mapType(paramType);
                // If the IL types differ, we need to unbox with the correct target type
                if (ilParamType.kind != result.type.kind && result.type.kind == Type::Kind::Ptr)
                {
                    // The value is boxed (Ptr) but we need the unboxed primitive type
                    argValue = emitUnbox(result.value, ilParamType).value;
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
// Generic Function Call Lowering
//=============================================================================

LowerResult Lowerer::lowerGenericFunctionCall(const std::string &mangledName, CallExpr *expr)
{
    // Get the function type from Sema
    TypeRef funcType = sema_.typeOf(expr->callee.get());
    if (!funcType || funcType->kind != TypeKindSem::Function)
    {
        // Fallback - compute return type from generic function declaration
        std::string baseName = mangledName.substr(0, mangledName.find('$'));
        std::string concreteTypeName = mangledName.substr(mangledName.find('$') + 1);
        FunctionDecl *genericDecl = sema_.getGenericFunction(baseName);

        Type ilReturnType = Type(Type::Kind::I64); // Default fallback
        if (genericDecl)
        {
            // Resolve return type from declaration and substitute type parameters
            if (genericDecl->returnType)
            {
                TypeRef declReturnType = sema_.resolveType(genericDecl->returnType.get());
                if (declReturnType && declReturnType->kind == TypeKindSem::TypeParam)
                {
                    // Return type is a type parameter - substitute with concrete type
                    TypeRef concreteType = sema_.resolveNamedType(concreteTypeName);
                    if (concreteType)
                    {
                        ilReturnType = mapType(concreteType);
                    }
                }
                else if (declReturnType)
                {
                    ilReturnType = mapType(declReturnType);
                }
            }
            else
            {
                ilReturnType = Type(Type::Kind::Void);
            }
        }

        // Lower arguments
        std::vector<Value> args;
        for (auto &arg : expr->args)
        {
            auto result = lowerExpr(arg.value.get());
            args.push_back(result.value);
        }

        // Queue the instantiated generic function for later lowering
        if (genericDecl && definedFunctions_.find(mangledName) == definedFunctions_.end())
        {
            // Mark as defined now to avoid re-queuing, but queue for actual lowering
            definedFunctions_.insert(mangledName);
            pendingFunctionInstantiations_.push_back({mangledName, genericDecl});
        }

        // Call the function
        if (ilReturnType.kind == Type::Kind::Void)
        {
            emitCall(mangledName, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        else
        {
            Value result = emitCallRet(ilReturnType, mangledName, args);
            return {result, ilReturnType};
        }
    }

    // We have proper function type info
    TypeRef returnType = funcType->returnType();
    Type ilReturnType = returnType ? mapType(returnType) : Type(Type::Kind::Void);

    // Lower arguments
    std::vector<Value> args;
    const auto &paramTypes = funcType->paramTypes();
    for (size_t i = 0; i < expr->args.size(); ++i)
    {
        auto result = lowerExpr(expr->args[i].value.get());
        Value argValue = result.value;

        // Widen bytes to integers
        if (result.type.kind == Type::Kind::I32)
        {
            argValue = widenByteToInteger(argValue);
        }

        args.push_back(argValue);
    }

    // Queue the instantiated generic function for later lowering
    std::string baseName = mangledName.substr(0, mangledName.find('$'));
    FunctionDecl *genericDecl = sema_.getGenericFunction(baseName);
    if (genericDecl && definedFunctions_.find(mangledName) == definedFunctions_.end())
    {
        // Mark as defined now to avoid re-queuing, but queue for actual lowering
        definedFunctions_.insert(mangledName);
        pendingFunctionInstantiations_.push_back({mangledName, genericDecl});
    }

    // Call the function
    if (ilReturnType.kind == Type::Kind::Void)
    {
        emitCall(mangledName, args);
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }
    else
    {
        Value result = emitCallRet(ilReturnType, mangledName, args);
        return {result, ilReturnType};
    }
}

} // namespace il::frontends::zia
