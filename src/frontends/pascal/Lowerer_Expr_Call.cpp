//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer_Expr_Call.cpp
// Purpose: Call expression lowering for Pascal AST to IL.
// Key invariants: Handles builtins, method calls, constructor calls, variadic IO.
// Ownership/Lifetime: Part of Lowerer; operates on borrowed AST.
//
//===----------------------------------------------------------------------===//

#include "frontends/common/CharUtils.hpp"
#include "frontends/pascal/BuiltinRegistry.hpp"
#include "frontends/pascal/Lowerer.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

namespace il::frontends::pascal
{

using common::char_utils::toLowercase;

inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

LowerResult Lowerer::lowerCall(const CallExpr &expr)
{
    // Check for constructor call (marked by semantic analyzer)
    if (expr.isConstructorCall && !expr.constructorClassName.empty())
    {
        // Use the OOP constructor lowering which properly initializes vtable
        return lowerConstructorCall(expr);
    }

    // Check for method call: obj.Method(args)
    if (expr.callee->kind == ExprKind::Field)
    {
        const auto &fieldExpr = static_cast<const FieldExpr &>(*expr.callee);

        // Check if this is an interface method call
        if (expr.isInterfaceCall && !expr.interfaceName.empty())
        {
            return lowerInterfaceMethodCall(fieldExpr, expr);
        }

        return lowerMethodCall(fieldExpr, expr);
    }

    // Get callee name for regular calls
    if (expr.callee->kind != ExprKind::Name)
    {
        // Unknown callee type - return default
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    auto &nameExpr = static_cast<const NameExpr &>(*expr.callee);
    std::string callee = nameExpr.name;

    // Implicit method call on Self inside a method: MethodName(args)
    if (!currentClassName_.empty())
    {
        std::string classKey = toLower(currentClassName_);
        auto *ci = sema_->lookupClass(classKey);
        if (ci)
        {
            std::string mkey = toLower(callee);
            const MethodInfo *methodInfo = ci->findMethod(mkey);
            if (methodInfo)
            {
                // Resolve Self from locals
                auto itSelf = locals_.find("self");
                if (itSelf != locals_.end())
                {
                    // Build arg list: Self + user args
                    std::vector<Value> args;
                    Value selfPtr = emitLoad(Type(Type::Kind::Ptr), itSelf->second);
                    args.push_back(selfPtr);
                    std::vector<PasType> argTypes;
                    for (const auto &arg : expr.args)
                    {
                        LowerResult lr = lowerExpr(*arg);
                        args.push_back(lr.value);
                        argTypes.push_back(PasType::unknown());
                    }

                    // Direct call to Class.Method
                    std::string funcName = currentClassName_ + "." + callee;
                    Type retType = mapType(methodInfo->returnType);
                    if (retType.kind == Type::Kind::Void)
                    {
                        emitCall(funcName, args);
                        return {Value::constInt(0), Type(Type::Kind::Void)};
                    }
                    else
                    {
                        Value res = emitCallRet(retType, funcName, args);
                        return {res, retType};
                    }
                }
            }
        }
    }

    // Method call through 'with' context (marked by semantic analyzer)
    if (expr.isWithMethodCall && !expr.withClassName.empty())
    {
        // Find the matching with context for this class
        for (auto it = withContexts_.rbegin(); it != withContexts_.rend(); ++it)
        {
            const WithContext &ctx = *it;
            if (ctx.type.kind == PasTypeKind::Class &&
                toLower(ctx.type.name) == toLower(expr.withClassName))
            {
                // Load the object pointer from the with context's slot
                Value objPtr = emitLoad(Type(Type::Kind::Ptr), ctx.slot);

                // Build arg list: Self + user args
                std::vector<Value> args;
                args.push_back(objPtr);
                for (const auto &arg : expr.args)
                {
                    LowerResult lr = lowerExpr(*arg);
                    args.push_back(lr.value);
                }

                // Get method info
                std::string classKey = toLower(expr.withClassName);
                auto *ci = sema_->lookupClass(classKey);
                if (ci)
                {
                    std::string mkey = toLower(callee);
                    const MethodInfo *methodInfo = ci->findMethod(mkey);
                    if (methodInfo)
                    {
                        // Direct call to Class.Method
                        std::string funcName = expr.withClassName + "." + callee;
                        Type retType = mapType(methodInfo->returnType);
                        if (retType.kind == Type::Kind::Void)
                        {
                            emitCall(funcName, args);
                            return {Value::constInt(0), Type(Type::Kind::Void)};
                        }
                        else
                        {
                            Value res = emitCallRet(retType, funcName, args);
                            return {res, retType};
                        }
                    }
                }
                break;
            }
        }
    }

    // Type-cast form: TClass(expr)
    // If callee is a type name and that type is a class, lower as rt_cast_as
    {
        std::string key = toLower(callee);
        auto typeOpt = sema_->lookupType(key);
        if (typeOpt &&
            (typeOpt->kind == PasTypeKind::Class || typeOpt->kind == PasTypeKind::Interface))
        {
            // Expect exactly one argument; if missing, return null pointer
            if (expr.args.empty())
            {
                return {Value::null(), Type(Type::Kind::Ptr)};
            }

            // Lower the operand
            LowerResult obj = lowerExpr(*expr.args[0]);

            // Determine class id for the target type
            int64_t classId = 0;
            if (typeOpt->kind == PasTypeKind::Class)
            {
                std::string classKey = toLower(typeOpt->name);
                auto layoutIt = classLayouts_.find(classKey);
                if (layoutIt != classLayouts_.end())
                {
                    classId = layoutIt->second.classId;
                }
            }
            else
            {
                // For interfaces, we could support rt_cast_as_iface; for now use class path if
                // available Fallback: return original pointer
                return obj;
            }

            usedExterns_.insert("rt_cast_as");
            Value casted = emitCallRet(
                Type(Type::Kind::Ptr), "rt_cast_as", {obj.value, Value::constInt(classId)});
            return {casted, Type(Type::Kind::Ptr)};
        }
    }

    // Lower arguments and track their types
    std::vector<Value> args;
    std::vector<PasType> argTypes;
    for (const auto &arg : expr.args)
    {
        LowerResult argResult = lowerExpr(*arg);
        args.push_back(argResult.value);
        // Map IL type back to PasType for dispatch
        PasType pasType;
        switch (argResult.type.kind)
        {
            case Type::Kind::I64:
            case Type::Kind::I32:
            case Type::Kind::I1:
                pasType.kind = PasTypeKind::Integer;
                break;
            case Type::Kind::F64:
                pasType.kind = PasTypeKind::Real;
                break;
            case Type::Kind::Ptr:
            case Type::Kind::Str:
                pasType.kind = PasTypeKind::String;
                break;
            default:
                pasType.kind = PasTypeKind::Unknown;
                break;
        }
        argTypes.push_back(pasType);
    }

    // Check for builtin functions
    std::string lowerCallee = toLower(callee);
    auto builtinOpt = lookupBuiltin(lowerCallee);

    if (builtinOpt)
    {
        PascalBuiltin builtin = *builtinOpt;
        const BuiltinDescriptor &desc = getBuiltinDescriptor(builtin);

        // Determine first arg type for dispatch
        PasTypeKind firstArgType = argTypes.empty() ? PasTypeKind::Unknown : argTypes[0].kind;

        // Handle Write/WriteLn specially (variadic with type dispatch)
        if (builtin == PascalBuiltin::Write || builtin == PascalBuiltin::WriteLn)
        {
            // Print each argument using type-appropriate runtime call
            for (size_t i = 0; i < args.size(); ++i)
            {
                const char *rtSym = getBuiltinRuntimeSymbol(PascalBuiltin::Write, argTypes[i].kind);
                if (rtSym)
                {
                    emitCall(rtSym, {args[i]});
                }
                else
                {
                    // Default to i64
                    emitCall("rt_print_i64", {args[i]});
                }
            }
            if (builtin == PascalBuiltin::WriteLn)
            {
                std::string nlGlobal = getStringGlobal("\n");
                Value nlStr = emitConstStr(nlGlobal);
                emitCall("rt_print_str", {nlStr});
            }
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        // Handle ReadLn
        if (builtin == PascalBuiltin::ReadLn)
        {
            // For now, just call rt_input_line and discard result
            emitCallRet(Type(Type::Kind::Str), "rt_input_line", {});
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        // Handle inline builtins
        if (builtin == PascalBuiltin::Ord)
        {
            // Ord just returns the integer value (identity for integers)
            if (!args.empty())
                return {args[0], Type(Type::Kind::I64)};
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Pred)
        {
            // Pred(x) = x - 1
            if (!args.empty())
            {
                Value one = Value::constInt(1);
                Value result = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), args[0], one);
                return {result, Type(Type::Kind::I64)};
            }
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Succ)
        {
            // Succ(x) = x + 1
            if (!args.empty())
            {
                Value one = Value::constInt(1);
                Value result = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), args[0], one);
                return {result, Type(Type::Kind::I64)};
            }
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Sqr)
        {
            // Sqr(x) = x * x (use overflow-checking multiplication for integers)
            if (!args.empty())
            {
                Opcode mulOp = (firstArgType == PasTypeKind::Real) ? Opcode::FMul : Opcode::IMulOvf;
                Type ty = (firstArgType == PasTypeKind::Real) ? Type(Type::Kind::F64)
                                                              : Type(Type::Kind::I64);
                Value result = emitBinary(mulOp, ty, args[0], args[0]);
                return {result, ty};
            }
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }

        if (builtin == PascalBuiltin::Randomize)
        {
            // Randomize([seed]) - if no seed provided, use 0 as default
            usedExterns_.insert("rt_randomize_i64");
            Value seed = args.empty() ? Value::constInt(0) : args[0];
            emitCall("rt_randomize_i64", {seed});
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        if (builtin == PascalBuiltin::Copy)
        {
            // Copy(s, startIdx, [count]) - Pascal uses 1-based indexing, runtime uses 0-based
            // Convert: Copy(s, 1, 5) => rt_substr(s, 0, 5)
            usedExterns_.insert("rt_substr");
            Value str = args[0];
            // Subtract 1 from start index to convert from 1-based to 0-based
            Value startIdx =
                emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), args[1], Value::constInt(1));
            // If count not provided, use string length (handled by rt_substr with large count)
            Value count = args.size() > 2 ? args[2] : Value::constInt(INT64_MAX);
            Value result = emitCallRet(Type(Type::Kind::Str), "rt_substr", {str, startIdx, count});
            return {result, Type(Type::Kind::Str)};
        }

