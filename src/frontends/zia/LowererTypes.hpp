//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/zia/LowererTypes.hpp
// Purpose: Type layout structures for the Zia IL lowerer.
//
// Key invariants:
//   - Field offsets respect alignment requirements
//   - Entity field offsets start after the 16-byte object header
//   - Class and interface IDs are unique within a module
//
// Ownership/Lifetime:
//   - Instances are owned by Lowerer (via unordered_map members)
//   - Persist for the duration of module lowering
//
// Links: frontends/zia/LowererTypeLayout.hpp, frontends/zia/Lowerer.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/zia/AST.hpp"
#include "frontends/zia/Types.hpp"
#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::frontends::zia
{

struct FieldLayout
{
    std::string name;
    TypeRef type;
    size_t offset;
    size_t size;
};

struct ValueTypeInfo
{
    std::string name;
    std::vector<FieldLayout> fields;
    std::vector<MethodDecl *> methods;
    size_t totalSize;
    std::unordered_map<std::string, size_t> fieldIndex;
    std::unordered_map<std::string, MethodDecl *> methodMap;

    const FieldLayout *findField(const std::string &n) const
    {
        auto it = fieldIndex.find(n);
        return it != fieldIndex.end() ? &fields[it->second] : nullptr;
    }

    MethodDecl *findMethod(const std::string &n) const
    {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }
};

struct EntityTypeInfo
{
    std::string name;
    std::string baseClass;
    std::vector<FieldLayout> fields;
    std::vector<MethodDecl *> methods;
    size_t totalSize;
    int classId;
    std::unordered_map<std::string, size_t> fieldIndex;
    std::unordered_map<std::string, MethodDecl *> methodMap;
    std::vector<std::string> vtable;
    std::unordered_map<std::string, size_t> vtableIndex;
    std::string vtableName;
    std::set<std::string> implementedInterfaces;
    std::set<std::string> propertyGetters;
    std::set<std::string> propertySetters;

    const FieldLayout *findField(const std::string &n) const
    {
        auto it = fieldIndex.find(n);
        return it != fieldIndex.end() ? &fields[it->second] : nullptr;
    }

    MethodDecl *findMethod(const std::string &n) const
    {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }

    size_t findVtableSlot(const std::string &n) const
    {
        auto it = vtableIndex.find(n);
        return it != vtableIndex.end() ? it->second : SIZE_MAX;
    }
};

struct InterfaceTypeInfo
{
    std::string name;
    int ifaceId{-1};
    std::vector<MethodDecl *> methods;
    std::unordered_map<std::string, MethodDecl *> methodMap;
    std::unordered_map<std::string, size_t> slotIndex;

    MethodDecl *findMethod(const std::string &n) const
    {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }

    size_t findSlot(const std::string &n) const
    {
        auto it = slotIndex.find(n);
        return it != slotIndex.end() ? it->second : SIZE_MAX;
    }
};

} // namespace il::frontends::zia
