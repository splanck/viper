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
#include "il/runtime/RuntimeSignatures.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/Options.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/Semantic_OOP.hpp"
#include "frontends/basic/ILTypeUtils.hpp"
#include "frontends/basic/sem/OverloadResolution.hpp"
#include "frontends/basic/StringUtils.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

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

        // BUG-107 fix: Module-level object variables don't have slots,
        // check SymbolInfo directly for object class
        if (const auto *info = findSymbol(var->name))
        {
            if (info->isObject && !info->objectClass.empty())
                return info->objectClass;
        }

        // BUG-107 fix: Check module-level scalar object cache (already resolved)
        auto it = moduleObjectClass_.find(std::string(var->name));
        if (it != moduleObjectClass_.end())
            return it->second;

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
    if (const auto *call = as<const CallExpr>(expr))
    {
        // BUG-089 fix: CallExpr might be a field array access (BASIC uses () for both)
        // Check if this is an implicit field array in a class method
        const FieldScope *scope = activeFieldScope();
        if (scope && scope->layout)
        {
            const auto *field = scope->layout->findField(call->callee);
            if (field && field->isArray && !field->objectClassName.empty())
            {
                return qualify(field->objectClassName);
            }
        }

        return {};
    }
    if (const auto *arr = as<const ArrayExpr>(expr))
    {
        // BUG-089 fix: Handle module-level, dotted member, and implicit field arrays
        const auto *info = findSymbol(arr->name);
        if (info && info->isObject && !info->objectClass.empty())
            return info->objectClass;

        // Check if this is a member array with dotted name (e.g., ME.items)
        bool isMemberArray = arr->name.find('.') != std::string::npos;
        if (isMemberArray)
        {
            const std::string &full = arr->name;
            std::size_t dot = full.find('.');
            std::string baseName = full.substr(0, dot);
            std::string fieldName = full.substr(dot + 1);
            std::string klass = getSlotType(baseName).objectClass;
            if (const ClassLayout *layout = findClassLayout(klass))
            {
                if (const ClassLayout::Field *fld = layout->findField(fieldName))
                {
                    if (!fld->objectClassName.empty())
                        return qualify(fld->objectClassName);
                }
            }
        }

        // BUG-089 fix: Check if this is an implicit field access (e.g., items in a method)
        const FieldScope *scope = activeFieldScope();
        if (scope && scope->layout)
        {
            const auto *field = scope->layout->findField(arr->name);
            if (field && !field->objectClassName.empty())
            {
                return qualify(field->objectClassName);
            }
        }
        // BUG-097 fix: If this is a module-level array referenced inside a procedure,
        // recover the element class from the cached module-level object array map.
        // Try the lookup directly - if the name isn't in the cache, it returns empty.
        std::string cls = lookupModuleArrayElemClass(arr->name);
        if (!cls.empty())
        {
            // Resolve canonical lowercase name to declared casing (e.g., 'widget' -> 'WIDGET')
            std::string qualified = qualify(cls);
            return resolveQualifiedClassCasing(qualified);
        }
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
                // BUG-082 fix: Look up the field class name in the class layout
                if (const ClassLayout *layout = findClassLayout(baseClass))
                {
                    const auto *field = layout->findField(access->member);
                    if (field && !field->objectClassName.empty())
                    {
                        // Field is an object type - qualify the class name for proper lookup
                        return qualify(field->objectClassName);
                    }
                }
                // Field is primitive or not found
                return {};
            }
        }
        return {};
    }
    if (const auto *call = as<const MethodCallExpr>(expr))
    {
        // BUG-096/BUG-098 fix: MethodCallExpr might be a field array access
        // (e.g., container.items(0) where items is an array field)
        // Check this BEFORE checking for method return types
        if (call->base)
        {
            std::string baseClass = resolveObjectClass(*call->base);
            if (!baseClass.empty())
            {
                // First check if this is a field array access, not an actual method call
                if (const ClassLayout *layout = findClassLayout(baseClass))
                {
                    const auto *field = layout->findField(call->method);
                    if (field && field->isArray && !field->objectClassName.empty())
                    {
                        // This is a field array access (e.g., obj.arrayField(idx))
                        // Return the array element class
                        return qualify(field->objectClassName);
                    }
                }

                // Not a field array, check the method's return type
                // BUG-099 fix: Use findMethodReturnClassName to get the actual return class
                std::string returnClassName = findMethodReturnClassName(baseClass, call->method);
                if (!returnClassName.empty())
                {
                    return returnClassName;
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

    // Minimal runtime type bridging: NEW Viper.System.Text.StringBuilder()
    if (FrontendOptions::enableRuntimeTypeBridging())
    {
        if (expr.args.empty())
        {
            // Match fully-qualified type
            bool isQualified = false;
            if (!expr.qualifiedType.empty())
            {
                const auto &q = expr.qualifiedType;
                if (q.size() == 4 && string_utils::iequals(q[0], "Viper") &&
                    string_utils::iequals(q[1], "System") && string_utils::iequals(q[2], "Text") &&
                    string_utils::iequals(q[3], "StringBuilder"))
                {
                    isQualified = true;
                }
            }
            // Fallback: check dot-joined className
            if (!isQualified)
            {
                if (string_utils::iequals(expr.className, "Viper.System.Text.StringBuilder"))
                    isQualified = true;
            }
            if (isQualified)
            {
                // Emit direct call to a runtime helper that returns an object pointer.
                if (builder)
                    builder->addExtern("Viper.Strings.Builder.New", Type(Type::Kind::Ptr), {});
                Value obj = emitCallRet(Type(Type::Kind::Ptr), "Viper.Strings.Builder.New", {});
                return {obj, Type(Type::Kind::Ptr)};
            }
        }
    }
    std::size_t objectSize = 0;
    std::int64_t classId = 0;
    if (auto layoutIt = classLayouts_.find(expr.className); layoutIt != classLayouts_.end())
    {
        objectSize = layoutIt->second.size;
        classId = layoutIt->second.classId;
    }

    // Ensure space for vptr at offset 0 even when class has no fields.
    if (objectSize < 8)
        objectSize = 8;
    requestHelper(RuntimeFeature::ObjNew);
    Value obj = emitCallRet(
        Type(Type::Kind::Ptr),
        "rt_obj_new_i64",
        {Value::constInt(classId), Value::constInt(static_cast<long long>(objectSize))});

    // Pre-initialize vptr for dynamic dispatch: build a per-class vtable and store it.
    if (const ClassInfo *ciInit = oopIndex_.findClass(qualify(expr.className)))
    {
        // Derive slot count from recorded method slots across the inheritance chain.
        std::size_t maxSlot = 0;
        bool hasAnyVirtual = false;
        {
            const ClassInfo *cur = ciInit;
            while (cur)
            {
                for (const auto &mp : cur->methods)
                {
                    const auto &mi = mp.second;
                    if (!mi.isVirtual || mi.slot < 0)
                        continue;
                    hasAnyVirtual = true;
                    maxSlot = std::max<std::size_t>(maxSlot, static_cast<std::size_t>(mi.slot));
                }
                if (cur->baseQualified.empty())
                    break;
                cur = oopIndex_.findClass(cur->baseQualified);
            }
        }
        const std::size_t slotCount = hasAnyVirtual ? (maxSlot + 1) : 0;
        if (slotCount > 0)
        {
            if (builder)
                builder->addExtern("rt_alloc", Type(Type::Kind::Ptr), {Type(Type::Kind::I64)});
            const long long bytes = static_cast<long long>(slotCount * 8ULL);
            Value vtblPtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(bytes)});

            auto findImplementorQClass = [&](const std::string &startQ,
                                             const std::string &mname) -> std::string
            {
                const ClassInfo *cur = oopIndex_.findClass(startQ);
                while (cur)
                {
                    auto itM = cur->methods.find(mname);
                    if (itM != cur->methods.end())
                    {
                        if (!itM->second.isAbstract)
                            return cur->qualifiedName;
                    }
                    if (cur->baseQualified.empty())
                        break;
                    cur = oopIndex_.findClass(cur->baseQualified);
                }
                return startQ; // fallback
            };

            // Populate slots by scanning most-derived to bases (prefer derived impls)
            std::vector<std::string> slotToName(slotCount);
            {
                const ClassInfo *cur = ciInit;
                while (cur)
                {
                    for (const auto &mp : cur->methods)
                    {
                        const auto &mname = mp.first;
                        const auto &mi = mp.second;
                        if (!mi.isVirtual || mi.slot < 0)
                            continue;
                        const std::size_t s = static_cast<std::size_t>(mi.slot);
                        if (s < slotToName.size() && slotToName[s].empty())
                            slotToName[s] = mname;
                    }
                    if (cur->baseQualified.empty())
                        break;
                    cur = oopIndex_.findClass(cur->baseQualified);
                }
            }

            for (std::size_t s = 0; s < slotCount; ++s)
            {
                const long long offset = static_cast<long long>(s * 8ULL);
                Value slotPtr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), vtblPtr, Value::constInt(offset));
                const std::string &mname = slotToName[s];
                if (mname.empty())
                {
                    emitStore(Type(Type::Kind::Ptr), slotPtr, Value::null());
                }
                else
                {
                    const std::string implQ = findImplementorQClass(ciInit->qualifiedName, mname);
                    const std::string target = mangleMethod(implQ, mname);
                    emitStore(Type(Type::Kind::Ptr), slotPtr, Value::global(target));
                }
            }

            // Store the vptr at offset 0 in the object
            emitStore(Type(Type::Kind::Ptr), obj, vtblPtr);
        }
    }

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

    // Only resolve member fields for instance receivers. If the base does not
    // represent an object (e.g., a class name in static access), bail out early
    // so callers can apply property sugar or static-field logic without forcing
    // a load of the base expression (which may not have storage).
    std::string className;
    if (const auto *vbase = as<const VarExpr>(*expr.base))
        className = getSlotType(vbase->name).objectClass;
    if (className.empty())
        className = resolveObjectClass(*expr.base);
    if (className.empty())
        return std::nullopt;

    RVal base = lowerExpr(*expr.base);
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
    const ClassLayout *layout = findClassLayout(className);
    if (!layout)
        return std::nullopt;

    const ClassLayout::Field *field = layout->findField(expr.member);
    if (!field)
        return std::nullopt;

    curLoc = expr.loc;
    Value fieldPtr = emitBinary(Opcode::GEP,
                                Type(Type::Kind::Ptr),
                                base.value,
                                Value::constInt(static_cast<long long>(field->offset)));
    // BUG-082 fix: Object fields are pointers, not I64
    Type fieldTy = !field->objectClassName.empty() ? Type(Type::Kind::Ptr)
                                                    : type_conv::astToIlType(field->type);
    MemberFieldAccess access;
    access.ptr = fieldPtr;
    access.ilType = fieldTy;
    access.astType = field->type;
    access.objectClassName = field->objectClassName;  // BUG-082 fix
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
    // BUG-082 fix: Object fields are pointers, not I64
    access.ilType = !field->objectClassName.empty() ? Type(Type::Kind::Ptr)
                                                     : type_conv::astToIlType(field->type);
    access.astType = field->type;
    access.objectClassName = field->objectClassName;  // BUG-082 fix
    return access;
}

