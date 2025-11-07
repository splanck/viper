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

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace il::frontends::basic
{
namespace
{
/// @brief Translate a BASIC AST type into the corresponding IL type.
///
/// @details Provides the shared mapping used by member access lowering when the
///          class layout records only high-level BASIC types.  Defaults to the
///          integral representation when a new enumerator is introduced before
///          the lowering logic is updated, preserving legacy behaviour until the
///          mapping is extended explicitly.
///
/// @param ty BASIC type enumerator describing the field layout.
/// @return Equivalent IL type descriptor.
[[nodiscard]] il::core::Type ilTypeForAstType(::il::frontends::basic::Type ty)
{
    using IlType = il::core::Type;
    switch (ty)
    {
        case ::il::frontends::basic::Type::I64:
            return IlType(IlType::Kind::I64);
        case ::il::frontends::basic::Type::F64:
            return IlType(IlType::Kind::F64);
        case ::il::frontends::basic::Type::Str:
            return IlType(IlType::Kind::Str);
        case ::il::frontends::basic::Type::Bool:
            return IlType(IlType::Kind::I1);
    }
    return IlType(IlType::Kind::I64);
}
} // namespace

/// @brief Determine the class name associated with an OOP expression.
///
/// @details Walks the expression tree to find the originating class, handling
///          variables, the implicit `ME` reference, @c NEW expressions, member
///          access, and method calls.  Returns an empty string when the class
///          cannot be determined, allowing callers to fall back to conservative
///          behaviour.
///
/// @param expr AST node describing the expression under inspection.
/// @return Class name string or empty when no object type is associated.
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
        SlotType slotInfo = getSlotType("Me");
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

/// @brief Lower a BASIC @c NEW expression into IL runtime calls.
///
/// @details Queries the cached class layout to determine the allocation size
///          and class identifier, requests the object-allocation runtime helper,
///          and emits the constructor call with the newly created object prepended
///          to the argument list.  The resulting pointer value is packaged in an
///          @ref RVal ready for further lowering.
///
/// @param expr AST node representing the @c NEW expression.
/// @return Runtime value describing the allocated object pointer.
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

/// @brief Lower the implicit @c ME expression to a pointer load.
///
/// @details Looks up the @c ME symbol in the current scope, falling back to a
///          null pointer when the binding is absent (for example, outside a
///          method).  When present the helper emits a load from the associated
///          slot so callers receive a runtime object pointer.
///
/// @param expr AST node representing the @c ME keyword.
/// @return Runtime value describing the current object instance.
Lowerer::RVal Lowerer::lowerMeExpr(const MeExpr &expr)
{
    curLoc = expr.loc;
    const auto *sym = findSymbol("Me");
    if (!sym || !sym->slotId)
        return {Value::null(), Type(Type::Kind::Ptr)};
    Value slot = Value::temp(*sym->slotId);
    Value self = emitLoad(Type(Type::Kind::Ptr), slot);
    return {self, Type(Type::Kind::Ptr)};
}

/// @brief Lower a member access expression to loads from the object layout.
///
/// @details Evaluates the base expression, consults the cached class layout for
///          the member, and emits a @c GEP followed by a load using the field's
///          static type.  When any prerequisite (base, layout, or field) is
///          missing, the helper returns a zero-valued integer as a defensive
///          fallback.
///
/// @param expr AST node describing the member access.
/// @return Runtime value of the selected field, or zero when unresolved.
std::optional<Lowerer::MemberFieldAccess>
Lowerer::resolveMemberField(const MemberAccessExpr &expr)
{
    if (!expr.base)
        return std::nullopt;

    RVal base = lowerExpr(*expr.base);
    std::string className = resolveObjectClass(*expr.base);
    auto layoutIt = classLayouts_.find(className);
    if (layoutIt == classLayouts_.end())
        return std::nullopt;

    const ClassLayout::Field *field = layoutIt->second.findField(expr.member);
    if (!field)
        return std::nullopt;

    curLoc = expr.loc;
    Value fieldPtr = emitBinary(Opcode::GEP,
                                Type(Type::Kind::Ptr),
                                base.value,
                                Value::constInt(static_cast<long long>(field->offset)));
    Type fieldTy = ilTypeForAstType(field->type);
    MemberFieldAccess access;
    access.ptr = fieldPtr;
    access.ilType = fieldTy;
    access.astType = field->type;
    return access;
}

Lowerer::RVal Lowerer::lowerMemberAccessExpr(const MemberAccessExpr &expr)
{
    auto access = resolveMemberField(expr);
    if (!access)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    curLoc = expr.loc;
    Value loaded = emitLoad(access->ilType, access->ptr);
    return {loaded, access->ilType};
}

/// @brief Lower an instance method call, dispatching through the mangled name.
///
/// @details Evaluates the receiver expression, prepends it to the argument list,
///          and emits a direct call using the class-aware mangled identifier.
///          When the class name cannot be resolved the raw method name is used,
///          preserving compatibility with late-bound scenarios.
///
/// @param expr AST node representing the method invocation.
/// @return Result value placeholder; currently the runtime returns @c void so
///         a zero integer is used to preserve SSA expectations.
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
