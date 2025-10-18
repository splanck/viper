//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lower_OOP_Expr.cpp
// Purpose: Lower BASIC OOP expressions into IL object runtime operations.
// Key invariants: Object allocations route through runtime helpers and class
//                 layouts computed during scanning; method/field access obeys
//                 recorded offsets.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module
//                     resources.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "support/feature_flags.hpp"

#if VIPER_ENABLE_OOP

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{
namespace
{
[[nodiscard]] Type ilTypeForAstType(Lowerer::AstType ty)
{
    switch (ty)
    {
        case Lowerer::AstType::I64:
            return Type(Type::Kind::I64);
        case Lowerer::AstType::F64:
            return Type(Type::Kind::F64);
        case Lowerer::AstType::Str:
            return Type(Type::Kind::Str);
        case Lowerer::AstType::Bool:
            return Type(Type::Kind::I1);
    }
    return Type(Type::Kind::I64);
}
} // namespace

std::string Lowerer::resolveObjectClass(const Expr &expr) const
{
    if (const auto *var = dynamic_cast<const VarExpr *>(&expr))
    {
        SlotType slotInfo = getSlotType(var->name);
        if (slotInfo.isObject)
            return slotInfo.objectClass;
        return {};
    }
    if (dynamic_cast<const MeExpr *>(&expr) != nullptr)
    {
        SlotType slotInfo = getSlotType("ME");
        if (slotInfo.isObject)
            return slotInfo.objectClass;
        return {};
    }
    if (const auto *alloc = dynamic_cast<const NewExpr *>(&expr))
    {
        return alloc->className;
    }
    if (const auto *access = dynamic_cast<const MemberAccessExpr *>(&expr))
    {
        if (access->base)
            return resolveObjectClass(*access->base);
        return {};
    }
    if (const auto *call = dynamic_cast<const MethodCallExpr *>(&expr))
    {
        if (call->base)
            return resolveObjectClass(*call->base);
        return {};
    }
    return {};
}

Lowerer::RVal Lowerer::lowerNewExpr(const NewExpr &expr)
{
    curLoc = expr.loc;
    std::size_t objectSize = 0;
    std::int64_t classId = 0;
    if (auto layoutIt = classLayouts_.find(expr.className); layoutIt != classLayouts_.end())
    {
        objectSize = layoutIt->second.size;
        classId = layoutIt->second.classId;
    }

    requestHelper(RuntimeFeature::ObjNew);
    Value obj = emitCallRet(
        Type(Type::Kind::Ptr),
        "rt_obj_new_i64",
        {Value::constInt(classId), Value::constInt(static_cast<long long>(objectSize))});

    std::vector<Value> ctorArgs;
    ctorArgs.reserve(expr.args.size() + 1);
    ctorArgs.push_back(obj);
    for (const auto &arg : expr.args)
    {
        if (!arg)
            continue;
        RVal lowered = lowerExpr(*arg);
        ctorArgs.push_back(lowered.value);
    }

    curLoc = expr.loc;
    emitCall(mangleClassCtor(expr.className), ctorArgs);
    return {obj, Type(Type::Kind::Ptr)};
}

Lowerer::RVal Lowerer::lowerMeExpr(const MeExpr &expr)
{
    curLoc = expr.loc;
    const auto *sym = findSymbol("ME");
    if (!sym || !sym->slotId)
        return {Value::null(), Type(Type::Kind::Ptr)};
    Value slot = Value::temp(*sym->slotId);
    Value self = emitLoad(Type(Type::Kind::Ptr), slot);
    return {self, Type(Type::Kind::Ptr)};
}

Lowerer::RVal Lowerer::lowerMemberAccessExpr(const MemberAccessExpr &expr)
{
    if (!expr.base)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    RVal base = lowerExpr(*expr.base);
    std::string className = resolveObjectClass(*expr.base);
    auto layoutIt = classLayouts_.find(className);
    if (layoutIt == classLayouts_.end())
        return {Value::constInt(0), Type(Type::Kind::I64)};

    const ClassLayout::Field *field = layoutIt->second.findField(expr.member);
    if (!field)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    curLoc = expr.loc;
    Value fieldPtr = emitBinary(Opcode::GEP,
                                Type(Type::Kind::Ptr),
                                base.value,
                                Value::constInt(static_cast<long long>(field->offset)));
    Type fieldTy = ilTypeForAstType(field->type);
    Value loaded = emitLoad(fieldTy, fieldPtr);
    return {loaded, fieldTy};
}

Lowerer::RVal Lowerer::lowerMethodCallExpr(const MethodCallExpr &expr)
{
    if (!expr.base)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    std::string className = resolveObjectClass(*expr.base);
    RVal base = lowerExpr(*expr.base);

    std::vector<Value> args;
    args.reserve(expr.args.size() + 1);
    args.push_back(base.value);
    for (const auto &arg : expr.args)
    {
        if (!arg)
            continue;
        RVal lowered = lowerExpr(*arg);
        args.push_back(lowered.value);
    }

    curLoc = expr.loc;
    std::string callee = className.empty() ? expr.method : mangleMethod(className, expr.method);
    emitCall(callee, args);
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

} // namespace il::frontends::basic

#endif // VIPER_ENABLE_OOP
