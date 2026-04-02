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

namespace il::frontends::zia {

/// Closure struct layout: [funcPtr (8 bytes)] [envPtr (8 bytes)]
static constexpr int kClosureEnvOffset = 8;

using namespace runtime;

namespace {

bool isHttpServerRouteRuntime(std::string_view callee) {
    return callee == "Viper.Network.HttpServer.Get" || callee == "Viper.Network.HttpServer.Post" ||
           callee == "Viper.Network.HttpServer.Put" ||
           callee == "Viper.Network.HttpServer.Delete";
}

bool isHttpHandlerPtrType(TypeRef type) {
    return type && type->kind == TypeKindSem::Ptr;
}

FunctionDecl *resolveHttpHandlerDecl(Sema &sema, const std::string &tag) {
    if (FunctionDecl *decl = sema.getFunctionDecl(tag)) {
        TypeRef fnType = sema.getFunctionType(decl);
        if (fnType && fnType->kind == TypeKindSem::Function && fnType->returnType() &&
            fnType->returnType()->kind == TypeKindSem::Void && fnType->paramTypes().size() == 2 &&
            isHttpHandlerPtrType(fnType->paramTypes()[0]) &&
            isHttpHandlerPtrType(fnType->paramTypes()[1])) {
            return decl;
        }
    }

    FunctionDecl *match = nullptr;
    for (FunctionDecl *decl : sema.getFunctionOverloads(tag)) {
        TypeRef fnType = sema.getFunctionType(decl);
        if (!fnType || fnType->kind != TypeKindSem::Function || !fnType->returnType() ||
            fnType->returnType()->kind != TypeKindSem::Void || fnType->paramTypes().size() != 2 ||
            !isHttpHandlerPtrType(fnType->paramTypes()[0]) ||
            !isHttpHandlerPtrType(fnType->paramTypes()[1])) {
            continue;
        }
        if (match)
            return nullptr;
        match = decl;
    }

    return match;
}

std::string httpHandlerTargetName(Sema &sema, const std::string &tag) {
    FunctionDecl *decl = resolveHttpHandlerDecl(sema, tag);
    if (!decl)
        return {};

    std::string lowered = sema.loweredFunctionName(decl);
    if (!lowered.empty())
        return lowered;
    return tag == "start" ? "main" : tag;
}

} // namespace

//=============================================================================
// Built-in Function Call Helper
//=============================================================================

std::optional<LowerResult> Lowerer::lowerBuiltinCall(const std::string &name, CallExpr *expr) {
    if (name == "print" || name == "println") {
        if (!expr->args.empty()) {
            auto arg = lowerExpr(expr->args[0].value.get());
            TypeRef argType = sema_.typeOf(expr->args[0].value.get());

            Value strVal = arg.value;
            if (argType && argType->kind != TypeKindSem::String) {
                if (argType->kind == TypeKindSem::Integer) {
                    strVal = emitCallRet(Type(Type::Kind::Str), kStringFromInt, {arg.value});
                } else if (argType->kind == TypeKindSem::Number) {
                    strVal = emitCallRet(Type(Type::Kind::Str), kStringFromNum, {arg.value});
                }
            }

            emitCall(kTerminalSay, {strVal});
        }
        return LowerResult{Value::constInt(0), Type(Type::Kind::Void)};
    }

    if (name == "toString") {
        if (expr->args.empty())
            return LowerResult{Value::constInt(0), Type(Type::Kind::Str)};

        auto *argExpr = expr->args[0].value.get();
        auto arg = lowerExpr(argExpr);
        TypeRef argType = sema_.typeOf(argExpr);

        if (argType) {
            switch (argType->kind) {
                case TypeKindSem::String:
                    return LowerResult{arg.value, Type(Type::Kind::Str)};
                case TypeKindSem::Integer: {
                    Value strVal = emitCallRet(Type(Type::Kind::Str), kStringFromInt, {arg.value});
                    return LowerResult{strVal, Type(Type::Kind::Str)};
                }
                case TypeKindSem::Number: {
                    Value strVal = emitCallRet(Type(Type::Kind::Str), kStringFromNum, {arg.value});
                    return LowerResult{strVal, Type(Type::Kind::Str)};
                }
                case TypeKindSem::Boolean: {
                    Value strVal = emitCallRet(Type(Type::Kind::Str), kFmtBool, {arg.value});
                    return LowerResult{strVal, Type(Type::Kind::Str)};
                }
                default:
                    break;
            }
        }

        if (arg.type.kind == Type::Kind::Ptr) {
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

LowerResult Lowerer::lowerCall(CallExpr *expr) {
    // Check for generic function call: identity[Integer](42)
    std::string genericCallee = sema_.genericFunctionCallee(expr);
    if (!genericCallee.empty()) {
        return lowerGenericFunctionCall(genericCallee, expr);
    }

    if (MethodDecl *resolvedMethod = sema_.resolvedMethodDecl(expr)) {
        std::string ownerType = sema_.resolvedMethodOwnerType(expr);
        std::string slotKey = sema_.resolvedMethodSlotKey(expr);

        if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->callee.get())) {
            if (fieldExpr->base->kind == ExprKind::SuperExpr) {
                Value selfPtr;
                if (getSelfPtr(selfPtr))
                    return lowerMethodCall(resolvedMethod, ownerType, selfPtr, expr);
            }

            auto baseResult = lowerExpr(fieldExpr->base.get());
            TypeRef baseType = sema_.typeOf(fieldExpr->base.get());
            if (baseType && baseType->kind == TypeKindSem::Optional && baseType->innerType())
                baseType = baseType->innerType();

            if (baseType && baseType->kind == TypeKindSem::Interface) {
                auto ifaceIt = interfaceTypes_.find(baseType->name);
                if (ifaceIt != interfaceTypes_.end())
                    return lowerInterfaceMethodCall(ifaceIt->second,
                                                    slotKey,
                                                    ownerType.empty() ? baseType->name : ownerType,
                                                    resolvedMethod,
                                                    baseResult.value,
                                                    expr);
            }

            if (baseType && baseType->kind == TypeKindSem::Class && !resolvedMethod->isStatic) {
                const ClassTypeInfo *entityInfoPtr = getOrCreateClassTypeInfo(baseType->name);
                if (entityInfoPtr) {
                    return lowerVirtualMethodCall(*entityInfoPtr,
                                                  slotKey,
                                                  ownerType.empty() ? baseType->name : ownerType,
                                                  resolvedMethod,
                                                  baseResult.value,
                                                  expr);
                }
            }

            return lowerMethodCall(resolvedMethod,
                                   ownerType.empty() ? (baseType ? baseType->name : "") : ownerType,
                                   baseResult.value,
                                   expr);
        }

        Value selfPtr;
        if (getSelfPtr(selfPtr)) {
            if (currentClassType_ && !resolvedMethod->isStatic)
                return lowerVirtualMethodCall(*currentClassType_,
                                              slotKey,
                                              ownerType.empty() ? currentClassType_->name
                                                                : ownerType,
                                              resolvedMethod,
                                              selfPtr,
                                              expr);

            std::string implicitOwner = ownerType;
            if (implicitOwner.empty()) {
                if (currentClassType_)
                    implicitOwner = currentClassType_->name;
                else if (currentStructType_)
                    implicitOwner = currentStructType_->name;
            }
            return lowerMethodCall(resolvedMethod, implicitOwner, selfPtr, expr);
        }
    }

    std::string resolvedFunction = sema_.resolvedFunctionCallee(expr);
    if (!resolvedFunction.empty()) {
        TypeRef calleeType = sema_.typeOf(expr->callee.get());
        TypeRef returnType = calleeType ? calleeType->returnType() : nullptr;
        Type ilReturnType = returnType ? mapType(returnType) : Type(Type::Kind::Void);

        std::vector<TypeRef> paramTypes;
        if (calleeType)
            paramTypes = calleeType->paramTypes();

        std::vector<Value> args;
        args.reserve(expr->args.size());
        for (size_t i = 0; i < expr->args.size(); ++i) {
            auto result = lowerExpr(expr->args[i].value.get());
            Value argValue = result.value;
            if (i < paramTypes.size()) {
                TypeRef paramType = paramTypes[i];
                TypeRef argType = sema_.typeOf(expr->args[i].value.get());
                if (paramType && paramType->kind == TypeKindSem::Optional) {
                    TypeRef innerType = paramType->innerType();
                    if (argType && argType->kind == TypeKindSem::Unit)
                        argValue = Value::null();
                    else if (argType && argType->kind != TypeKindSem::Optional && innerType)
                        argValue = emitOptionalWrap(result.value, innerType);
                } else if (paramType && paramType->kind == TypeKindSem::Number && argType &&
                           argType->kind == TypeKindSem::Integer) {
                    unsigned convId = nextTempId();
                    il::core::Instr convInstr;
                    convInstr.result = convId;
                    convInstr.op = Opcode::Sitofp;
                    convInstr.type = Type(Type::Kind::F64);
                    convInstr.operands = {argValue};
                    convInstr.loc = curLoc_;
                    blockMgr_.currentBlock()->instructions.push_back(convInstr);
                    argValue = Value::temp(convId);
                }
            }
            args.push_back(argValue);
        }

        // Pack variadic arguments into a List if the callee has a variadic param.
        // This must happen before padDefaultArgs which fills missing fixed params.
        {
            // Try multiple lookup strategies (name may differ between sema and lowerer)
            FunctionDecl *varFuncDecl = sema_.resolvedFunctionDecl(expr);
            if (!varFuncDecl)
                varFuncDecl = sema_.getFunctionDecl(resolvedFunction);
            if (!varFuncDecl && expr->callee->kind == ExprKind::Ident) {
                auto *ident = static_cast<IdentExpr *>(expr->callee.get());
                varFuncDecl = sema_.getFunctionDecl(ident->name);
            }
            // Also check lowered name variants
            if (!varFuncDecl) {
                // Try without module prefix
                auto dotPos = resolvedFunction.rfind('.');
                if (dotPos != std::string::npos)
                    varFuncDecl = sema_.getFunctionDecl(
                        resolvedFunction.substr(dotPos + 1));
            }
            if (varFuncDecl && !varFuncDecl->params.empty() &&
                varFuncDecl->params.back().isVariadic) {
                size_t fixedCount = varFuncDecl->params.size() - 1;

                // Create a new List and pack the excess arguments
                Value list = emitCallRet(Type(Type::Kind::Ptr), kListNew, {});
                for (size_t vi = fixedCount; vi < args.size(); ++vi) {
                    TypeRef argType = (vi < expr->args.size())
                                         ? sema_.typeOf(expr->args[vi].value.get())
                                         : nullptr;
                    Type ilArgType = Type(Type::Kind::I64); // default
                    if (vi < paramTypes.size())
                        ilArgType = mapType(paramTypes[vi]);
                    else if (argType)
                        ilArgType = mapType(argType);
                    Value boxed = emitBoxValue(args[vi], ilArgType, argType);
                    emitCall(kListAdd, {list, boxed});
                }

                // Replace the variadic args with the single List
                args.erase(args.begin() + static_cast<ptrdiff_t>(fixedCount), args.end());
                args.push_back(list);
            }
        }

        if (resolvedFunction == kHeapRelease && args.size() == 1) {
            TypeRef argType = sema_.typeOf(expr->args[0].value.get());
            bool isString = isStringType(argType);
            Value releaseCount = emitManagedReleaseRet(args[0], isString);
            return {releaseCount, Type(Type::Kind::I64)};
        }

        padDefaultArgs(resolvedFunction, args, expr);
        if (ilReturnType.kind == Type::Kind::Void) {
            emitCall(resolvedFunction, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        Value result = emitCallRet(ilReturnType, resolvedFunction, args);
        return {result, ilReturnType};
    }

    // Handle generic function calls that weren't detected during semantic analysis
    // This happens for calls inside generic function bodies like: identity[T](x)
    // where T is a type parameter that needs to be substituted
    if (expr->callee->kind == ExprKind::Index) {
        auto *indexExpr = static_cast<IndexExpr *>(expr->callee.get());
        if (indexExpr->base->kind == ExprKind::Ident) {
            auto *identExpr = static_cast<IdentExpr *>(indexExpr->base.get());
            // Check if this is a call to a generic function
            if (sema_.isGenericFunction(identExpr->name)) {
                // Get the type argument from the index expression
                if (indexExpr->index->kind == ExprKind::Ident) {
                    auto *typeArgExpr = static_cast<IdentExpr *>(indexExpr->index.get());
                    std::string typeArgName = typeArgExpr->name;

                    // If the type arg is a type parameter, substitute it
                    TypeRef substType = sema_.lookupTypeParam(typeArgName);
                    if (substType) {
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

    // Check for method call on value or class type: obj.method()
    if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->callee.get())) {
        // Check for super.method() call - dispatch to parent class method
        if (fieldExpr->base->kind == ExprKind::SuperExpr) {
            Value selfPtr;
            if (getSelfPtr(selfPtr) && currentClassType_ &&
                !currentClassType_->baseClass.empty()) {
                auto parentIt = classTypes_.find(currentClassType_->baseClass);
                if (parentIt != classTypes_.end()) {
                    if (auto *method = parentIt->second.findMethod(fieldExpr->field)) {
                        return lowerMethodCall(
                            method, currentClassType_->baseClass, selfPtr, expr);
                    }
                }
            }
        }

        // Get the type of the base expression
        TypeRef baseType = sema_.typeOf(fieldExpr->base.get());
        if (baseType) {
            // Unwrap Optional types for method resolution
            // This handles the case where a variable was assigned from an optional
            // after a null check (e.g., `var table = maybeTable;` after `if maybeTable == null {
            // return; }`)
            if (baseType->kind == TypeKindSem::Optional && baseType->innerType()) {
                baseType = baseType->innerType();
            }

            std::string typeName = baseType->name;

            // Check struct type methods
            const StructTypeInfo *valueInfo = getOrCreateStructTypeInfo(typeName);
            if (valueInfo) {
                if (auto *method = valueInfo->findMethod(fieldExpr->field)) {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    return lowerMethodCall(method, typeName, baseResult.value, expr);
                }
            }

            // Check class type methods with virtual dispatch
            const ClassTypeInfo *entityInfoPtr = getOrCreateClassTypeInfo(typeName);
            if (entityInfoPtr) {
                const ClassTypeInfo &entityInfo = *entityInfoPtr;
                MethodDecl *namedMethod = entityInfo.findMethod(fieldExpr->field);

                if (namedMethod) {
                    std::string slotKey = sema_.methodSlotKey(typeName, namedMethod);
                    size_t vtableSlot = entityInfo.findVtableSlot(slotKey);
                    if (vtableSlot != SIZE_MAX) {
                        auto baseResult = lowerExpr(fieldExpr->base.get());
                        return lowerVirtualMethodCall(
                            entityInfo, slotKey, typeName, namedMethod, baseResult.value, expr);
                    }
                }

                if (namedMethod) {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    return lowerMethodCall(namedMethod, typeName, baseResult.value, expr);
                }

                // Check parent class for inherited methods
                std::string parentName = entityInfo.baseClass;
                while (!parentName.empty()) {
                    auto parentIt = classTypes_.find(parentName);
                    if (parentIt == classTypes_.end())
                        break;
                    if (auto *method = parentIt->second.findMethod(fieldExpr->field)) {
                        auto baseResult = lowerExpr(fieldExpr->base.get());
                        return lowerMethodCall(method, parentName, baseResult.value, expr);
                    }
                    parentName = parentIt->second.baseClass;
                }

                // Class type found but method not in it or any parent — emit error
                diag_.report(
                    {il::support::Severity::Error,
                     "Class type '" + typeName + "' has no method '" + fieldExpr->field + "'",
                     expr->loc,
                     "V3100"});
                return {Value::constInt(0), Type(Type::Kind::Void)};
            }

            // Handle interface method calls
            if (baseType->kind == TypeKindSem::Interface) {
                auto ifaceIt = interfaceTypes_.find(typeName);
                if (ifaceIt != interfaceTypes_.end()) {
                    auto methodIt = ifaceIt->second.methodMap.find(fieldExpr->field);
                    if (methodIt != ifaceIt->second.methodMap.end()) {
                        auto baseResult = lowerExpr(fieldExpr->base.get());
                        std::string slotKey = sema_.methodSlotKey(typeName, methodIt->second);
                        return lowerInterfaceMethodCall(ifaceIt->second,
                                                        slotKey,
                                                        typeName,
                                                        methodIt->second,
                                                        baseResult.value,
                                                        expr);
                    }
                }
            }

            // Handle module-qualified function calls
            if (baseType->kind == TypeKindSem::Module) {
                // Check if sema resolved a runtime callee name for this call
                // (e.g., "ResultOk" for Viper.Result.Ok)
                std::string funcName = sema_.runtimeCallee(expr);
                if (funcName.empty()) {
                    // Try qualified name for runtime functions (e.g., Viper.Result.Ok)
                    std::string qualName = baseType->name + "." + fieldExpr->field;
                    if (il::runtime::findRuntimeDescriptor(qualName))
                        funcName = qualName;
                    else
                        funcName = fieldExpr->field; // user-defined module function
                }

                std::vector<Value> args;

                // Look up runtime signature for auto-boxing/coercion
                const auto *rtDesc = il::runtime::findRuntimeDescriptor(funcName);
                const std::vector<il::core::Type> *expectedParamTypes = nullptr;
                if (rtDesc)
                    expectedParamTypes = &rtDesc->signature.paramTypes;

                for (size_t i = 0; i < expr->args.size(); ++i) {
                    auto result = lowerExpr(expr->args[i].value.get());
                    Value argValue = result.value;
                    if (result.type.kind == Type::Kind::I32)
                        argValue = widenByteToInteger(argValue);

                    // Auto-box primitives or coerce i64→f64 when expected by runtime
                    if (expectedParamTypes && i < expectedParamTypes->size()) {
                        Type expectedType = (*expectedParamTypes)[i];
                        if (expectedType.kind == Type::Kind::Ptr &&
                            result.type.kind != Type::Kind::Ptr &&
                            result.type.kind != Type::Kind::Void) {
                            argValue = emitBox(argValue, result.type);
                        } else if (expectedType.kind == Type::Kind::F64 &&
                                   result.type.kind == Type::Kind::I64) {
                            unsigned convId = nextTempId();
                            il::core::Instr convInstr;
                            convInstr.result = convId;
                            convInstr.op = Opcode::Sitofp;
                            convInstr.type = Type(Type::Kind::F64);
                            convInstr.operands = {argValue};
                            convInstr.loc = curLoc_;
                            blockMgr_.currentBlock()->instructions.push_back(convInstr);
                            argValue = Value::temp(convId);
                        }
                    }
                    args.push_back(argValue);
                }

                TypeRef exprType = sema_.typeOf(expr);
                Type ilReturnType = exprType ? mapType(exprType) : Type(Type::Kind::Void);

                if (funcName == kHeapRelease && args.size() == 1) {
                    TypeRef argType = sema_.typeOf(expr->args[0].value.get());
                    bool isString = isStringType(argType);
                    Value releaseCount = emitManagedReleaseRet(args[0], isString);
                    return {releaseCount, Type(Type::Kind::I64)};
                }

                // Use the extern's declared return type for the call instruction
                // to match the function signature. The sema type may differ (e.g.,
                // String? maps to Ptr, but the extern returns str). We'll use the
                // extern type for the call and the sema type for the result.
                Type callReturnType = ilReturnType;
                if (rtDesc)
                    callReturnType = rtDesc->signature.retType;

                if (ilReturnType.kind == Type::Kind::Void) {
                    emitCall(funcName, args);
                    return {Value::constInt(0), Type(Type::Kind::Void)};
                } else {
                    Value result = emitCallRet(callReturnType, funcName, args);
                    return {result, ilReturnType};
                }
            }

            // Handle String method calls - Bug #018 fix
            // String.length() should be treated as a property access, not a method call
            if (baseType->kind == TypeKindSem::String) {
                if (equalsIgnoreCase(fieldExpr->field, "length")) {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    Value result =
                        emitCallRet(Type(Type::Kind::I64), kStringLength, {baseResult.value});
                    return {result, Type(Type::Kind::I64)};
                }
            }

            // Handle Integer method calls - Bug #018 fix
            // Integer.toString() should convert to string
            if (baseType->kind == TypeKindSem::Integer) {
                if (equalsIgnoreCase(fieldExpr->field, "toString")) {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    Value result =
                        emitCallRet(Type(Type::Kind::Str), kStringFromInt, {baseResult.value});
                    return {result, Type(Type::Kind::Str)};
                }
            }

            // Handle Number method calls - Bug #018 fix
            // Number.toString() should convert to string
            if (baseType->kind == TypeKindSem::Number) {
                if (equalsIgnoreCase(fieldExpr->field, "toString")) {
                    auto baseResult = lowerExpr(fieldExpr->base.get());
                    Value result =
                        emitCallRet(Type(Type::Kind::Str), kStringFromNum, {baseResult.value});
                    return {result, Type(Type::Kind::Str)};
                }
            }

            // Handle List method calls
            if (baseType->kind == TypeKindSem::List) {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                auto listResult =
                    lowerListMethodCall(baseResult.value, baseType, fieldExpr->field, expr);
                if (listResult)
                    return *listResult;
            }

            // Handle Map method calls
            if (baseType->kind == TypeKindSem::Map) {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                auto mapResult =
                    lowerMapMethodCall(baseResult.value, baseType, fieldExpr->field, expr);
                if (mapResult)
                    return *mapResult;
            }

            // Handle Set method calls
            if (baseType->kind == TypeKindSem::Set) {
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
    if (!runtimeCallee.empty()) {
        // Auto-dispatch Say/Print to typed variants based on argument type.
        // This allows Say(42), Say(3.14), Say(true) to work without requiring
        // explicit string conversion. The typed runtime functions (SayInt, SayNum,
        // SayBool, PrintInt, PrintNum, PrintBool) already exist — we just redirect.
        if (expr->args.size() == 1) {
            auto *argExpr = expr->args[0].value.get();
            TypeRef argType = sema_.typeOf(argExpr);

            if (argType && argType->kind != TypeKindSem::String) {
                std::string typedCallee;

                if (runtimeCallee == kTerminalSay) {
                    if (argType->kind == TypeKindSem::Integer)
                        typedCallee = kTerminalSayInt;
                    else if (argType->kind == TypeKindSem::Number)
                        typedCallee = kTerminalSayNum;
                    else if (argType->kind == TypeKindSem::Boolean)
                        typedCallee = kTerminalSayBool;
                } else if (runtimeCallee == kTerminalPrint) {
                    if (argType->kind == TypeKindSem::Integer)
                        typedCallee = kTerminalPrintInt;
                    else if (argType->kind == TypeKindSem::Number)
                        typedCallee = kTerminalPrintNum;
                    else if (argType->kind == TypeKindSem::Boolean)
                        typedCallee = kTerminalPrintBool;
                }

                if (!typedCallee.empty()) {
                    auto arg = lowerExpr(argExpr);
                    Value argVal = arg.value;
                    if (arg.type.kind == Type::Kind::I32)
                        argVal = widenByteToInteger(argVal);
                    emitCall(typedCallee, {argVal});
                    return {Value::constInt(0), Type(Type::Kind::Void)};
                }
            }
        }

        std::vector<Value> args;

        if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->callee.get())) {
            TypeRef baseType = sema_.typeOf(fieldExpr->base.get());
            if (baseType &&
                (baseType->name.find("Viper.") == 0 || baseType->kind == TypeKindSem::Set ||
                 baseType->kind == TypeKindSem::List || baseType->kind == TypeKindSem::Map ||
                 baseType->kind == TypeKindSem::String)) {
                auto baseResult = lowerExpr(fieldExpr->base.get());
                args.push_back(baseResult.value);
            }
        }

        // BUG-008 fix: Look up runtime signature to auto-box primitives when expected type is ptr
        const auto *rtDesc = il::runtime::findRuntimeDescriptor(runtimeCallee);
        const std::vector<il::core::Type> *expectedParamTypes = nullptr;
        if (rtDesc) {
            expectedParamTypes = &rtDesc->signature.paramTypes;
        }

        args.reserve(args.size() + expr->args.size());
        size_t paramOffset = args.size(); // Account for implicit self parameter if present
        for (size_t i = 0; i < expr->args.size(); ++i) {
            auto result = lowerExpr(expr->args[i].value.get());
            Value argValue = result.value;
            if (result.type.kind == Type::Kind::I32) {
                argValue = widenByteToInteger(argValue);
            }

            // BUG-008 fix: Auto-box primitive if expected type is Ptr
            // BUG-013 fix: Implicit i64→f64 coercion when expected type is F64
            if (expectedParamTypes && (paramOffset + i) < expectedParamTypes->size()) {
                Type expectedType = (*expectedParamTypes)[paramOffset + i];
                if (expectedType.kind == Type::Kind::Ptr && result.type.kind != Type::Kind::Ptr &&
                    result.type.kind != Type::Kind::Void) {
                    // Primitive passed where object expected - auto-box
                    argValue = emitBox(argValue, result.type);
                } else if (expectedType.kind == Type::Kind::F64 &&
                           result.type.kind == Type::Kind::I64) {
                    // Integer passed where f64 expected - emit sitofp
                    unsigned convId = nextTempId();
                    il::core::Instr convInstr;
                    convInstr.result = convId;
                    convInstr.op = Opcode::Sitofp;
                    convInstr.type = Type(Type::Kind::F64);
                    convInstr.operands = {argValue};
                    convInstr.loc = curLoc_;
                    blockMgr_.currentBlock()->instructions.push_back(convInstr);
                    argValue = Value::temp(convId);
                }
            }

            args.push_back(argValue);
        }

        if (isHttpServerRouteRuntime(runtimeCallee) && expr->args.size() == 2 && args.size() >= 3) {
            if (auto *tagExpr = dynamic_cast<StringLiteralExpr *>(expr->args[1].value.get())) {
                std::string handlerTarget = httpHandlerTargetName(sema_, tagExpr->value);
                if (!handlerTarget.empty()) {
                    emitCall("Viper.Network.HttpServer.BindHandler",
                             {args[0], args[paramOffset + 1], Value::global(handlerTarget)});
                }
            }
        }

        TypeRef exprType = sema_.functionReturnType(runtimeCallee);
        Type ilReturnType = exprType ? mapType(exprType) : Type(Type::Kind::Void);

        if (runtimeCallee == kHeapRelease && args.size() == 1) {
            TypeRef argType = sema_.typeOf(expr->args[0].value.get());
            bool isString = isStringType(argType);
            Value releaseCount = emitManagedReleaseRet(args[0], isString);
            return {releaseCount, Type(Type::Kind::I64)};
        }

        // Use the extern's declared return type for the call instruction so it
        // matches the function signature. The sema type may differ for optional
        // returns (e.g., String? maps to Ptr, but the extern returns str).
        Type callReturnType = ilReturnType;
        if (rtDesc)
            callReturnType = rtDesc->signature.retType;

        // Handle void return types correctly - don't try to store void results
        if (ilReturnType.kind == Type::Kind::Void) {
            emitCall(runtimeCallee, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        } else {
            Value result = emitCallRet(callReturnType, runtimeCallee, args);
            return {result, ilReturnType};
        }
    }

    // Check for built-in functions and struct type construction
    if (auto *ident = dynamic_cast<IdentExpr *>(expr->callee.get())) {
        // Check built-in functions
        auto builtinResult = lowerBuiltinCall(ident->name, expr);
        if (builtinResult)
            return *builtinResult;

        // Check struct type construction
        auto valueTypeResult = lowerStructTypeConstruction(ident->name, expr);
        if (valueTypeResult)
            return *valueTypeResult;

        // Check class type construction (Entity(args) without 'new' keyword)
        auto entityTypeResult = lowerClassTypeConstruction(ident->name, expr);
        if (entityTypeResult)
            return *entityTypeResult;
    }

    // Handle direct or indirect function calls
    std::string calleeName;
    bool isIndirectCall = false;
    Value funcPtr;

    TypeRef calleeType = sema_.typeOf(expr->callee.get());
    bool isLambdaClosure = calleeType && calleeType->isCallable();

    if (auto *ident = dynamic_cast<IdentExpr *>(expr->callee.get())) {
        // Check for implicit method call
        if (currentClassType_) {
            if (auto *method = currentClassType_->findMethod(ident->name)) {
                Value selfPtr;
                if (getSelfPtr(selfPtr)) {
                    return lowerMethodCall(method, currentClassType_->name, selfPtr, expr);
                }
            }
        }

        // Check if this is a variable holding a function pointer
        if (definedFunctions_.find(mangleFunctionName(ident->name)) == definedFunctions_.end()) {
            auto slotIt = slots_.find(ident->name);
            if (slotIt != slots_.end()) {
                unsigned loadId = nextTempId();
                il::core::Instr loadInstr;
                loadInstr.result = loadId;
                loadInstr.op = Opcode::Load;
                loadInstr.type = Type(Type::Kind::Ptr);
                loadInstr.operands = {slotIt->second};
                loadInstr.loc = curLoc_;
                blockMgr_.currentBlock()->instructions.push_back(loadInstr);
                funcPtr = Value::temp(loadId);
                isIndirectCall = true;
            } else {
                auto localIt = locals_.find(ident->name);
                if (localIt != locals_.end()) {
                    funcPtr = localIt->second;
                    isIndirectCall = true;
                }
            }
        }

        if (!isIndirectCall) {
            calleeName = mangleFunctionName(ident->name);
        }
    } else if (auto *fieldExpr = dynamic_cast<FieldExpr *>(expr->callee.get())) {
        // Check if this is a namespace-qualified function call (e.g., Math.add or
        // Outer.Inner.getValue) Recursively build the qualified name from nested FieldExpr nodes
        std::string qualifiedName;
        std::function<bool(Expr *)> buildQualifiedName = [&](Expr *e) -> bool {
            if (auto *ident = dynamic_cast<IdentExpr *>(e)) {
                qualifiedName = ident->name;
                return true;
            }
            if (auto *field = dynamic_cast<FieldExpr *>(e)) {
                if (buildQualifiedName(field->base.get())) {
                    qualifiedName += "." + field->field;
                    return true;
                }
            }
            return false;
        };
        buildQualifiedName(expr->callee.get());

        // Check if the qualified name is a defined function.
        // Try both mangled and unmangled names (mirrors line 596 which
        // correctly uses mangleFunctionName for ident-based calls).
        if (!qualifiedName.empty()) {
            std::string mangledQN = mangleFunctionName(qualifiedName);
            if (definedFunctions_.find(mangledQN) != definedFunctions_.end()) {
                calleeName = mangledQN;
                isIndirectCall = false;
            } else if (definedFunctions_.find(qualifiedName) != definedFunctions_.end()) {
                calleeName = qualifiedName;
                isIndirectCall = false;
            } else {
                // Regular field access on a value - lower and use as indirect call
                auto calleeResult = lowerExpr(expr->callee.get());
                funcPtr = calleeResult.value;
                isIndirectCall = true;
            }
        } else {
            // Regular field access on a value - lower and use as indirect call
            auto calleeResult = lowerExpr(expr->callee.get());
            funcPtr = calleeResult.value;
            isIndirectCall = true;
        }
    } else {
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
    for (size_t i = 0; i < expr->args.size(); ++i) {
        auto &arg = expr->args[i];
        auto result = lowerExpr(arg.value.get());
        Value argValue = result.value;

        if (i < paramTypes.size()) {
            TypeRef paramType = paramTypes[i];
            TypeRef argType = sema_.typeOf(arg.value.get());
            auto coerced = coerceValueToType(argValue, result.type, argType, paramType);
            argValue = coerced.value;
        }

        args.push_back(argValue);
    }

    if (isIndirectCall) {
        if (isLambdaClosure) {
            Value closurePtr = funcPtr;
            Value actualFuncPtr = emitLoad(closurePtr, Type(Type::Kind::Ptr));
            Value envFieldAddr = emitGEP(closurePtr, kClosureEnvOffset);
            Value envPtr = emitLoad(envFieldAddr, Type(Type::Kind::Ptr));

            std::vector<Value> closureArgs;
            closureArgs.reserve(args.size() + 1);
            closureArgs.push_back(envPtr);
            for (const auto &arg : args) {
                closureArgs.push_back(arg);
            }

            if (ilReturnType.kind == Type::Kind::Void) {
                emitCallIndirect(actualFuncPtr, closureArgs);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            } else {
                Value result = emitCallIndirectRet(ilReturnType, actualFuncPtr, closureArgs);
                return {result, ilReturnType};
            }
        } else {
            if (ilReturnType.kind == Type::Kind::Void) {
                emitCallIndirect(funcPtr, args);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            } else {
                Value result = emitCallIndirectRet(ilReturnType, funcPtr, args);
                return {result, ilReturnType};
            }
        }
    } else {
        // Pack variadic arguments if the callee has a variadic last param.
        {
            FunctionDecl *vDecl = sema_.resolvedFunctionDecl(expr);
            if (!vDecl)
                vDecl = sema_.getFunctionDecl(calleeName);
            if (!vDecl && expr->callee->kind == ExprKind::Ident) {
                auto *ident = static_cast<IdentExpr *>(expr->callee.get());
                vDecl = sema_.getFunctionDecl(ident->name);
            }
            if (vDecl && !vDecl->params.empty() && vDecl->params.back().isVariadic) {
                size_t fixedCount = vDecl->params.size() - 1;
                Value list = emitCallRet(Type(Type::Kind::Ptr), kListNew, {});
                for (size_t vi = fixedCount; vi < args.size(); ++vi) {
                    TypeRef argType = (vi < expr->args.size())
                                         ? sema_.typeOf(expr->args[vi].value.get())
                                         : nullptr;
                    Type ilArgType = argType ? mapType(argType) : Type(Type::Kind::I64);
                    Value boxed = emitBoxValue(args[vi], ilArgType, argType);
                    emitCall(kListAdd, {list, boxed});
                }
                args.erase(args.begin() + static_cast<ptrdiff_t>(fixedCount), args.end());
                args.push_back(list);
            }
        }

        // Pad missing trailing arguments with default values from function declaration
        padDefaultArgs(calleeName, args, expr);

        if (ilReturnType.kind == Type::Kind::Void) {
            emitCall(calleeName, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        } else {
            Value result = emitCallRet(ilReturnType, calleeName, args);
            return {result, ilReturnType};
        }
    }
}

//=============================================================================
// Default Parameter Padding
//=============================================================================

void Lowerer::padDefaultArgs(const std::string &calleeName,
                             std::vector<Value> &args,
                             CallExpr *callExpr) {
    FunctionDecl *funcDecl = sema_.getFunctionDecl(calleeName);
    // Fall back to original ident name (before mangling)
    if (!funcDecl && callExpr->callee->kind == ExprKind::Ident) {
        auto *ident = static_cast<IdentExpr *>(callExpr->callee.get());
        funcDecl = sema_.getFunctionDecl(ident->name);
    }
    if (!funcDecl)
        return;

    size_t numParams = funcDecl->params.size();
    size_t numArgs = args.size();
    if (numArgs >= numParams)
        return;

    // Pad missing trailing arguments with their default values
    for (size_t i = numArgs; i < numParams; ++i) {
        const auto &param = funcDecl->params[i];
        if (!param.defaultValue)
            break; // No default — shouldn't happen if sema validated

        auto result = lowerExpr(param.defaultValue.get());
        args.push_back(result.value);
    }
}

//=============================================================================
// Generic Function Call Lowering
//=============================================================================

LowerResult Lowerer::lowerGenericFunctionCall(const std::string &mangledName, CallExpr *expr) {
    // Get the function type from Sema
    TypeRef funcType = sema_.typeOf(expr->callee.get());
    if (!funcType || funcType->kind != TypeKindSem::Function) {
        // Fallback - compute return type from generic function declaration
        std::string baseName = mangledName.substr(0, mangledName.find('$'));
        std::string concreteTypeName = mangledName.substr(mangledName.find('$') + 1);
        FunctionDecl *genericDecl = sema_.getGenericFunction(baseName);

        Type ilReturnType = Type(Type::Kind::I64); // Default fallback
        if (genericDecl) {
            // Resolve return type from declaration and substitute type parameters
            if (genericDecl->returnType) {
                TypeRef declReturnType = sema_.resolveType(genericDecl->returnType.get());
                if (declReturnType && declReturnType->kind == TypeKindSem::TypeParam) {
                    // Return type is a type parameter - substitute with concrete type
                    TypeRef concreteType = sema_.resolveNamedType(concreteTypeName);
                    if (concreteType) {
                        ilReturnType = mapType(concreteType);
                    }
                } else if (declReturnType) {
                    ilReturnType = mapType(declReturnType);
                }
            } else {
                ilReturnType = Type(Type::Kind::Void);
            }
        }

        // Lower arguments
        std::vector<Value> args;
        for (auto &arg : expr->args) {
            auto result = lowerExpr(arg.value.get());
            args.push_back(result.value);
        }

        // Queue the instantiated generic function for later lowering
        if (genericDecl && definedFunctions_.find(mangledName) == definedFunctions_.end()) {
            // Mark as defined now to avoid re-queuing, but queue for actual lowering
            definedFunctions_.insert(mangledName);
            pendingFunctionInstantiations_.push_back({mangledName, genericDecl});
        }

        // Call the function
        if (ilReturnType.kind == Type::Kind::Void) {
            emitCall(mangledName, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        } else {
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
    for (size_t i = 0; i < expr->args.size(); ++i) {
        auto result = lowerExpr(expr->args[i].value.get());
        TypeRef argType = sema_.typeOf(expr->args[i].value.get());
        TypeRef paramType = i < paramTypes.size() ? paramTypes[i] : nullptr;
        auto coerced = coerceValueToType(result.value, result.type, argType, paramType);
        args.push_back(coerced.value);
    }

    // Queue the instantiated generic function for later lowering
    std::string baseName = mangledName.substr(0, mangledName.find('$'));
    FunctionDecl *genericDecl = sema_.getGenericFunction(baseName);
    if (genericDecl && definedFunctions_.find(mangledName) == definedFunctions_.end()) {
        // Mark as defined now to avoid re-queuing, but queue for actual lowering
        definedFunctions_.insert(mangledName);
        pendingFunctionInstantiations_.push_back({mangledName, genericDecl});
    }

    // Call the function
    if (ilReturnType.kind == Type::Kind::Void) {
        emitCall(mangledName, args);
        return {Value::constInt(0), Type(Type::Kind::Void)};
    } else {
        Value result = emitCallRet(ilReturnType, mangledName, args);
        return {result, ilReturnType};
    }
}

} // namespace il::frontends::zia
