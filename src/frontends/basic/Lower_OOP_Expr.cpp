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
/// @brief Convert a BASIC AST type into the corresponding IL type handle.
///
/// @details Collapses the BASIC surface type system down to the subset of IL
///          kinds required by the object lowering path.  Object references map
///          to pointer types while primitive values retain their scalar kinds.
///
/// @param ty BASIC AST type enumerator.
/// @return IL type used for emitted instructions.
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

/// @brief Determine the object class associated with an expression.
///
/// @details Inspects variables, @c ME references, constructor calls, member
///          accesses, and method calls to recover the originating class name.
///          The method consults slot metadata populated during scanning to keep
///          lookups cheap and avoids emitting diagnostics here—the caller is
///          expected to handle missing metadata gracefully.
///
/// @param expr Expression whose class should be determined.
/// @return Name of the resolved class or an empty string when unknown.
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

/// @brief Lower an object allocation expression into runtime helper calls.
///
/// @details Looks up the pre-computed class layout to determine object size and
///          class identifier, emits the allocation call, and forwards the
///          resulting object pointer to the class constructor.  Missing layout
///          entries degrade gracefully by passing zeroed metadata, allowing
///          diagnostics to occur elsewhere.
///
/// @param expr Constructor expression describing the allocation.
/// @return Resulting pointer value paired with its IL type.
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

/// @brief Lower a @c ME expression that references the current object instance.
///
/// @details Retrieves the cached slot identifier for the implicit receiver,
///          emits a load of the pointer stored in that slot, and returns a
///          pointer-typed value.  When the symbol is missing the lowering
///          returns a null pointer so upstream diagnostics can flag the error.
///
/// @param expr AST node representing the @c ME keyword.
/// @return Loaded receiver pointer or a null sentinel when unavailable.
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

/// @brief Lower access to an object field into pointer arithmetic and loads.
///
/// @details Resolves the class layout of the base expression, computes the
///          field pointer via a `gep`, and loads the field value using the
///          appropriate IL type.  Absent layouts or unknown fields yield a zero
///          literal so subsequent verification can surface precise diagnostics.
///
/// @param expr Member access expression to lower.
/// @return Loaded field value along with its IL type.
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

/// @brief Lower a method invocation, injecting the receiver as the first argument.
///
/// @details Resolves the receiver class to select the correct mangled method
///          name, evaluates each argument (including the receiver), and emits a
///          call instruction.  Return values are ignored because BASIC methods
///          are currently modelled as procedures that return no value.
///
/// @param expr Method call expression describing the invocation.
/// @return Null integer slot signalling the absence of a return value.
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

