//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/SemanticAnalyzer_Class.cpp
// Purpose: Class and interface semantic checks.
// Key invariants: Two-pass analysis; error recovery returns Unknown type.
// Ownership/Lifetime: Borrows DiagnosticEngine; AST not owned.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/common/CharUtils.hpp"
#include "frontends/pascal/BuiltinRegistry.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace il::frontends::pascal
{

// Use common toLowercase for case-insensitive comparison
using common::char_utils::toLowercase;

// Alias for compatibility with existing code
inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// Class/Interface Semantic Checks
//===----------------------------------------------------------------------===//

void SemanticAnalyzer::checkClassSemantics()
{
    for (auto &pair : classes_)
    {
        checkClassInfo(pair.second);
        // Compute abstractness: set ClassInfo.isAbstract flag
        pair.second.isAbstract = isAbstractClass(pair.second.name);
    }
}

void SemanticAnalyzer::checkClassInfo(const ClassInfo &classInfo)
{
    // Collect the effective base class and interfaces
    // The parser puts the first heritage item in baseClass, rest in interfaces
    // But if baseClass is actually an interface, treat it as such
    std::string effectiveBaseClass;
    std::vector<std::string> effectiveInterfaces = classInfo.interfaces;

    if (!classInfo.baseClass.empty())
    {
        std::string baseKey = toLower(classInfo.baseClass);
        if (classes_.find(baseKey) != classes_.end())
        {
            // It's a class - use as base class
            effectiveBaseClass = classInfo.baseClass;
        }
        else if (interfaces_.find(baseKey) != interfaces_.end())
        {
            // It's an interface - add to interfaces list
            effectiveInterfaces.insert(effectiveInterfaces.begin(), classInfo.baseClass);
        }
        else
        {
            error(classInfo.loc, "unknown type '" + classInfo.baseClass + "' in heritage clause");
        }
    }

    // Check for multiple base classes (heritage list with >1 class)
    // We need to verify interfaces list items are actually interfaces, not classes
    for (const auto &ifaceName : effectiveInterfaces)
    {
        std::string key = toLower(ifaceName);
        // Check if it's a class (error) or interface (ok)
        if (classes_.find(key) != classes_.end())
        {
            error(classInfo.loc,
                  "at most one base class permitted; '" + ifaceName +
                      "' is a class, not an interface");
        }
        else if (interfaces_.find(key) == interfaces_.end())
        {
            error(classInfo.loc, "unknown interface '" + ifaceName + "'");
        }
    }

    // Check method overrides using effective base class
    checkOverridesWithBase(classInfo, effectiveBaseClass);

    // Check interface implementation using effective interfaces
    checkInterfaceImplementationWith(classInfo, effectiveInterfaces);

    // Check weak fields
    checkWeakFields(classInfo);
}

void SemanticAnalyzer::checkOverrides(const ClassInfo &classInfo)
{
    checkOverridesWithBase(classInfo, classInfo.baseClass);
}

void SemanticAnalyzer::checkOverridesWithBase(const ClassInfo &classInfo,
                                              const std::string &effectiveBaseClass)
{
    for (const auto &[methodKey, method] : classInfo.methods)
    {
        if (method.isOverride)
        {
            // Must find a virtual method in base class hierarchy
            auto baseMethod = findVirtualInBase(effectiveBaseClass, method.name);
            if (!baseMethod)
            {
                error(method.loc,
                      "method '" + method.name +
                          "' marked override but no virtual method found in base class");
            }
            else if (!signaturesMatch(method, *baseMethod))
            {
                error(method.loc,
                      "override method '" + method.name +
                          "' signature does not match base virtual method");
            }
        }
    }
}

std::optional<MethodInfo> SemanticAnalyzer::findVirtualInBase(const std::string &className,
                                                              const std::string &methodName) const
{
    if (className.empty())
        return std::nullopt;

    std::string classKey = toLower(className);
    auto it = classes_.find(classKey);
    if (it == classes_.end())
        return std::nullopt;

    const ClassInfo &baseClass = it->second;

    // Look for method in this class
    std::string methodKey = toLower(methodName);
    auto methodIt = baseClass.methods.find(methodKey);
    if (methodIt != baseClass.methods.end())
    {
        if (methodIt->second.isVirtual || methodIt->second.isAbstract)
        {
            return methodIt->second;
        }
    }

    // Recurse to parent
    return findVirtualInBase(baseClass.baseClass, methodName);
}

bool SemanticAnalyzer::signaturesMatch(const MethodInfo &m1, const MethodInfo &m2) const
{
    // Return types must match
    if (m1.returnType.kind != m2.returnType.kind)
        return false;

    // Parameter count must match
    if (m1.params.size() != m2.params.size())
        return false;

    // Each parameter type and var/out must match
    for (size_t i = 0; i < m1.params.size(); ++i)
    {
        if (m1.params[i].second.kind != m2.params[i].second.kind)
            return false;
        if (m1.isVarParam[i] != m2.isVarParam[i])
            return false;
    }

    return true;
}

void SemanticAnalyzer::checkInterfaceImplementation(const ClassInfo &classInfo)
{
    checkInterfaceImplementationWith(classInfo, classInfo.interfaces);
}

void SemanticAnalyzer::checkInterfaceImplementationWith(
    const ClassInfo &classInfo, const std::vector<std::string> &effectiveInterfaces)
{
    // Collect all methods required by interfaces
    std::map<std::string, MethodInfo> requiredMethods;
    for (const auto &ifaceName : effectiveInterfaces)
    {
        collectInterfaceMethods(ifaceName, requiredMethods);
    }

    // Check each required method is implemented
    for (const auto &[methodKey, ifaceMethod] : requiredMethods)
    {
        // Look in this class
        auto classMethodIt = classInfo.methods.find(methodKey);
        if (classMethodIt != classInfo.methods.end())
        {
            // Check signature
            if (!signaturesMatch(classMethodIt->second, ifaceMethod))
            {
                error(classInfo.loc,
                      "method '" + ifaceMethod.name + "' signature does not match interface");
            }
            continue;
        }

        // Look in base class hierarchy
        bool found = false;
        std::string baseClass = classInfo.baseClass;
        while (!baseClass.empty() && !found)
        {
            std::string baseKey = toLower(baseClass);
            auto baseIt = classes_.find(baseKey);
            if (baseIt == classes_.end())
                break;

            auto baseMethodIt = baseIt->second.methods.find(methodKey);
            if (baseMethodIt != baseIt->second.methods.end())
            {
                if (signaturesMatch(baseMethodIt->second, ifaceMethod))
                {
                    found = true;
                }
                break;
            }
            baseClass = baseIt->second.baseClass;
        }

        if (!found)
        {
            error(classInfo.loc,
                  "class '" + classInfo.name + "' does not implement interface method '" +
                      ifaceMethod.name + "'");
        }
    }
}

void SemanticAnalyzer::collectInterfaceMethods(const std::string &ifaceName,
                                               std::map<std::string, MethodInfo> &methods) const
{
    std::string key = toLower(ifaceName);
    auto it = interfaces_.find(key);
    if (it == interfaces_.end())
        return;

    const InterfaceInfo &iface = it->second;

    // Add this interface's methods
    for (const auto &[methodKey, method] : iface.methods)
    {
        methods[methodKey] = method;
    }

    // Recurse to base interfaces
    for (const auto &baseIface : iface.baseInterfaces)
    {
        collectInterfaceMethods(baseIface, methods);
    }
}

void SemanticAnalyzer::checkWeakFields(const ClassInfo &classInfo)
{
    for (const auto &[fieldKey, field] : classInfo.fields)
    {
        if (field.isWeak)
        {
            // Check that the field type is a class or interface (or optional of such)
            PasType fieldType = field.type;
            if (fieldType.isOptional() && fieldType.innerType)
            {
                fieldType = *fieldType.innerType;
            }

            if (fieldType.kind != PasTypeKind::Class && fieldType.kind != PasTypeKind::Interface)
            {
                error(field.loc,
                      "weak may only be applied to class/interface fields, not " +
                          field.type.toString());
            }
        }
    }
}

bool SemanticAnalyzer::classImplementsInterface(const std::string &className,
                                                const std::string &interfaceName) const
{
    if (className.empty() || interfaceName.empty())
        return false;

    std::string classKey = toLower(className);
    std::string ifaceKey = toLower(interfaceName);

    auto classIt = classes_.find(classKey);
    if (classIt == classes_.end())
        return false;

    const ClassInfo &classInfo = classIt->second;

    // Check directly implemented interfaces
    for (const auto &implIface : classInfo.interfaces)
    {
        if (toLower(implIface) == ifaceKey)
            return true;

        // Check if implemented interface extends target interface
        if (interfaceExtendsInterface(implIface, interfaceName))
            return true;
    }

    // Also check if "baseClass" is actually an interface (parser puts first parent there)
    if (!classInfo.baseClass.empty())
    {
        std::string baseKey = toLower(classInfo.baseClass);
        if (interfaces_.find(baseKey) != interfaces_.end())
        {
            if (baseKey == ifaceKey ||
                interfaceExtendsInterface(classInfo.baseClass, interfaceName))
                return true;
        }
    }

    // Recurse to base class
    if (!classInfo.baseClass.empty())
    {
        std::string baseKey = toLower(classInfo.baseClass);
        if (classes_.find(baseKey) != classes_.end())
        {
            return classImplementsInterface(classInfo.baseClass, interfaceName);
        }
    }

    return false;
}

bool SemanticAnalyzer::classInheritsFrom(const std::string &derivedName,
                                         const std::string &baseName) const
{
    if (derivedName.empty() || baseName.empty())
        return false;

    // Same class (case-insensitive)
    if (toLower(derivedName) == toLower(baseName))
        return true;

    std::string derivedKey = toLower(derivedName);
    auto it = classes_.find(derivedKey);
    if (it == classes_.end())
        return false;

    const ClassInfo &classInfo = it->second;

    // Recurse to base class
    if (!classInfo.baseClass.empty())
    {
        return classInheritsFrom(classInfo.baseClass, baseName);
    }

    return false;
}

bool SemanticAnalyzer::isAbstractClass(const std::string &className) const
{
    if (className.empty())
        return false;
    std::string key = toLower(className);
    auto it = classes_.find(key);
    if (it == classes_.end())
        return false;

    const ClassInfo &cls = it->second;

    // If this class declares any abstract method, it's abstract
    for (const auto &kv : cls.methods)
    {
        if (kv.second.isAbstract)
            return true;
    }

    // Collect inherited abstract methods
    std::map<std::string, MethodInfo> inheritedAbstract;
    std::string base = cls.baseClass;
    while (!base.empty())
    {
        std::string bkey = toLower(base);
        auto bit = classes_.find(bkey);
        if (bit == classes_.end())
            break;
        const ClassInfo &baseInfo = bit->second;
        for (const auto &mk : baseInfo.methods)
        {
            const MethodInfo &m = mk.second;
            if (m.isAbstract)
            {
                inheritedAbstract[mk.first] = m;
            }
        }
        base = baseInfo.baseClass;
    }

    // Remove any methods overridden concretely by this class
    for (const auto &mk : cls.methods)
    {
        const MethodInfo &m = mk.second;
        if (!m.isAbstract)
        {
            inheritedAbstract.erase(mk.first);
        }
    }

    // If any inherited abstract remains, class is abstract
    return !inheritedAbstract.empty();
}

bool SemanticAnalyzer::isMemberVisible(Visibility visibility, const std::string &declaringClass,
                                       const std::string &accessingClass) const
{
    // Public members are always visible
    if (visibility == Visibility::Public)
        return true;

    // Private members are only visible within the declaring class
    if (accessingClass.empty())
        return false;

    // Case-insensitive comparison
    return toLower(declaringClass) == toLower(accessingClass);
}

bool SemanticAnalyzer::interfaceExtendsInterface(const std::string &derivedName,
                                                 const std::string &baseName) const
{
    if (derivedName.empty() || baseName.empty())
        return false;

    // Same interface (case-insensitive)
    if (toLower(derivedName) == toLower(baseName))
        return true;

    std::string derivedKey = toLower(derivedName);
    auto it = interfaces_.find(derivedKey);
    if (it == interfaces_.end())
        return false;

    const InterfaceInfo &ifaceInfo = it->second;

    // Check base interfaces
    for (const auto &baseIface : ifaceInfo.baseInterfaces)
    {
        if (interfaceExtendsInterface(baseIface, baseName))
            return true;
    }

    return false;
}

const ClassInfo *SemanticAnalyzer::lookupClass(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = classes_.find(key);
    if (it != classes_.end())
        return &it->second;
    return nullptr;
}

const InterfaceInfo *SemanticAnalyzer::lookupInterface(const std::string &name) const
{
    std::string key = toLower(name);
    auto it = interfaces_.find(key);
    if (it != interfaces_.end())
        return &it->second;
    return nullptr;
}


} // namespace il::frontends::pascal
