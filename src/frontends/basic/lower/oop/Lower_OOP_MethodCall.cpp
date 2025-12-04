//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/oop/Lower_OOP_MethodCall.cpp
// Purpose: Lower BASIC OOP method calls and virtual dispatch operations.
// Key invariants: Method calls use vtable for virtual dispatch; property
//                 accessors follow get_/set_ naming conventions.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/ILTypeUtils.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/OopLoweringContext.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "frontends/basic/lower/oop/Lower_OOP_Internal.hpp"
#include "frontends/basic/sem/OverloadResolution.hpp"
#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <string>
#include <vector>

namespace il::frontends::basic
{

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
        // If a symbol with this name exists (local/param/global), treat as instance, not static.
        // Module-level variables do not have slots; rely on symbol presence alone.
        if (const auto *sym = findSymbol(vb->name); sym)
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
                    argAstTypes.push_back(
                        a ? (scanExpr(*a) == ExprType::F64
                                 ? ::il::frontends::basic::Type::F64
                                 : (scanExpr(*a) == ExprType::Str
                                        ? ::il::frontends::basic::Type::Str
                                        : (scanExpr(*a) == ExprType::Bool
                                               ? ::il::frontends::basic::Type::Bool
                                               : ::il::frontends::basic::Type::I64)))
                          : ::il::frontends::basic::Type::I64);
                std::string selected = expr.method;
                if (auto resolved = sem::resolveMethodOverload(oopIndex_,
                                                               qname,
                                                               expr.method,
                                                               /*isStatic*/ true,
                                                               argAstTypes,
                                                               currentClass(),
                                                               diagnosticEmitter(),
                                                               expr.loc))
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
                // BUG-CARDS-010 fix: Check for object-returning methods first
                std::string retClassName = findMethodReturnClassName(qname, selected);
                if (!retClassName.empty())
                {
                    // Method returns a custom class type - use ptr
                    Type ilRetTy(Type::Kind::Ptr);
                    Value result = emitCallRet(ilRetTy, callee, args);
                    deferReleaseObj(result, retClassName);
                    return {result, ilRetTy};
                }
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
            else
            {
                // Static call on a runtime class from the catalog (no receiver)
                auto isRuntimeClass = [&](const std::string &qn)
                {
                    const auto &rc = il::runtime::runtimeClassCatalog();
                    for (const auto &c : rc)
                        if (string_utils::iequals(qn, c.qname))
                            return true;
                    return false;
                };
                if (isRuntimeClass(qname))
                {
                    auto &midx = runtimeMethodIndex();
                    auto info = midx.find(qname, expr.method, expr.args.size());
                    if (!info)
                    {
                        if (auto *em = diagnosticEmitter())
                        {
                            auto cands = midx.candidates(qname, expr.method);
                            std::string msg =
                                "no such method '" + expr.method + "' on '" + qname + "'";
                            if (!cands.empty())
                            {
                                msg += "; candidates: ";
                                for (size_t i = 0; i < cands.size(); ++i)
                                {
                                    if (i)
                                        msg += ", ";
                                    msg += cands[i];
                                }
                            }
                            em->emit(il::support::Severity::Error,
                                     "E_NO_SUCH_METHOD",
                                     expr.loc,
                                     static_cast<uint32_t>(expr.method.size()),
                                     std::move(msg));
                        }
                        return {Value::constInt(0), Type(Type::Kind::I64)};
                    }
                    std::vector<Value> args;
                    args.reserve(expr.args.size());
                    for (const auto &a : expr.args)
                    {
                        RVal av = lowerExpr(*a);
                        args.push_back(av.value);
                    }
                    auto mapBasicToIl = [](BasicType t) -> Type::Kind
                    {
                        switch (t)
                        {
                            case BasicType::String:
                                return Type::Kind::Str;
                            case BasicType::Float:
                                return Type::Kind::F64;
                            case BasicType::Bool:
                                return Type::Kind::I1;
                            case BasicType::Void:
                                return Type::Kind::Void;
                            case BasicType::Object:
                                return Type::Kind::Ptr;
                            case BasicType::Int:
                            case BasicType::Unknown:
                            default:
                                return Type::Kind::I64;
                        }
                    };
                    Type retTy(mapBasicToIl(info->ret));
                    runtimeTracker.trackCalleeName(info->target);
                    curLoc = expr.loc;
                    Value result = retTy.kind == Type::Kind::Void
                                       ? (emitCall(info->target, args), Value::constInt(0))
                                       : emitCallRet(retTy, info->target, args);
                    if (retTy.kind == Type::Kind::Str)
                        deferReleaseStr(result);
                    return {result, retTy.kind == Type::Kind::Void ? Type(Type::Kind::I64) : retTy};
                }
            }
        }
    }

    // Runtime class method calls via catalog (e.g., Viper.String)
    {
        // Determine runtime class qname
        std::string qClass;
        {
            std::string cls = resolveObjectClass(*expr.base);
            if (!cls.empty())
                qClass = qualify(cls);
        }
        if (qClass.empty())
        {
            RVal baseProbe = lowerExpr(*expr.base);
            if (baseProbe.type.kind == Type::Kind::Str)
                qClass = "Viper.String";
        }
        // Only consult the runtime method catalog for true runtime classes
        auto isRuntimeClass = [&](const std::string &qn)
        {
            const auto &rc = il::runtime::runtimeClassCatalog();
            for (const auto &c : rc)
                if (string_utils::iequals(qn, c.qname))
                    return true;
            return false;
        };
        if (!qClass.empty() && isRuntimeClass(qClass))
        {
            auto &midx = runtimeMethodIndex();
            auto info = midx.find(qClass, expr.method, expr.args.size());
            if (!info)
            {
                if (auto *em = diagnosticEmitter())
                {
                    auto cands = midx.candidates(qClass, expr.method);
                    std::string msg = "no such method '" + expr.method + "' on '" + qClass + "'";
                    if (!cands.empty())
                    {
                        msg += "; candidates: ";
                        for (size_t i = 0; i < cands.size(); ++i)
                        {
                            if (i)
                                msg += ", ";
                            msg += cands[i];
                        }
                    }
                    em->emit(il::support::Severity::Error,
                             "E_NO_SUCH_METHOD",
                             expr.loc,
                             static_cast<uint32_t>(expr.method.size()),
                             std::move(msg));
                }
                return {Value::constInt(0), Type(Type::Kind::I64)};
            }
            // Lower base and build (receiver, args...)
            RVal base = lowerExpr(*expr.base);
            std::vector<Value> args;
            args.reserve(1 + expr.args.size());
            args.push_back(base.value);

            auto mapBasicToIl = [](BasicType t) -> Type::Kind
            {
                switch (t)
                {
                    case BasicType::String:
                        return Type::Kind::Str;
                    case BasicType::Float:
                        return Type::Kind::F64;
                    case BasicType::Bool:
                        return Type::Kind::I1;
                    case BasicType::Void:
                        return Type::Kind::Void;
                    case BasicType::Object:
                        return Type::Kind::Ptr;
                    case BasicType::Int:
                    case BasicType::Unknown:
                    default:
                        return Type::Kind::I64;
                }
            };
            // Coerce each user arg to expected BasicType
            for (size_t i = 0; i < expr.args.size(); ++i)
            {
                RVal av = lowerExpr(*expr.args[i]);
                BasicType expect = (i < info->args.size()) ? info->args[i] : BasicType::Int;
                if (expect == BasicType::Bool)
                    av = coerceToBool(std::move(av), expr.loc);
                else if (expect == BasicType::Float)
                    av = coerceToF64(std::move(av), expr.loc);
                else if (expect == BasicType::Int)
                    av = coerceToI64(std::move(av), expr.loc);
                args.push_back(av.value);
            }
            // Declare extern with receiver + arg types
            std::vector<Type> paramTypes;
            paramTypes.reserve(1 + info->args.size());
            // Receiver: strings use str; others default to ptr
            paramTypes.push_back(qClass == "Viper.String" ? Type(Type::Kind::Str)
                                                          : Type(Type::Kind::Ptr));
            for (BasicType bt : info->args)
                paramTypes.push_back(Type(mapBasicToIl(bt)));

            Type retTy(mapBasicToIl(info->ret));
            // Record the catalog target spelling (e.g., Viper.String.Substring)
            // so extern declarations can include the accessor alongside
            // canonical function names selected at call sites.
            runtimeTracker.trackCalleeName(info->target);
            curLoc = expr.loc;
            Value result = retTy.kind == Type::Kind::Void
                               ? (emitCall(info->target, args), Value::constInt(0))
                               : emitCallRet(retTy, info->target, args);
            if (retTy.kind == Type::Kind::Str)
                deferReleaseStr(result);
            return {result, retTy.kind == Type::Kind::Void ? Type(Type::Kind::I64) : retTy};
        }

        // Fallback: Object methods on any instance (Viper.Object.*, System alias supported)
        // BUT only if the user-defined class doesn't override the method.
        {
            // First check if the user-defined class has this method - if so, skip the
            // Viper.Object fallback and let the user-defined method handling below take over.
            bool userClassHasMethod = false;
            if (!qClass.empty())
            {
                if (oopIndex_.findMethodInHierarchy(qClass, expr.method))
                    userClassHasMethod = true;
            }

            auto &midx = runtimeMethodIndex();
            auto info = midx.find("Viper.Object", expr.method, expr.args.size());
            if (!info)
                info = midx.find("Viper.System.Object", expr.method, expr.args.size());
            if (info && !userClassHasMethod)
            {
                // Lower base and build (receiver, args...)
                RVal base = lowerExpr(*expr.base);
                std::vector<Value> args;
                args.reserve(1 + expr.args.size());
                args.push_back(base.value);
                for (size_t i = 0; i < expr.args.size(); ++i)
                {
                    RVal av = lowerExpr(*expr.args[i]);
                    args.push_back(av.value);
                }
                // Receiver is ptr; args are passed as-is; ret type from info
                auto mapBasicToIl = [](BasicType t) -> Type::Kind
                {
                    switch (t)
                    {
                        case BasicType::String:
                            return Type::Kind::Str;
                        case BasicType::Float:
                            return Type::Kind::F64;
                        case BasicType::Bool:
                            return Type::Kind::I1;
                        case BasicType::Void:
                            return Type::Kind::Void;
                        case BasicType::Object:
                            return Type::Kind::Ptr;
                        case BasicType::Int:
                        case BasicType::Unknown:
                        default:
                            return Type::Kind::I64;
                    }
                };
                Type retTy(mapBasicToIl(info->ret));
                runtimeTracker.trackCalleeName(info->target);
                curLoc = expr.loc;
                Value result = retTy.kind == Type::Kind::Void
                                   ? (emitCall(info->target, args), Value::constInt(0))
                                   : emitCallRet(retTy, info->target, args);
                if (retTy.kind == Type::Kind::Str)
                    deferReleaseStr(result);
                return {result, retTy.kind == Type::Kind::Void ? Type(Type::Kind::I64) : retTy};
            }
            // As a last resort, special-case common Object methods to canonical targets
            // (only if user class doesn't override)
            if (!userClassHasMethod && string_utils::iequals(expr.method, "ToString") &&
                expr.args.size() == 0)
            {
                curLoc = expr.loc;
                RVal base = lowerExpr(*expr.base);
                runtimeTracker.trackCalleeName("Viper.Object.ToString");
                Value result =
                    emitCallRet(Type(Type::Kind::Str), "Viper.Object.ToString", {base.value});
                deferReleaseStr(result);
                return {result, Type(Type::Kind::Str)};
            }
            if (!userClassHasMethod && string_utils::iequals(expr.method, "Equals") &&
                expr.args.size() == 1)
            {
                curLoc = expr.loc;
                RVal base = lowerExpr(*expr.base);
                RVal rhs = lowerExpr(*expr.args[0]);
                runtimeTracker.trackCalleeName("Viper.Object.Equals");
                Value result = emitCallRet(
                    Type(Type::Kind::I1), "Viper.Object.Equals", {base.value, rhs.value});
                return {result, Type(Type::Kind::I1)};
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
        argAstTypes.push_back(
            a ? (scanExpr(*a) == ExprType::F64
                     ? ::il::frontends::basic::Type::F64
                     : (scanExpr(*a) == ExprType::Str
                            ? ::il::frontends::basic::Type::Str
                            : (scanExpr(*a) == ExprType::Bool ? ::il::frontends::basic::Type::Bool
                                                              : ::il::frontends::basic::Type::I64)))
              : ::il::frontends::basic::Type::I64);

    std::string qc = qname.empty() ? directQClass : qname;
    std::string curClass = currentClass();
    std::string selectedName = expr.method;
    if (!qc.empty())
    {
        if (auto resolved = sem::resolveMethodOverload(oopIndex_,
                                                       qc,
                                                       expr.method,
                                                       false,
                                                       argAstTypes,
                                                       curClass,
                                                       diagnosticEmitter(),
                                                       expr.loc))
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
    std::string directCallee =
        emitClassName.empty() ? selectedName : mangleMethod(emitClassName, selectedName);

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
                builder->addExtern(
                    std::string(desc->name), desc->signature.retType, desc->signature.paramTypes);
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
                retTy = type_conv::astToIlType(
                    *iface->slots[static_cast<std::size_t>(slotIndex)].returnType);
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
        Value entryPtr =
            emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), tablePtr, Value::constInt(offset));
        Value fnPtr = emitLoad(Type(Type::Kind::Ptr), entryPtr);

        // BUG-CARDS-010 fix: Check for object-returning methods first
        std::string retClassName = findMethodReturnClassName(className, expr.method);
        if (!retClassName.empty())
        {
            Type ilRetTy(Type::Kind::Ptr);
            Value result = emitCallIndirectRet(ilRetTy, fnPtr, args);
            deferReleaseObj(result, retClassName);
            return {result, ilRetTy};
        }
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
    // BUG-CARDS-010 fix: Check for object-returning methods first
    std::string retClassName = findMethodReturnClassName(retClassLookup, selectedName);
    if (!retClassName.empty())
    {
        Type ilRetTy(Type::Kind::Ptr);
        Value result = emitCallRet(ilRetTy, directCallee, args);
        deferReleaseObj(result, retClassName);
        return {result, ilRetTy};
    }
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

// -------------------------------------------------------------------------
// OopLoweringContext-aware implementations
// -------------------------------------------------------------------------

Lowerer::RVal Lowerer::lowerMethodCallExpr(const MethodCallExpr &expr, OopLoweringContext &ctx)
{
    // Pre-cache class info for method dispatch target.
    // This accelerates access control and overload resolution.
    if (expr.base)
    {
        std::string cls = resolveObjectClass(*expr.base);
        if (!cls.empty())
            (void)ctx.findClassInfo(qualify(cls));
    }
    return lowerMethodCallExpr(expr);
}

} // namespace il::frontends::basic
