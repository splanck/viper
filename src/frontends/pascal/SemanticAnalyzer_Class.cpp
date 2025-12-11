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
            error(classInfo.loc,
                  "class '" + classInfo.name + "' has unknown base type '" + classInfo.baseClass +
                      "'; ensure the base class or interface is declared before this class");
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
                  "class '" + classInfo.name + "' cannot inherit from multiple classes; '" +
                      ifaceName + "' is a class, not an interface; "
                                  "Pascal supports single class inheritance only");
        }
        else if (interfaces_.find(key) == interfaces_.end())
        {
            error(classInfo.loc,
                  "class '" + classInfo.name + "' references unknown interface '" + ifaceName +
                      "'; ensure the interface is declared before this class");
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
    for (const auto &[methodKey, overloads] : classInfo.methods)
    {
        for (const MethodInfo &method : overloads)
        {
            if (method.isOverride)
            {
                // Must find a virtual method in base class hierarchy with matching signature
                auto baseMethod = findVirtualInBaseWithSignature(effectiveBaseClass, method);
                if (!baseMethod)
                {
                    error(method.loc,
                          "method '" + classInfo.name + "." + method.name +
                              "' is marked 'override' but no matching virtual method exists in "
                              "base class hierarchy; declare base method as 'virtual' first");
                }
                else if (!signaturesMatch(method, *baseMethod))
                {
                    error(method.loc,
                          "override method '" + classInfo.name + "." + method.name +
                              "' has incompatible signature with base virtual method; "
                              "parameter types and return type must match exactly");
                }
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

    // Look for method in this class (returns first virtual overload)
    std::string methodKey = toLower(methodName);
    auto methodIt = baseClass.methods.find(methodKey);
    if (methodIt != baseClass.methods.end())
    {
        for (const auto &method : methodIt->second)
        {
            if (method.isVirtual || method.isAbstract)
            {
                return method;
            }
        }
    }

    // Recurse to parent
    return findVirtualInBase(baseClass.baseClass, methodName);
}

std::optional<MethodInfo> SemanticAnalyzer::findVirtualInBaseWithSignature(
    const std::string &className, const MethodInfo &targetMethod) const
{
    if (className.empty())
        return std::nullopt;

    std::string classKey = toLower(className);
    auto it = classes_.find(classKey);
    if (it == classes_.end())
        return std::nullopt;

    const ClassInfo &baseClass = it->second;

    // Look for method with matching signature in this class
    std::string methodKey = toLower(targetMethod.name);
    auto methodIt = baseClass.methods.find(methodKey);
    if (methodIt != baseClass.methods.end())
    {
        for (const auto &method : methodIt->second)
        {
            if ((method.isVirtual || method.isAbstract) &&
                parameterTypesMatch(targetMethod, method))
            {
                return method;
            }
        }
    }

    // Recurse to parent
    return findVirtualInBaseWithSignature(baseClass.baseClass, targetMethod);
}

bool SemanticAnalyzer::parameterTypesMatch(const MethodInfo &m1, const MethodInfo &m2) const
{
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
    // Collect all methods required by interfaces (as a flat list of all overloads)
    std::vector<MethodInfo> requiredMethods;
    for (const auto &ifaceName : effectiveInterfaces)
    {
        collectInterfaceMethods(ifaceName, requiredMethods);
    }

    // Check each required method is implemented with matching signature
    for (const MethodInfo &ifaceMethod : requiredMethods)
    {
        std::string methodKey = toLower(ifaceMethod.name);

        // Helper to find a method with matching signature in a method map
        auto findMatchingMethod =
            [this](const std::map<std::string, std::vector<MethodInfo>> &methods,
                   const std::string &key,
                   const MethodInfo &target) -> bool
        {
            auto it = methods.find(key);
            if (it == methods.end())
                return false;
            for (const auto &m : it->second)
            {
                if (signaturesMatch(m, target))
                    return true;
            }
            return false;
        };

        // Look in this class
        if (findMatchingMethod(classInfo.methods, methodKey, ifaceMethod))
            continue;

        // Look in base class hierarchy
        bool found = false;
        std::string baseClass = classInfo.baseClass;
        while (!baseClass.empty() && !found)
        {
            std::string baseKey = toLower(baseClass);
            auto baseIt = classes_.find(baseKey);
            if (baseIt == classes_.end())
                break;

            if (findMatchingMethod(baseIt->second.methods, methodKey, ifaceMethod))
            {
                found = true;
                break;
            }
            baseClass = baseIt->second.baseClass;
        }

        if (!found)
        {
            error(classInfo.loc,
                  "class '" + classInfo.name + "' must implement interface method '" +
                      ifaceMethod.name + "' with matching signature; "
                                         "add 'procedure " +
                      ifaceMethod.name + "' or 'function " + ifaceMethod.name + "' to the class");
        }
    }
}

void SemanticAnalyzer::collectInterfaceMethods(const std::string &ifaceName,
                                               std::vector<MethodInfo> &methods) const
{
    std::string key = toLower(ifaceName);
    auto it = interfaces_.find(key);
    if (it == interfaces_.end())
        return;

    const InterfaceInfo &iface = it->second;

    // Add this interface's methods (all overloads)
    for (const auto &[methodKey, overloads] : iface.methods)
    {
        for (const auto &method : overloads)
        {
            methods.push_back(method);
        }
    }

    // Recurse to base interfaces
    for (const auto &baseIface : iface.baseInterfaces)
    {
        collectInterfaceMethods(baseIface, methods);
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

    // Add this interface's methods (first overload per name for backwards compatibility)
    for (const auto &[methodKey, overloads] : iface.methods)
    {
        if (!overloads.empty() && methods.find(methodKey) == methods.end())
        {
            methods[methodKey] = overloads.front();
        }
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
                      "'weak' attribute on field '" + field.name +
                          "' is invalid; 'weak' can only be applied to class or interface "
                          "references, not '" +
                          field.type.toString() + "'");
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
    for (const auto &[methodKey, overloads] : cls.methods)
    {
        for (const auto &method : overloads)
        {
            if (method.isAbstract)
                return true;
        }
    }

    // Collect inherited abstract methods (all overloads)
    std::vector<MethodInfo> inheritedAbstract;
    std::string base = cls.baseClass;
    while (!base.empty())
    {
        std::string bkey = toLower(base);
        auto bit = classes_.find(bkey);
        if (bit == classes_.end())
            break;
        const ClassInfo &baseInfo = bit->second;
        for (const auto &[methodKey, overloads] : baseInfo.methods)
        {
            for (const auto &m : overloads)
            {
                if (m.isAbstract)
                {
                    inheritedAbstract.push_back(m);
                }
            }
        }
        base = baseInfo.baseClass;
    }

    // Remove any methods overridden concretely by this class (matching signature)
    for (const auto &[methodKey, overloads] : cls.methods)
    {
        for (const auto &m : overloads)
        {
            if (!m.isAbstract)
            {
                // Remove any inherited abstract with same signature
                inheritedAbstract.erase(std::remove_if(inheritedAbstract.begin(),
                                                       inheritedAbstract.end(),
                                                       [this, &m](const MethodInfo &abs)
                                                       {
                                                           return toLower(abs.name) ==
                                                                      toLower(m.name) &&
                                                                  parameterTypesMatch(abs, m);
                                                       }),
                                        inheritedAbstract.end());
            }
        }
    }

    // If any inherited abstract remains, class is abstract
    return !inheritedAbstract.empty();
}

bool SemanticAnalyzer::isMemberVisible(Visibility visibility,
                                       const std::string &declaringClass,
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

//===----------------------------------------------------------------------===//
// Overload Resolution
//===----------------------------------------------------------------------===//

bool SemanticAnalyzer::argumentsCompatible(const MethodInfo &method,
                                           const std::vector<PasType> &argTypes)
{
    // Check argument count (considering required params and defaults)
    size_t minParams = method.requiredParams;
    size_t maxParams = method.params.size();

    if (argTypes.size() < minParams || argTypes.size() > maxParams)
        return false;

    // Check each argument type is compatible with the corresponding parameter
    for (size_t i = 0; i < argTypes.size(); ++i)
    {
        const PasType &argType = argTypes[i];
        const PasType &paramType = method.params[i].second;

        // Check type compatibility (arg must be assignable to param)
        if (!isAssignableFrom(paramType, argType))
            return false;
    }

    return true;
}

int SemanticAnalyzer::overloadMatchScore(const MethodInfo &method,
                                         const std::vector<PasType> &argTypes)
{
    if (!argumentsCompatible(method, argTypes))
        return -1; // Not compatible

    int score = 0;

    // Score based on exact type matches vs compatible matches
    for (size_t i = 0; i < argTypes.size(); ++i)
    {
        const PasType &argType = argTypes[i];
        const PasType &paramType = method.params[i].second;

        if (argType.kind == paramType.kind)
        {
            // Exact type match
            if (argType.kind == PasTypeKind::Class || argType.kind == PasTypeKind::Interface ||
                argType.kind == PasTypeKind::Record || argType.kind == PasTypeKind::Enum ||
                argType.kind == PasTypeKind::Array)
            {
                // Named types - check if names match exactly
                if (toLower(argType.name) == toLower(paramType.name))
                    score += 10; // Exact match
                else
                    score += 5; // Compatible but not exact (e.g., derived class)
            }
            else
            {
                score += 10; // Exact primitive type match
            }
        }
        else
        {
            // Compatible but different kind (e.g., Integer to Real)
            score += 1;
        }
    }

    // Penalize if using default parameters
    size_t defaultsUsed = method.params.size() - argTypes.size();
    score -= static_cast<int>(defaultsUsed);

    return score;
}

const MethodInfo *SemanticAnalyzer::resolveOverload(const std::vector<MethodInfo> &overloads,
                                                    const std::vector<PasType> &argTypes,
                                                    il::support::SourceLoc loc)
{
    if (overloads.empty())
        return nullptr;

    // If only one overload, use simple compatibility check
    if (overloads.size() == 1)
    {
        if (argumentsCompatible(overloads[0], argTypes))
            return &overloads[0];
        return nullptr;
    }

    // Find all compatible overloads with their scores
    std::vector<std::pair<const MethodInfo *, int>> candidates;
    for (const auto &overload : overloads)
    {
        int score = overloadMatchScore(overload, argTypes);
        if (score >= 0)
            candidates.emplace_back(&overload, score);
    }

    if (candidates.empty())
        return nullptr; // No compatible overload

    if (candidates.size() == 1)
        return candidates[0].first;

    // Find the best score
    int bestScore = candidates[0].second;
    for (const auto &[method, score] : candidates)
    {
        if (score > bestScore)
            bestScore = score;
    }

    // Count how many have the best score
    std::vector<const MethodInfo *> bestMatches;
    for (const auto &[method, score] : candidates)
    {
        if (score == bestScore)
            bestMatches.push_back(method);
    }

    if (bestMatches.size() == 1)
        return bestMatches[0];

    // Ambiguous - multiple overloads with same score
    std::string methodName = overloads[0].name;
    error(loc,
          "ambiguous call to overloaded method '" + methodName + "'; " +
              std::to_string(bestMatches.size()) +
              " overloads match equally well; use explicit type conversions to disambiguate");
    return bestMatches[0]; // Return first to continue analysis
}


} // namespace il::frontends::pascal
