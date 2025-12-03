//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/OopLoweringContext.cpp
// Purpose: Implementation of OOP lowering context methods.
// Key invariants: All lookups delegate to Lowerer/OopIndex with local caching.
//===----------------------------------------------------------------------===//

#include "frontends/basic/OopLoweringContext.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/OopIndex.hpp"

namespace il::frontends::basic
{

// =============================================================================
// Class Metadata Lookups
// =============================================================================

const ClassInfo *OopLoweringContext::findClassInfo(const std::string &className)
{
    // Check cache first
    auto it = classCache.find(className);
    if (it != classCache.end())
        return it->second;

    // Look up in OOP index and cache result
    const ClassInfo *info = oopIndex.findClass(className);
    classCache[className] = info;
    return info;
}

const ClassLayout *OopLoweringContext::findClassLayout(const std::string &className)
{
    // Check cache first
    auto it = layoutCache.find(className);
    if (it != layoutCache.end())
        return it->second;

    // Delegate to Lowerer and cache result
    const ClassLayout *layout = lowerer.findClassLayout(className);
    layoutCache[className] = layout;
    return layout;
}

// =============================================================================
// Object Class Resolution
// =============================================================================

std::string OopLoweringContext::resolveObjectClass(const Expr &expr) const
{
    return lowerer.resolveObjectClass(expr);
}

// =============================================================================
// Name Mangling Helpers
// =============================================================================

std::string OopLoweringContext::getDestructorName(const std::string &className) const
{
    return mangleClassDtor(className);
}

std::string OopLoweringContext::getConstructorName(const std::string &className) const
{
    return mangleClassCtor(className);
}

std::string OopLoweringContext::getMethodName(const std::string &className,
                                              const std::string &methodName) const
{
    return mangleMethod(className, methodName);
}

// =============================================================================
// Namespace Utilities
// =============================================================================

std::string OopLoweringContext::qualify(const std::string &className) const
{
    return lowerer.qualify(className);
}

} // namespace il::frontends::basic