//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/OopLoweringContext.cpp
// Purpose: Implementation of OOP lowering context methods.
//===----------------------------------------------------------------------===//

#include "frontends/basic/OopLoweringContext.hpp"
#include "frontends/basic/OopIndex.hpp"

namespace il::frontends::basic
{

const ClassInfo* OopLoweringContext::findClassInfo(const std::string &className)
{
    // Check cache first
    auto it = classCache.find(className);
    if (it != classCache.end())
    {
        return it->second;
    }

    // Look up in OOP index
    const ClassInfo* info = oopIndex.findClass(className);
    classCache[className] = info;
    return info;
}

} // namespace il::frontends::basic