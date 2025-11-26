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

#include <algorithm>
#include <cctype>

namespace il::frontends::basic
{

namespace
{

/// @brief Case-insensitive string comparison helper.
bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

} // namespace

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

// =============================================================================
// Field Query API Implementation
// =============================================================================

const ClassInfo::FieldInfo *OopIndex::findField(const std::string &className,
                                                std::string_view fieldName) const
{
    const ClassInfo *info = findClass(className);
    if (!info)
        return nullptr;

    // Search instance fields (case-insensitive)
    for (const auto &field : info->fields)
    {
        if (iequals(field.name, fieldName))
            return &field;
    }

    // Search static fields (case-insensitive)
    for (const auto &field : info->staticFields)
    {
        if (iequals(field.name, fieldName))
            return &field;
    }

    return nullptr;
}

const ClassInfo::FieldInfo *OopIndex::findFieldInHierarchy(const std::string &className,
                                                           std::string_view fieldName) const
{
    const ClassInfo *cur = findClass(className);
    while (cur)
    {
        // Search instance fields
        for (const auto &field : cur->fields)
        {
            if (iequals(field.name, fieldName))
                return &field;
        }

        // Search static fields
        for (const auto &field : cur->staticFields)
        {
            if (iequals(field.name, fieldName))
                return &field;
        }

        // Move to base class
        if (cur->baseQualified.empty())
            break;
        cur = findClass(cur->baseQualified);
    }
    return nullptr;
}

// =============================================================================
// Method Query API Implementation
// =============================================================================

const ClassInfo::MethodInfo *OopIndex::findMethod(const std::string &className,
                                                  std::string_view methodName) const
{
    const ClassInfo *info = findClass(className);
    if (!info)
        return nullptr;

    auto it = info->methods.find(std::string(methodName));
    if (it != info->methods.end())
        return &it->second;

    return nullptr;
}

const ClassInfo::MethodInfo *OopIndex::findMethodInHierarchy(const std::string &className,
                                                             std::string_view methodName) const
{
    const ClassInfo *cur = findClass(className);
    while (cur)
    {
        auto it = cur->methods.find(std::string(methodName));
        if (it != cur->methods.end())
            return &it->second;

        // Move to base class
        if (cur->baseQualified.empty())
            break;
        cur = findClass(cur->baseQualified);
    }
    return nullptr;
}

// =============================================================================
// Virtual Slot Query
// =============================================================================

int getVirtualSlot(const OopIndex &index,
                   const std::string &qualifiedClass,
                   const std::string &methodName)
{
    const ClassInfo::MethodInfo *mi = index.findMethod(qualifiedClass, methodName);
    if (!mi)
        return -1;
    return mi->slot;
}

} // namespace il::frontends::basic