        // GotoXY(col, row) - Pascal convention is (col, row) but rt_term_locate expects (row, col)
        // Swap the arguments before calling the runtime function
        if (builtin == PascalBuiltin::GotoXY && args.size() >= 2)
        {
            usedExterns_.insert("rt_term_locate");
            // Swap: GotoXY(col, row) -> rt_term_locate(row, col)
            emitCall("rt_term_locate", {args[1], args[0]});
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }

        // Handle builtins with runtime symbols
        const char *rtSym = getBuiltinRuntimeSymbol(builtin, firstArgType);
        if (rtSym)
        {
            // Look up the actual runtime signature to get the correct return type
            const auto *rtDesc = il::runtime::findRuntimeDescriptor(rtSym);
            Type rtRetType = Type(Type::Kind::Void);
            if (rtDesc)
            {
                rtRetType = rtDesc->signature.retType;
            }
            else
            {
                // Fallback to Pascal type mapping
                PasType resultPasType = getBuiltinResultType(builtin, firstArgType);
                rtRetType = mapType(resultPasType);
            }

            // Also get the Pascal-expected return type for conversion
            PasType pascalResultType = getBuiltinResultType(builtin, firstArgType);
            Type pascalRetType = mapType(pascalResultType);

            if (rtRetType.kind == Type::Kind::Void)
            {
                emitCall(rtSym, args);
                return {Value::constInt(0), Type(Type::Kind::Void)};
            }
            else
            {
                Value result = emitCallRet(rtRetType, rtSym, args);

                // Convert integer to i1 if Pascal expects Boolean but runtime returns integer
                if (pascalRetType.kind == Type::Kind::I1 &&
                    (rtRetType.kind == Type::Kind::I32 || rtRetType.kind == Type::Kind::I64))
                {
                    // Convert to i1: compare != 0
                    Value zero = Value::constInt(0);
                    result = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, zero);
                    return {result, Type(Type::Kind::I1)};
                }

                // Convert f64 to i64 if Pascal expects Integer but runtime returns f64
                // (e.g., Trunc, Round, Floor, Ceil return f64 from runtime but Pascal expects Integer)
                if (pascalRetType.kind == Type::Kind::I64 && rtRetType.kind == Type::Kind::F64)
                {
                    result = emitUnary(Opcode::CastFpToSiRteChk, Type(Type::Kind::I64), result);
                    return {result, Type(Type::Kind::I64)};
                }

                return {result, rtRetType};
            }
        }
    }

    // Regular function call
    const FuncSignature *sig = sema_->lookupFunction(callee);
    Type retType = sig ? mapType(sig->returnType) : Type(Type::Kind::I64);

    // Process arguments - handle interface parameters specially
    std::vector<Value> processedArgs;
    for (size_t i = 0; i < expr.args.size(); ++i)
    {
        // Check if the target parameter is an interface type
        PasType paramType;
        if (sig && i < sig->params.size())
        {
            paramType = sig->params[i].second;
        }

        if (paramType.kind == PasTypeKind::Interface)
        {
            // Get the source expression's type
            PasType srcType = typeOfExpr(*expr.args[i]);

            if (srcType.kind == PasTypeKind::Class)
            {
                // Passing a class to an interface parameter - create a fat pointer
                LowerResult argResult = lowerExpr(*expr.args[i]);
                Value objPtr = argResult.value;

                // Allocate temporary fat pointer on stack (16 bytes)
                Value fatPtr = emitAlloca(16);

                // Store object pointer at offset 0
                emitStore(Type(Type::Kind::Ptr), fatPtr, objPtr);

                // Look up interface table for this class+interface
                std::string ifaceName = paramType.name;
                std::string className = srcType.name;
                std::string ifaceKey = toLower(ifaceName);
                std::string classKey = toLower(className);

                auto layoutIt = interfaceLayouts_.find(ifaceKey);
                auto classLayoutIt = classLayouts_.find(classKey);
                if (layoutIt != interfaceLayouts_.end() && classLayoutIt != classLayouts_.end())
                {
                    // Get itable pointer via runtime lookup
                    usedExterns_.insert("rt_get_interface_impl");
                    Value itablePtr = emitCallRet(Type(Type::Kind::Ptr),
                                                  "rt_get_interface_impl",
                                                  {Value::constInt(classLayoutIt->second.classId),
                                                   Value::constInt(layoutIt->second.interfaceId)});

                    // Store itable pointer at offset 8
                    Value itablePtrAddr = emitGep(fatPtr, Value::constInt(8));
                    emitStore(Type(Type::Kind::Ptr), itablePtrAddr, itablePtr);
                }

                // Pass the address of the fat pointer
                processedArgs.push_back(fatPtr);
            }
            else if (srcType.kind == PasTypeKind::Interface)
            {
                // Passing an interface to an interface parameter
                // We need to copy the fat pointer to a new temporary because
                // tail call optimization might reuse the caller's stack frame
                Value srcSlot;
                bool foundSrc = false;

                if (expr.args[i]->kind == ExprKind::Name)
                {
                    const auto &nameExpr = static_cast<const NameExpr &>(*expr.args[i]);
                    std::string key = toLower(nameExpr.name);
                    auto localIt = locals_.find(key);
                    if (localIt != locals_.end())
                    {
                        srcSlot = localIt->second;
                        foundSrc = true;
                    }
                }

                if (!foundSrc)
                {
                    // Complex expression - lower it
                    LowerResult argResult = lowerExpr(*expr.args[i]);
                    srcSlot = argResult.value;
                }

                // Allocate a fresh temporary fat pointer and copy contents
                Value fatPtr = emitAlloca(16);

                // Copy object pointer
                Value srcObjPtr = emitLoad(Type(Type::Kind::Ptr), srcSlot);
                emitStore(Type(Type::Kind::Ptr), fatPtr, srcObjPtr);

                // Copy itable pointer
                Value srcItablePtrAddr = emitGep(srcSlot, Value::constInt(8));
                Value srcItablePtr = emitLoad(Type(Type::Kind::Ptr), srcItablePtrAddr);
                Value dstItablePtrAddr = emitGep(fatPtr, Value::constInt(8));
                emitStore(Type(Type::Kind::Ptr), dstItablePtrAddr, srcItablePtr);

                processedArgs.push_back(fatPtr);
            }
            else
            {
                // Unexpected type - just pass as-is
                processedArgs.push_back(args[i]);
            }
        }
        else
        {
            // Non-interface parameter - use already lowered value
            processedArgs.push_back(args[i]);
        }
    }

    if (retType.kind == Type::Kind::Void)
    {
        emitCall(callee, processedArgs);
        return {Value::constInt(0), retType};
    }
    else
    {
        Value result = emitCallRet(retType, callee, processedArgs);
        return {result, retType};
    }
}

} // namespace il::frontends::pascal
