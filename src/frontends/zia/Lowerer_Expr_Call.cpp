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
/// @details This file handles the main call expression dispatcher, generic
/// function call lowering, and built-in function call lowering. Method calls,
/// collection method calls, and type construction are in Lowerer_Expr_Method.cpp.
///
/// @see Lowerer_Expr_Method.cpp - Method call and type construction lowering
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <iostream>

namespace il::frontends::zia
{

using namespace runtime;

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
                    Value result =
                        emitCallRet(Type(Type::Kind::I64), kStringLength, {baseResult.value});
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
            if (baseType &&
                (baseType->name.find("Viper.") == 0 || baseType->kind == TypeKindSem::Set ||
                 baseType->kind == TypeKindSem::List || baseType->kind == TypeKindSem::Map))
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
