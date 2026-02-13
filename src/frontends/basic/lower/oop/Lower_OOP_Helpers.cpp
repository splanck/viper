//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/oop/Lower_OOP_Helpers.cpp
// Purpose: Shared helper functions for BASIC OOP lowering operations.
// Key invariants: Provides common utilities for type resolution and orchestration.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/IdentifierUtil.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/OopLoweringContext.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "frontends/basic/lower/oop/Lower_OOP_Internal.hpp"
#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <functional>
#include <string>

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

        // Module-level object variables may lack slots; check SymbolInfo directly. (BUG-107)
        if (const auto *info = findSymbol(var->name))
        {
            if (info->isObject && !info->objectClass.empty())
                return info->objectClass;
        }

        // Check module-level scalar object cache (already resolved). (BUG-107)
        auto it = moduleObjectClass_.find(var->name);
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
        // CallExpr may be a field array access (BASIC uses () for both calls and indexing).
        // Check if this is an implicit field array in a class method. (BUG-089)
        const FieldScope *scope = activeFieldScope();
        if (scope && scope->layout)
        {
            const auto *field = scope->layout->findField(call->callee);
            if (field && field->isArray && !field->objectClassName.empty())
            {
                return qualify(field->objectClassName);
            }
        }

        std::string calleeName;
        if (!call->calleeQualified.empty())
            calleeName = JoinDots(call->calleeQualified);
        else
            calleeName = call->callee;
        if (!calleeName.empty())
        {
            const auto &classes = il::runtime::runtimeClassCatalog();
            for (const auto &klass : classes)
            {
                if (!klass.ctor)
                    continue;
                if (string_utils::iequals(calleeName, klass.ctor))
                    return std::string(klass.qname);
            }

            // Check if callee is a static factory method on a runtime class
            // (e.g., Viper.Math.Vec2.Zero → returns Vec2)
            auto lastDot = calleeName.rfind('.');
            if (lastDot != std::string::npos)
            {
                std::string prefix = calleeName.substr(0, lastDot);
                std::string method = calleeName.substr(lastDot + 1);
                if (const auto *rtClass = il::runtime::findRuntimeClassByQName(prefix))
                {
                    // Check class methods for obj return type
                    for (const auto &m : rtClass->methods)
                    {
                        if (m.name && string_utils::iequals(m.name, method) && m.signature)
                        {
                            auto sig = il::runtime::parseRuntimeSignature(m.signature);
                            if (sig.returnType == il::runtime::ILScalarType::Object)
                                return std::string(rtClass->qname);
                            break;
                        }
                    }
                    // Also check standalone RT_FUNCs with this prefix that return obj.
                    // Try various arities to find the method in RuntimeMethodIndex.
                    for (std::size_t ar = 0; ar <= 4; ++ar)
                    {
                        auto entry = runtimeMethodIndex().find(prefix, method, ar);
                        if (entry && entry->ret == BasicType::Object)
                            return prefix;
                    }
                }
            }
        }

        return {};
    }
    if (const auto *arr = as<const ArrayExpr>(expr))
    {
        // Handle module-level, dotted member, and implicit field arrays. (BUG-089)
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

        // Check if this is an implicit field access (e.g., items in a method). (BUG-089)
        const FieldScope *scope = activeFieldScope();
        if (scope && scope->layout)
        {
            const auto *field = scope->layout->findField(arr->name);
            if (field && !field->objectClassName.empty())
            {
                return qualify(field->objectClassName);
            }
        }
        // Module-level arrays referenced inside procedures need their element class
        // recovered from the cached module-level object array map. (BUG-097)
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
        // Check if the FIELD itself is an object type, not the base. (BUG-061)
        if (access->base)
        {
            std::string baseClass = resolveObjectClass(*access->base);
            if (!baseClass.empty())
            {
                // Look up the field class name in the class layout. (BUG-082)
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
        // MethodCallExpr may be a field array access (e.g., container.items(0)).
        // Check this BEFORE checking for method return types. (BUG-096/BUG-098)
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

                // Not a field array; check the method's return type.
                // Use findMethodReturnClassName to get the actual return class. (BUG-099)
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

// -------------------------------------------------------------------------
// Centralized OOP Resolution Helpers
// -------------------------------------------------------------------------
// These helpers consolidate patterns for resolving object class names from
// fields, arrays, and method return types. (BUG-061, BUG-082, BUG-089, etc.)

std::string resolveFieldObjectClass(const ClassLayout *layout,
                                    std::string_view fieldName,
                                    const std::function<std::string(const std::string &)> &qualify)
{
    if (!layout)
        return {};
    const auto *field = layout->findField(fieldName);
    if (!field || field->objectClassName.empty())
        return {};
    return qualify ? qualify(field->objectClassName) : field->objectClassName;
}

std::string resolveFieldArrayElementClass(
    const ClassLayout *layout,
    std::string_view fieldName,
    const std::function<std::string(const std::string &)> &qualify)
{
    if (!layout)
        return {};
    const auto *field = layout->findField(fieldName);
    if (!field || !field->isArray || field->objectClassName.empty())
        return {};
    return qualify ? qualify(field->objectClassName) : field->objectClassName;
}

} // namespace il::frontends::basic
