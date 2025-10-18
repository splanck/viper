//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lower_OOP_Expr.cpp
// Purpose: Lower BASIC OOP expression nodes into IL when the OOP feature is
//          enabled, handling object allocation, member reads, and method calls.
// Key invariants: Class layouts discovered during scanning provide deterministic
//                 offsets and constructor/method signatures.
// Ownership/Lifetime: Operates on the active Lowerer instance without owning
//                     AST nodes or IL state.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "support/feature_flags.hpp"

#if VIPER_ENABLE_OOP

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{
namespace
{
using AstType = ::il::frontends::basic::Type;

[[nodiscard]] Type ilTypeForAst(AstType ty)
{
    switch (ty)
    {
        case AstType::I64: return Type(Type::Kind::I64);
        case AstType::F64: return Type(Type::Kind::F64);
        case AstType::Str: return Type(Type::Kind::Str);
        case AstType::Bool: return Type(Type::Kind::I1);
    }
    return Type(Type::Kind::I64);
}

[[nodiscard]] Lowerer::RVal coerceForParam(Lowerer &lowerer,
                                           Lowerer::RVal value,
                                           const Lowerer::ClassLayout::ParamInfo &param,
                                           il::support::SourceLoc loc)
{
    if (param.isArray)
        return value;

    switch (param.type)
    {
        case AstType::I64: return lowerer.ensureI64(std::move(value), loc);
        case AstType::F64: return lowerer.ensureF64(std::move(value), loc);
        case AstType::Bool: return lowerer.coerceToBool(std::move(value), loc);
        case AstType::Str:
        default: return value;
    }
}
} // namespace

std::int64_t Lowerer::allocateClassId()
{
    return nextClassId++;
}

std::optional<std::string> Lowerer::resolveObjectClass(const Expr &expr) const
{
    if (const auto *var = dynamic_cast<const VarExpr *>(&expr))
    {
        if (var->name.empty())
            return std::nullopt;
        if (const auto *sym = findSymbol(var->name); sym && sym->isObject)
            return sym->objectClass;
        return std::nullopt;
    }
    if (dynamic_cast<const MeExpr *>(&expr) != nullptr)
    {
        if (const auto *sym = findSymbol("ME"); sym && sym->isObject)
            return sym->objectClass;
        return std::nullopt;
    }
    if (const auto *ctor = dynamic_cast<const NewExpr *>(&expr))
    {
        return ctor->className;
    }
    if (const auto *access = dynamic_cast<const MemberAccessExpr *>(&expr))
    {
        if (!access->base)
            return std::nullopt;
        return resolveObjectClass(*access->base);
    }
    return std::nullopt;
}

Lowerer::RVal Lowerer::lowerMeExpr(const MeExpr &expr)
{
    curLoc = expr.loc;
    const auto *info = findSymbol("ME");
    if (!info || !info->slotId)
        return {Value::null(), Type(Type::Kind::Ptr)};
    Value slot = Value::temp(*info->slotId);
    Value self = emitLoad(Type(Type::Kind::Ptr), slot);
    return {self, Type(Type::Kind::Ptr)};
}

Lowerer::RVal Lowerer::lowerNewExpr(const NewExpr &expr)
{
    curLoc = expr.loc;
    auto layoutIt = classLayouts_.find(expr.className);
    if (layoutIt == classLayouts_.end())
        return {Value::null(), Type(Type::Kind::Ptr)};
    const ClassLayout &layout = layoutIt->second;

    Value classIdVal = emitConstI64(layout.classId);
    Value sizeVal = emitConstI64(static_cast<std::int64_t>(layout.size));
    Value obj = emitCallRet(Type(Type::Kind::Ptr), "rt_obj_new_i64", {classIdVal, sizeVal});

    std::vector<Value> ctorArgs;
    ctorArgs.reserve(expr.args.size() + 1);
    ctorArgs.push_back(obj);
    for (std::size_t i = 0; i < expr.args.size(); ++i)
    {
        RVal lowered = lowerExpr(*expr.args[i]);
        if (i < layout.ctorParams.size())
            lowered = coerceForParam(*this, std::move(lowered), layout.ctorParams[i], expr.loc);
        ctorArgs.push_back(lowered.value);
    }

    std::string ctorName = mangleClassCtor(expr.className);
    emitCall(ctorName, ctorArgs);
    return {obj, Type(Type::Kind::Ptr)};
}

Lowerer::RVal Lowerer::lowerMemberAccessExpr(const MemberAccessExpr &expr)
{
    if (!expr.base)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    RVal baseVal = lowerExpr(*expr.base);
    curLoc = expr.loc;

    auto className = resolveObjectClass(*expr.base);
    if (!className)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    auto layoutIt = classLayouts_.find(*className);
    if (layoutIt == classLayouts_.end())
        return {Value::constInt(0), Type(Type::Kind::I64)};

    const auto *field = layoutIt->second.findField(expr.member);
    if (!field)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    Value offset = Value::constInt(static_cast<std::int64_t>(field->offset));
    Value fieldPtr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), baseVal.value, offset);
    Type fieldTy = ilTypeForAst(field->type);
    Value loaded = emitLoad(fieldTy, fieldPtr);
    return {loaded, fieldTy};
}

Lowerer::RVal Lowerer::lowerMethodCallExpr(const MethodCallExpr &expr)
{
    if (!expr.base)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    auto className = resolveObjectClass(*expr.base);
    RVal baseVal = lowerExpr(*expr.base);
    curLoc = expr.loc;

    const ClassLayout::MethodInfo *methodInfo = nullptr;
    if (className)
    {
        auto layoutIt = classLayouts_.find(*className);
        if (layoutIt != classLayouts_.end())
        {
            auto it = layoutIt->second.methods.find(expr.method);
            if (it != layoutIt->second.methods.end())
                methodInfo = &it->second;
        }
    }

    std::vector<Value> args;
    args.reserve(expr.args.size() + 1);
    args.push_back(baseVal.value);
    for (std::size_t i = 0; i < expr.args.size(); ++i)
    {
        RVal lowered = lowerExpr(*expr.args[i]);
        if (methodInfo && i < methodInfo->params.size())
            lowered = coerceForParam(*this, std::move(lowered), methodInfo->params[i], expr.loc);
        args.push_back(lowered.value);
    }

    if (!className)
    {
        emitCall(expr.method, args);
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    std::string callee = mangleMethod(*className, expr.method);
    if (methodInfo && methodInfo->retType)
    {
        Type retTy = ilTypeForAst(*methodInfo->retType);
        Value result = emitCallRet(retTy, callee, args);
        return {result, retTy};
    }

    emitCall(callee, args);
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

} // namespace il::frontends::basic

#endif // VIPER_ENABLE_OOP
