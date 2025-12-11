//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer_Expr_Name.cpp
// Purpose: Name expression lowering for Pascal AST to IL.
// Key invariants: Resolves names through locals, globals, with contexts, fields.
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

LowerResult Lowerer::lowerName(const NameExpr &expr)
{
    std::string key = toLower(expr.name);

    // Check locals FIRST - user-defined symbols shadow builtins
    auto localIt = locals_.find(key);
    if (localIt != locals_.end())
    {
        // First check our own localTypes_ map (for procedure locals)
        // then fall back to semantic analyzer (for global variables)
        Type ilType = Type(Type::Kind::I64);
        auto localTypeIt = localTypes_.find(key);
        if (localTypeIt != localTypes_.end())
        {
            ilType = mapType(localTypeIt->second);
        }
        else
        {
            if (auto varType = sema_->lookupVariable(key))
                ilType = mapType(*varType);
        }
        Value slot = localIt->second;
        Value loaded = emitLoad(ilType, slot);
        return {loaded, ilType};
    }

    // Check for global variables (module-level, accessible from all functions)
    auto globalIt = globalTypes_.find(key);
    if (globalIt != globalTypes_.end())
    {
        Type ilType = mapType(globalIt->second);
        Value addr = getGlobalVarAddr(key, globalIt->second);
        Value loaded = emitLoad(ilType, addr);
        return {loaded, ilType};
    }

    // Check 'with' contexts for field/property access (innermost first)
    for (auto it = withContexts_.rbegin(); it != withContexts_.rend(); ++it)
    {
        const WithContext &ctx = *it;
        if (ctx.type.kind == PasTypeKind::Class)
        {
            auto *classInfo = sema_->lookupClass(toLower(ctx.type.name));
            if (classInfo)
            {
                // Check fields
                auto fieldIt = classInfo->fields.find(key);
                if (fieldIt != classInfo->fields.end())
                {
                    Value objPtr = emitLoad(Type(Type::Kind::Ptr), ctx.slot);
                    // Build type with fields for getFieldAddress
                    PasType classTypeWithFields = ctx.type;
                    for (const auto &[fname, finfo] : classInfo->fields)
                    {
                        classTypeWithFields.fields[fname] = std::make_shared<PasType>(finfo.type);
                    }
                    auto [fieldAddr, fieldType] =
                        getFieldAddress(objPtr, classTypeWithFields, expr.name);
                    Value fieldVal = emitLoad(fieldType, fieldAddr);
                    return {fieldVal, fieldType};
                }
                // Check properties
                auto propIt = classInfo->properties.find(key);
                if (propIt != classInfo->properties.end())
                {
                    Value objPtr = emitLoad(Type(Type::Kind::Ptr), ctx.slot);
                    const auto &p = propIt->second;
                    if (p.getter.kind == PropertyAccessor::Kind::Method)
                    {
                        std::string funcName = classInfo->name + "." + p.getter.name;
                        Type retType = mapType(p.type);
                        Value result = emitCallRet(retType, funcName, {objPtr});
                        return {result, retType};
                    }
                    if (p.getter.kind == PropertyAccessor::Kind::Field)
                    {
                        PasType classTypeWithFields = ctx.type;
                        for (const auto &[fname, finfo] : classInfo->fields)
                        {
                            classTypeWithFields.fields[fname] =
                                std::make_shared<PasType>(finfo.type);
                        }
                        auto [fieldAddr, fieldType] =
                            getFieldAddress(objPtr, classTypeWithFields, p.getter.name);
                        Value fieldVal = emitLoad(fieldType, fieldAddr);
                        return {fieldVal, fieldType};
                    }
                }
            }
        }
        else if (ctx.type.kind == PasTypeKind::Record)
        {
            auto fieldIt = ctx.type.fields.find(key);
            if (fieldIt != ctx.type.fields.end() && fieldIt->second)
            {
                // For records, slot holds the record directly
                auto [fieldAddr, fieldType] = getFieldAddress(ctx.slot, ctx.type, expr.name);
                Value fieldVal = emitLoad(fieldType, fieldAddr);
                return {fieldVal, fieldType};
            }
        }
    }

    // Check class fields/properties if inside a method (walk inheritance chain)
    if (!currentClassName_.empty())
    {
        // Walk inheritance chain looking for field or property
        std::string curClass = toLower(currentClassName_);
        while (!curClass.empty())
        {
            auto *classInfo = sema_->lookupClass(curClass);
            if (!classInfo)
                break;

            // Check for field in this class
            auto fieldIt = classInfo->fields.find(key);
            if (fieldIt != classInfo->fields.end())
            {
                auto selfIt = locals_.find("self");
                if (selfIt != locals_.end())
                {
                    Value selfPtr = emitLoad(Type(Type::Kind::Ptr), selfIt->second);
                    // Use currentClassName_ for getFieldAddress - it uses classLayouts_
                    // which includes inherited fields at correct offsets
                    PasType selfType = PasType::classType(currentClassName_);
                    auto [fieldAddr, fieldType] = getFieldAddress(selfPtr, selfType, expr.name);
                    Value fieldVal = emitLoad(fieldType, fieldAddr);
                    return {fieldVal, fieldType};
                }
            }

            // Check for property in this class
            auto propIt = classInfo->properties.find(key);
            if (propIt != classInfo->properties.end())
            {
                auto selfIt = locals_.find("self");
                if (selfIt != locals_.end())
                {
                    Value selfPtr = emitLoad(Type(Type::Kind::Ptr), selfIt->second);
                    const auto &p = propIt->second;
                    // Getter via method - use defining class name
                    if (p.getter.kind == PropertyAccessor::Kind::Method)
                    {
                        std::string funcName = classInfo->name + "." + p.getter.name;
                        Type retType = mapType(p.type);
                        Value result = emitCallRet(retType, funcName, {selfPtr});
                        return {result, retType};
                    }
                    // Getter via field - use currentClassName_ for correct offsets
                    if (p.getter.kind == PropertyAccessor::Kind::Field)
                    {
                        PasType selfType = PasType::classType(currentClassName_);
                        auto [fieldAddr, fieldType] =
                            getFieldAddress(selfPtr, selfType, p.getter.name);
                        Value result = emitLoad(fieldType, fieldAddr);
                        return {result, fieldType};
                    }
                }
            }

            // Move to base class
            curClass = toLower(classInfo->baseClass);
        }
    }

    // Check user-defined constants (from user's const declarations)
    auto constIt = constants_.find(key);
    if (constIt != constants_.end())
    {
        return {constIt->second, Type(Type::Kind::I64)}; // Type approximation
    }

    // Check semantic analyzer for user-defined enum constants and typed constants
    // (These have higher priority than builtin constants like Pi and E)
    if (auto constType = sema_->lookupConstant(key))
    {
        if (constType->kind == PasTypeKind::Enum && constType->enumOrdinal >= 0)
        {
            // Enum constant: emit its ordinal value as an integer
            return {Value::constInt(constType->enumOrdinal), Type(Type::Kind::I64)};
        }
        // Handle Integer constants
        if (constType->kind == PasTypeKind::Integer)
        {
            if (auto val = sema_->lookupConstantInt(key))
            {
                return {Value::constInt(*val), Type(Type::Kind::I64)};
            }
        }
        // Handle Real constants
        if (constType->kind == PasTypeKind::Real)
        {
            if (auto val = sema_->lookupConstantReal(key))
            {
                return {Value::constFloat(*val), Type(Type::Kind::F64)};
            }
            return {Value::constFloat(0.0), Type(Type::Kind::F64)};
        }
        // Handle String constants
        if (constType->kind == PasTypeKind::String)
        {
            if (auto val = sema_->lookupConstantStr(key))
            {
                std::string globalName = getStringGlobal(*val);
                Value strVal = emitConstStr(globalName);
                return {strVal, Type(Type::Kind::Str)};
            }
        }
        // Handle Boolean constants (stored as integers 0/1 in constantValues_)
        if (constType->kind == PasTypeKind::Boolean)
        {
            if (auto val = sema_->lookupConstantInt(key))
            {
                return {Value::constBool(*val != 0), Type(Type::Kind::I1)};
            }
        }
    }

    // Check for built-in math constants (Pi and E from Viper.Math)
    // These are checked LAST so user-defined symbols can shadow them
    if (key == "pi")
    {
        return {Value::constFloat(3.14159265358979323846), Type(Type::Kind::F64)};
    }
    if (key == "e")
    {
        return {Value::constFloat(2.71828182845904523536), Type(Type::Kind::F64)};
    }

    // Check for zero-argument builtin functions (Pascal allows calling without parens)
    if (auto builtinOpt = lookupBuiltin(key))
    {
        const auto &desc = getBuiltinDescriptor(*builtinOpt);
        // Only handle if it can be called with 0 args and has non-void return type
        if (desc.minArgs == 0 && desc.result != ResultKind::Void)
        {
            const char *rtSym = getBuiltinRuntimeSymbol(*builtinOpt);

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
                PasType resultPasType = getBuiltinResultType(*builtinOpt);
                rtRetType = mapType(resultPasType);
            }

            // Also get the Pascal-expected return type for conversion
            PasType pascalResultType = getBuiltinResultType(*builtinOpt);
            Type pascalRetType = mapType(pascalResultType);

            // Emit call with no arguments
            Value result = emitCallRet(rtRetType, rtSym, {});

            // Convert integer to i1 if Pascal expects Boolean but runtime returns integer
            if (pascalRetType.kind == Type::Kind::I1 &&
                (rtRetType.kind == Type::Kind::I32 || rtRetType.kind == Type::Kind::I64))
            {
                // Convert to i1: compare != 0
                Value zero = Value::constInt(0);
                result = emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), result, zero);
                return {result, Type(Type::Kind::I1)};
            }

            return {result, rtRetType};
        }
    }

    // Check for zero-argument user-defined functions (Pascal allows calling without parens)
    if (auto sig = sema_->lookupFunction(key))
    {
        // Only handle if it can be called with 0 args and has non-void return type
        if (sig->requiredParams == 0 && sig->returnType.kind != PasTypeKind::Void)
        {
            Type retType = mapType(sig->returnType);
            // Use the original function name from the signature (preserves case)
            Value result = emitCallRet(retType, sig->name, {});
            return {result, retType};
        }
    }

    // Unknown - return zero
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

} // namespace il::frontends::pascal