Lowerer::RVal Lowerer::lowerMemberAccessExpr(const MemberAccessExpr &expr)
{
    auto access = resolveMemberField(expr);
    if (!access)
    {
        // Fallbacks:
        // 1) Instance property getter sugar: base.member -> call get_member(base)
        // 2) Static property getter sugar:  Class.member -> call Class.get_member()
        // 3) Static field access:           Class.field  -> load @Class::field

        // 1) Instance property sugar
        if (expr.base)
        {
            std::string instClass = resolveObjectClass(*expr.base);
            if (!instClass.empty())
            {
                std::string qname = qualify(instClass);
                std::string getter = std::string("get_") + expr.member;
                // Overload resolution for property getter (0 user params)
                std::string curClass = currentClass();
                if (auto resolved = sem::resolveMethodOverload(
                        oopIndex_, qname, expr.member, /*isStatic*/ false, /*args*/ {}, curClass,
                        diagnosticEmitter(), expr.loc))
                {
                    getter = resolved->methodName;
                }
                else if (diagnosticEmitter())
                {
                    return {Value::constInt(0), Type(Type::Kind::I64)};
                }
                std::string callee = mangleMethod(qname, getter);
                RVal base = lowerExpr(*expr.base);
                std::vector<Value> args;
                args.push_back(base.value);
                Type retTy = Type(Type::Kind::I64);
                if (auto rt = findMethodReturnType(qname, getter))
                    retTy = type_conv::astToIlType(*rt);
                Value result = (retTy.kind == Type(Type::Kind::Void).kind)
                                   ? (emitCall(callee, args), Value::constInt(0))
                                   : emitCallRet(retTy, callee, args);
                return {result, retTy};
            }

            // 2/3) Static property or static field on a class name
            if (const auto *v = as<const VarExpr>(*expr.base))
            {
                // If a symbol with this name exists (local/param/global), it's not a static access
                if (const auto *sym = findSymbol(v->name); sym && sym->slotId)
                {
                    return {Value::null(), Type(Type::Kind::Ptr)};
                }
                // Attempt to resolve the class by current namespace context
                std::string qname = resolveQualifiedClassCasing(qualify(v->name));
                if (const ClassInfo *ci = oopIndex_.findClass(qname))
                {
                    // Prefer property getter sugar when present (resolve overloads)
                    std::string getter = std::string("get_") + expr.member;
                    if (auto resolved = sem::resolveMethodOverload(oopIndex_, qname, expr.member,
                                                                  /*isStatic*/ true, /*args*/ {},
                                                                  currentClass(), diagnosticEmitter(), expr.loc))
                        getter = resolved->methodName;
                    else if (diagnosticEmitter())
                        return {Value::constInt(0), Type(Type::Kind::I64)};
                    auto it = ci->methods.find(getter);
                    if (it != ci->methods.end() && it->second.isStatic)
                    {
                        Type retTy = Type(Type::Kind::I64);
                        if (auto rt = findMethodReturnType(qname, getter))
                            retTy = type_conv::astToIlType(*rt);
                        std::string callee = mangleMethod(ci->qualifiedName, getter);
                        Value result = (retTy.kind == Type(Type::Kind::Void).kind)
                                           ? (emitCall(callee, {}), Value::constInt(0))
                                           : emitCallRet(retTy, callee, {});
                        return {result, retTy};
                    }

                    // Otherwise, try a static field load
                    for (const auto &sf : ci->staticFields)
                    {
                        if (sf.name == expr.member)
                        {
                            Type ilTy = sf.objectClassName.empty() ? type_conv::astToIlType(sf.type)
                                                                   : Type(Type::Kind::Ptr);
                            curLoc = expr.loc;
                            std::string gname = ci->qualifiedName + "::" + expr.member;
                            Value addr = emitUnary(Opcode::AddrOf, Type(Type::Kind::Ptr), Value::global(gname));
                            Value loaded = emitLoad(ilTy, addr);
                            return {loaded, ilTy};
                        }
                    }
                }
            }
        }

        return {Value::null(), Type(Type::Kind::Ptr)};
    }

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

    // Static method calls: Class.Method(...)
    if (const auto *vb = as<const VarExpr>(*expr.base))
    {
        // If a symbol with this name exists (local/param/global), treat as instance, not static
        if (const auto *sym = findSymbol(vb->name); sym && sym->slotId)
        {
            // fall through to instance path below
        }
        else
        {
        std::string qname = resolveQualifiedClassCasing(qualify(vb->name));
        if (const ClassInfo *ci = oopIndex_.findClass(qname))
        {
            // Overload resolution for static call
            std::vector<::il::frontends::basic::Type> argAstTypes;
            argAstTypes.reserve(expr.args.size());
            for (const auto &a : expr.args)
                argAstTypes.push_back(a ? (scanExpr(*a) == ExprType::F64 ? ::il::frontends::basic::Type::F64
                                                                         : (scanExpr(*a) == ExprType::Str
                                                                                ? ::il::frontends::basic::Type::Str
                                                                                : (scanExpr(*a) == ExprType::Bool
                                                                                       ? ::il::frontends::basic::Type::Bool
                                                                                       : ::il::frontends::basic::Type::I64)))
                                         : ::il::frontends::basic::Type::I64);
            std::string selected = expr.method;
            if (auto resolved = sem::resolveMethodOverload(
                    oopIndex_, qname, expr.method, /*isStatic*/ true, argAstTypes, currentClass(),
                    diagnosticEmitter(), expr.loc))
            {
                selected = resolved->methodName;
            }
            std::vector<::il::frontends::basic::Type> expectParamAst;
            if (auto it = ci->methods.find(selected); it != ci->methods.end())
                expectParamAst = it->second.sig.paramTypes;

            std::vector<Value> args;
            args.reserve(expr.args.size());
            for (std::size_t i = 0; i < expr.args.size(); ++i)
            {
                RVal lowered = lowerExpr(*expr.args[i]);
                if (i < expectParamAst.size())
                {
                    auto astTy = expectParamAst[i];
                    if (astTy == ::il::frontends::basic::Type::Bool)
                        lowered = coerceToBool(std::move(lowered), expr.loc);
                    else if (astTy == ::il::frontends::basic::Type::F64)
                        lowered = coerceToF64(std::move(lowered), expr.loc);
                    else if (astTy == ::il::frontends::basic::Type::I64)
                        lowered = coerceToI64(std::move(lowered), expr.loc);
                }
                args.push_back(lowered.value);
            }

            std::string callee = mangleMethod(ci->qualifiedName, selected);
            if (auto retType = findMethodReturnType(qname, selected))
            {
                Type ilRetTy = type_conv::astToIlType(*retType);
                Value result = emitCallRet(ilRetTy, callee, args);
                if (ilRetTy.kind == Type::Kind::Str)
                    deferReleaseStr(result);
                else if (ilRetTy.kind == Type::Kind::Ptr)
                    deferReleaseObj(result, qname);
                return {result, ilRetTy};
            }
            emitCall(callee, args);
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }
        }
    }

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
    for (std::size_t i = 0; i < expr.args.size(); ++i)
    {
        const auto &arg = expr.args[i];
        if (!arg)
            continue;
        RVal lowered = lowerExpr(*arg);
        args.push_back(lowered.value);
    }

    curLoc = expr.loc;
    const std::string qname = qualify(className);

    // Attempt to coerce arguments to the method's declared parameter types to avoid IL mismatches
    // (e.g., BOOLEAN params expect i1; TRUE/FALSE literals lower as i64 otherwise).
    if (!expr.args.empty())
    {
        std::vector<::il::frontends::basic::Type> expectParamAst;
        if (!qname.empty())
        {
            if (const ClassInfo *ci = oopIndex_.findClass(qname))
            {
                auto it = ci->methods.find(expr.method);
                if (it != ci->methods.end())
                    expectParamAst = it->second.sig.paramTypes;
            }
        }
        if (!expectParamAst.empty())
        {
            // Re-lower/coerce arguments based on expected AST types and rebuild 'args'
            std::vector<Value> coerced;
            coerced.reserve(1 + expr.args.size());
            coerced.push_back(args.front()); // selfArg
            for (std::size_t i = 0; i < expr.args.size(); ++i)
            {
                RVal lowered = lowerExpr(*expr.args[i]);
                if (i < expectParamAst.size())
                {
                    auto astTy = expectParamAst[i];
                    if (astTy == ::il::frontends::basic::Type::Bool)
                        lowered = coerceToBool(std::move(lowered), expr.loc);
                    else if (astTy == ::il::frontends::basic::Type::F64)
                        lowered = coerceToF64(std::move(lowered), expr.loc);
                    else if (astTy == ::il::frontends::basic::Type::I64)
                        lowered = coerceToI64(std::move(lowered), expr.loc);
                }
                coerced.push_back(lowered.value);
            }
            args.swap(coerced);
        }
    }

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

    // Resolve overload to select the best callee among same-name methods.
    // Build argument AST types (excluding implicit self).
    std::vector<::il::frontends::basic::Type> argAstTypes;
    argAstTypes.reserve(expr.args.size());
    for (const auto &a : expr.args)
        argAstTypes.push_back(a ? (scanExpr(*a) == ExprType::F64 ? ::il::frontends::basic::Type::F64
                                                                 : (scanExpr(*a) == ExprType::Str
                                                                        ? ::il::frontends::basic::Type::Str
                                                                        : (scanExpr(*a) == ExprType::Bool
                                                                               ? ::il::frontends::basic::Type::Bool
                                                                               : ::il::frontends::basic::Type::I64)))
                                 : ::il::frontends::basic::Type::I64);

    std::string qc = qname.empty() ? directQClass : qname;
    std::string curClass = currentClass();
    std::string selectedName = expr.method;
    if (!qc.empty())
    {
        if (auto resolved = sem::resolveMethodOverload(oopIndex_, qc, expr.method, false, argAstTypes,
                                                       curClass, diagnosticEmitter(), expr.loc))
        {
            selectedName = resolved->methodName;
        }
        else if (diagnosticEmitter())
        {
            return {Value::constInt(0), Type(Type::Kind::I64)};
        }
    }
    std::string emitClassName = qc;
    if (!qc.empty())
    {
        if (const ClassInfo *ci = oopIndex_.findClass(qc))
            emitClassName = ci->qualifiedName;
    }
    std::string directCallee = emitClassName.empty() ? selectedName : mangleMethod(emitClassName, selectedName);

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
        // Ensure runtime extern is declared for itable lookup
        if (builder)
        {
            if (const auto *desc = il::runtime::findRuntimeDescriptor("rt_itable_lookup"))
                builder->addExtern(std::string(desc->name), desc->signature.retType, desc->signature.paramTypes);
            else
                builder->addExtern("rt_itable_lookup",
                                   Type(Type::Kind::Ptr),
                                   {Type(Type::Kind::Ptr), Type(Type::Kind::I64)});
        }
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
                    type_conv::astToIlType(*iface->slots[static_cast<std::size_t>(slotIndex)].returnType);
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

    // If virtual and not BASE-qualified, attempt dynamic dispatch by reading a per-object
    // method pointer table address from a module-level binding when available. As a
    // conservative fallback, construct the pointer from the object's class slot table by
    // loading the function address at 'slot' from a contiguous array starting at the
    // indirect callee pointer. This preserves correct behaviour for projects that populate
    // per-class tables in module init.
    if (slot >= 0 && !baseQualified)
    {
        // Pointer-based table lookup: treat operand 0 as a pointer to the table base, then GEP.
        // Load the callee-table pointer from the object (projects may store a table pointer
        // at offset 0). If unavailable, this yields null and the indirect call path below
        // will trap with a clear message.
        Value tablePtr = emitLoad(Type(Type::Kind::Ptr), selfArg);
        const long long offset = static_cast<long long>(slot * 8LL);
        Value entryPtr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), tablePtr, Value::constInt(offset));
        Value fnPtr = emitLoad(Type(Type::Kind::Ptr), entryPtr);

        if (auto retType = findMethodReturnType(className, expr.method))
        {
            Type ilRetTy = type_conv::astToIlType(*retType);
            Value result = emitCallIndirectRet(ilRetTy, fnPtr, args);
            if (ilRetTy.kind == Type::Kind::Str)
                deferReleaseStr(result);
            else if (ilRetTy.kind == Type::Kind::Ptr && !className.empty())
                deferReleaseObj(result, className);
            return {result, ilRetTy};
        }
        emitCallIndirect(fnPtr, args);
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Direct call path.
    // For BASE-qualified direct calls, consult the resolved base class for return type.
    const std::string retClassLookup = baseQualified ? directQClass : qc;
    if (auto retType = findMethodReturnType(retClassLookup, selectedName))
    {
        Type ilRetTy = type_conv::astToIlType(*retType);
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
