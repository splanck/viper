//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/OopIndex.cpp
// Purpose: Implementation of the pure OOP data model without AST dependencies.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/OopIndex.hpp"

namespace il::frontends::basic
{

/// @brief Look up a mutable class record by name.
/// @details Searches the internal @c std::unordered_map for the requested class
///          name and returns a pointer to the stored @ref ClassInfo instance
///          when found.  Returning @c nullptr keeps callers explicit about the
///          missing-class case without performing map insertions.
/// @param name Class identifier to locate.
/// @return Pointer to the associated @ref ClassInfo or @c nullptr when absent.
ClassInfo *OopIndex::findClass(const std::string &name)
{
    auto it = classes_.find(name);
    if (it == classes_.end())
    {
        return nullptr;
    }
    return &it->second;
}

/// @brief Look up an immutable class record by name.
/// @details Const-qualified overload used by read-only consumers.  The method
///          performs the same map probe as the mutable variant but preserves
///          const-correctness so callers cannot mutate the stored metadata.
/// @param name Class identifier to locate.
/// @return Pointer to the stored @ref ClassInfo or @c nullptr when absent.
const ClassInfo *OopIndex::findClass(const std::string &name) const
{
    auto it = classes_.find(name);
    if (it == classes_.end())
    {
        return nullptr;
    }
    return &it->second;
}

int getVirtualSlot(const OopIndex &index,
                   const std::string &qualifiedClass,
                   const std::string &methodName)
{
    const ClassInfo *info = index.findClass(qualifiedClass);
    if (!info)
        return -1;

    auto it = info->methods.find(methodName);
    if (it == info->methods.end())
        return -1;

    return it->second.slot;
}

} // namespace il::frontends::basic