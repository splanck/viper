//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements object-oriented expression lowering for the BASIC front end. The
// helpers translate AST nodes representing classes, object creation, field
// access, and method dispatch into IL operations wired to runtime support.
// Class layout metadata produced by earlier scans guides offset calculations so
// the generated IL maintains ABI alignment with the runtime object model.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Converts BASIC OOP expressions into IL instructions.
/// @details The lowering routines centralise object semantics, ensuring every
///          object manipulation flows through a consistent runtime API. Sharing
///          the implementation prevents drift between allocation, field access,
///          and method invocation while minimising dependencies from other
///          lowering modules.

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
/// @brief Map a BASIC AST type to its IL representation.
///
/// @details BASIC's object system reuses the scalar type lattice. This helper
///          translates the enumerants to their IL equivalents so callers can
///          emit typed loads and stores without open-coding the mapping. Unknown
///          enumerants conservatively fall back to 64-bit integers.
///
/// @param ty BASIC AST type enumerant.
/// @return Equivalent IL type used when emitting instructions.
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

/// @brief Deduce the class name for an object-valued expression.
///
/// @details The method peels apart variable references, `ME` self references,
///          `NEW` expressions, member access chains, and method calls to recover
///          the originating object class. The information comes from the
///          lowerer's recorded slot metadata populated during scanning.
///
/// @param expr Expression representing an object in some form.
/// @return Class name recorded for the expression, or empty string on failure.
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

/// @brief Lower an object allocation expression.
///
/// @details Resolves class metadata to determine object size and runtime class
///          identifier, then requests the object allocation helper. After
///          allocation the constructor is invoked with the new object as the
///          first parameter followed by lowered arguments.
///
/// @param expr AST node representing `NEW Class(args...)`.
/// @return Result value holding the pointer to the allocated object.
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

/// @brief Lower the implicit `ME` self reference.
///
/// @details Retrieves the stored slot for the current method's receiver and
///          emits a load from it. Missing slot information degrades gracefully
///          to a null pointer so diagnostics can handle the error path later.
///
/// @param expr AST node representing the `ME` expression.
/// @return Result value containing the loaded object pointer.
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

/// @brief Lower a field access expression on an object.
///
/// @details Resolves the base class, looks up the field offset within the class
///          layout, and emits a GEP followed by a typed load. Unknown classes or
///          fields return a zero literal so later passes can surface diagnostics
///          without crashing.
///
/// @param expr AST node describing the member access.
/// @return Result value carrying the loaded field content.
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

/// @brief Lower an object method invocation.
///
/// @details Evaluates the receiver, gathers argument values, and dispatches to
///          the mangled method symbol. The receiver is inserted as the leading
///          argument to match the runtime calling convention. Unknown class
///          metadata results in a no-op call returning zero so diagnostics can
///          highlight the issue during verification.
///
/// @param expr AST node describing the method call.
/// @return Result value describing the call's return payload (void => zero).
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

