//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Lower_OOP_Helpers.cpp
// Purpose: Shared helper functions for BASIC OOP lowering operations.
// Key invariants: Provides common utilities for type resolution and orchestration.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lower_OOP_Internal.hpp"
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"

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

// TODO: Move emitOopDeclsAndBodies here from Lower_OOP_Emit.cpp (lines 917-1226)
// This function orchestrates the overall OOP code generation process

} // namespace il::frontends::basic