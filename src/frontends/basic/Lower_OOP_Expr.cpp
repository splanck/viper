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

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/Semantic_OOP.hpp"

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
    if (const auto *var = as<const VarExpr>(expr))
    {
        SlotType slotInfo = getSlotType(var->name);
        if (slotInfo.isObject)
            return slotInfo.objectClass;
        return {};
    }
    if (is<MeExpr>(expr))
    {
        SlotType slotInfo = getSlotType("ME");
        if (slotInfo.isObject)
            return slotInfo.objectClass;
        return {};
    }
    if (const auto *alloc = as<const NewExpr>(expr))
    {
        return alloc->className;
    }
    if (const auto *arr = as<const ArrayExpr>(expr))
    {
        // Array element access inherits the element's class from the array symbol
        const auto *info = findSymbol(arr->name);
        if (info && info->isObject && !info->objectClass.empty())
            return info->objectClass;
        return {};
    }
    if (const auto *access = as<const MemberAccessExpr>(expr))
    {
        // BUG-061 fix: Check if the FIELD itself is an object type, not the base
        if (access->base)
        {
            std::string baseClass = resolveObjectClass(*access->base);
            if (!baseClass.empty())
            {
                // Look up the field type in the class layout
                if (auto fieldType = findFieldType(baseClass, access->member))
                {
                    // Only return a class name if the field is an object type (custom class)
                    // For primitive types (I64, F64, Str, Bool), return empty string
                    if (*fieldType == ::il::frontends::basic::Type::I64 ||
                        *fieldType == ::il::frontends::basic::Type::F64 ||
                        *fieldType == ::il::frontends::basic::Type::Str ||
                        *fieldType == ::il::frontends::basic::Type::Bool)
                    {
                        return {};  // Field is primitive, not an object
                    }
                    // If it's a custom type, it must be an object
                    // The field type is stored as the class name for object fields
                    // For now, we know it's an object but don't have the class name
                    // This is a limitation - we need to track object field class names
                    // TODO: Store object class names in FieldInfo
                    return {};  // Conservative: treat as non-object for now
                }
            }
        }
        return {};
    }
    if (const auto *call = as<const MethodCallExpr>(expr))
    {
        // BUG-039/BUG-040 fix: Method calls should only be treated as returning objects
        // if the method's return type is actually a class type (not INTEGER, STRING, etc.)
        if (call->base)
        {
            std::string baseClass = resolveObjectClass(*call->base);
            if (!baseClass.empty())
            {
                // Check the method's return type
                if (auto retType = findMethodReturnType(baseClass, call->method))
                {
                    // Only return a class name if the method returns a class type (ptr)
                    // For primitive types (I64, F64, Str, Bool), return empty string
                    if (*retType == ::il::frontends::basic::Type::I64 ||
                        *retType == ::il::frontends::basic::Type::F64 ||
                        *retType == ::il::frontends::basic::Type::Str ||
                        *retType == ::il::frontends::basic::Type::Bool)
                    {
                        return {};
                    }
                    // If it's a custom type, we need to look it up
                    // For now, assume it's an object if it's not a known primitive
                    // This handles class return types
                    return baseClass;  // TODO: Should return the actual return class, not base class
                }
            }
        }
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
    const auto *sym = findSymbol("ME");
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
std::optional<Lowerer::MemberFieldAccess> Lowerer::resolveMemberField(const MemberAccessExpr &expr)
{
    if (!expr.base)
        return std::nullopt;

    RVal base = lowerExpr(*expr.base);
    std::string className = resolveObjectClass(*expr.base);
    // Access control for fields: Private may only be accessed within the declaring class.
    if (!className.empty())
    {
        std::string qname = qualify(className);
        if (const ClassInfo *cinfo = oopIndex_.findClass(qname))
        {
            for (const auto &f : cinfo->fields)
            {
                if (f.name == expr.member)
                {
                    if (f.access == Access::Private && currentClass() != cinfo->qualifiedName)
                    {
                        if (auto *em = diagnosticEmitter())
                        {
                            std::string msg = "cannot access private member '" + expr.member +
                                              "' of class '" + cinfo->qualifiedName + "'";
                            em->emit(il::support::Severity::Error,
                                     "B2021",
                                     expr.loc,
                                     static_cast<uint32_t>(expr.member.size()),
                                     std::move(msg));
                        }
                        else
                        {
                            std::fprintf(stderr,
                                         "B2021: cannot access private member '%s' of class '%s'\n",
                                         expr.member.c_str(),
                                         qname.c_str());
                        }
                        return std::nullopt;
                    }
                    break;
                }
            }
        }
    }
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

std::optional<Lowerer::MemberFieldAccess> Lowerer::resolveImplicitField(std::string_view name,
                                                                        il::support::SourceLoc loc)
{
    const FieldScope *scope = activeFieldScope();
    if (!scope || !scope->layout)
        return std::nullopt;

    const auto *field = scope->layout->findField(name);
    if (!field)
        return std::nullopt;

    const auto *selfInfo = findSymbol("ME");
    if (!selfInfo || !selfInfo->slotId)
        return std::nullopt;

    curLoc = loc;
    Value selfPtr = emitLoad(Type(Type::Kind::Ptr), Value::temp(*selfInfo->slotId));
    curLoc = loc;
    Value fieldPtr = emitBinary(Opcode::GEP,
                                Type(Type::Kind::Ptr),
                                selfPtr,
                                Value::constInt(static_cast<long long>(field->offset)));
    MemberFieldAccess access;
    access.ptr = fieldPtr;
    access.ilType = ilTypeForAstType(field->type);
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
    // Compute the instance (self) argument. For BASE-qualified calls, use ME.
    Value selfArg;
    if (const auto *v = as<const VarExpr>(*expr.base); v && v->name == "BASE")
    {
        const auto *sym = findSymbol("ME");
        if (sym && sym->slotId)
        {
            curLoc = expr.loc;
            selfArg = emitLoad(Type(Type::Kind::Ptr), Value::temp(*sym->slotId));
        }
        else
        {
            selfArg = Value::null();
        }
    }
    else
    {
        RVal base = lowerExpr(*expr.base);
        selfArg = base.value;
    }
    // Access control for methods: Private may only be called within the declaring class.
    if (!className.empty())
    {
        std::string qname = qualify(className);
        if (const ClassInfo *cinfo = oopIndex_.findClass(qname))
        {
            auto it = cinfo->methods.find(expr.method);
            if (it != cinfo->methods.end() && it->second.sig.access == Access::Private &&
                currentClass() != cinfo->qualifiedName)
            {
                if (auto *em = diagnosticEmitter())
                {
                    std::string msg = "cannot access private member '" + expr.method +
                                      "' of class '" + cinfo->qualifiedName + "'";
                    em->emit(il::support::Severity::Error,
                             "B2021",
                             expr.loc,
                             static_cast<uint32_t>(expr.method.size()),
                             std::move(msg));
                }
                else
                {
                    std::fprintf(stderr,
                                 "B2021: cannot access private member '%s' of class '%s'\n",
                                 expr.method.c_str(),
                                 qname.c_str());
                }
                return {Value::constInt(0), Type(Type::Kind::I64)};
            }
        }
    }

    std::vector<Value> args;
    args.reserve(expr.args.size() + 1);
    args.push_back(selfArg);
    for (const auto &arg : expr.args)
    {
        if (!arg)
            continue;
        RVal lowered = lowerExpr(*arg);
        args.push_back(lowered.value);
    }

    curLoc = expr.loc;
    const std::string qname = qualify(className);

    // Detect BASE-qualified calls conservatively: treat `BASE` as a direct call cue.
    bool baseQualified = false;
    if (const auto *v = as<const VarExpr>(*expr.base))
        baseQualified = (v->name == "BASE");

    // Determine if the target is virtual via OOP index.
    int slot = -1;
    if (!qname.empty())
        slot = getVirtualSlot(oopIndex_, qname, expr.method);

    // Determine the target class for direct dispatch. For BASE-qualified calls,
    // we must resolve to the immediate base of the current lowering class.
    std::string directQClass = qname;
    if (baseQualified)
    {
        const std::string cur = currentClass();
        if (!cur.empty())
        {
            if (const ClassInfo *ci = oopIndex_.findClass(cur))
            {
                if (!ci->baseQualified.empty())
                    directQClass = ci->baseQualified;
            }
        }
    }

    // Name of the direct callee when not using virtual dispatch.
    std::string emitClassName = directQClass;
    if (!directQClass.empty())
    {
        if (const ClassInfo *ci = oopIndex_.findClass(directQClass))
            emitClassName = ci->qualifiedName;
    }
    std::string directCallee = emitClassName.empty() ? expr.method : mangleMethod(emitClassName, expr.method);

    // If virtual and not BASE-qualified, emit call.indirect; otherwise direct call or interface
    // dispatch. Interface dispatch via (expr AS IFACE).Method: detect AS with interface target.
    auto tryInterfaceDispatch = [&]() -> std::optional<RVal>
    {
        const AsExpr *asBase = as<const AsExpr>(*expr.base);
        if (!asBase)
            return std::nullopt;
        // Build dotted name for interface and locate InterfaceInfo
        std::string dotted;
        for (size_t i = 0; i < asBase->typeName.size(); ++i)
        {
            if (i)
                dotted.push_back('.');
            dotted += asBase->typeName[i];
        }
        const InterfaceInfo *iface = nullptr;
        for (const auto &p : oopIndex_.interfacesByQname())
        {
            if (p.first == dotted)
            {
                iface = &p.second;
                break;
            }
        }
        if (!iface)
            return std::nullopt;
        // Recover slot index by name match (and simple arity check when possible)
        int slotIndex = -1;
        std::size_t userArity = expr.args.size();
        for (std::size_t idx = 0; idx < iface->slots.size(); ++idx)
        {
            const auto &sig = iface->slots[idx];
            if (sig.name != expr.method)
                continue;
            if (sig.paramTypes.size() == userArity)
            {
                slotIndex = static_cast<int>(idx);
                break;
            }
            // Fallback: pick first name match when arity differs (best-effort)
            if (slotIndex < 0)
                slotIndex = static_cast<int>(idx);
        }
        if (slotIndex < 0)
            return std::nullopt;

        // Lookup itable, load function pointer at slot, and call.indirect
        Value itable = emitCallRet(
            Type(Type::Kind::Ptr), "rt_itable_lookup", {selfArg, Value::constInt(iface->ifaceId)});
        const long long offset = static_cast<long long>(slotIndex * 8ULL);
        Value entryPtr =
            emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), itable, Value::constInt(offset));
        Value fnPtr = emitLoad(Type(Type::Kind::Ptr), entryPtr);

        // Determine return type from interface signature when available.
        Type retTy = Type(Type::Kind::Void);
        if (slotIndex >= 0 && static_cast<std::size_t>(slotIndex) < iface->slots.size())
        {
            if (iface->slots[static_cast<std::size_t>(slotIndex)].returnType)
            {
                retTy =
                    ilTypeForAstType(*iface->slots[static_cast<std::size_t>(slotIndex)].returnType);
            }
        }

        if (retTy.kind != Type(Type::Kind::Void).kind)
        {
            Value result = emitCallIndirectRet(retTy, fnPtr, args);
            if (retTy.kind == Type::Kind::Str)
                deferReleaseStr(result);
            else if (retTy.kind == Type::Kind::Ptr && !className.empty())
                deferReleaseObj(result, className);
            return RVal{result, retTy};
        }
        emitCallIndirect(fnPtr, args);
        return RVal{Value::constInt(0), Type(Type::Kind::I64)};
    };

    if (auto dispatched = tryInterfaceDispatch())
        return *dispatched;

    // If virtual and not BASE-qualified, emit call.indirect; otherwise direct call.
    if (slot >= 0 && !baseQualified)
    {
        // Indirect callee operand uses the mangled method identifier as a global.
        Value calleeOp = Value::global(directCallee);
        if (auto retType = findMethodReturnType(className, expr.method))
        {
            Type ilRetTy = ilTypeForAstType(*retType);
            Value result = emitCallIndirectRet(ilRetTy, calleeOp, args);
            return {result, ilRetTy};
        }
        emitCallIndirect(calleeOp, args);
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Direct call path.
    // For BASE-qualified direct calls, consult the resolved base class for return type.
    const std::string retClassLookup = baseQualified ? directQClass : qname;
    if (auto retType = findMethodReturnType(retClassLookup, expr.method))
    {
        Type ilRetTy = ilTypeForAstType(*retType);
        Value result = emitCallRet(ilRetTy, directCallee, args);
        if (ilRetTy.kind == Type::Kind::Str)
            deferReleaseStr(result);
        else if (ilRetTy.kind == Type::Kind::Ptr && !className.empty())
            deferReleaseObj(result, className);
        return {result, ilRetTy};
    }
    emitCall(directCallee, args);
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

} // namespace il::frontends::basic
