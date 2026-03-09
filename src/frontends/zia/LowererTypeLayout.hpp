//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/zia/LowererTypeLayout.hpp
// Purpose: Type layout computation and storage for the Zia IL lowerer.
//
// Key invariants:
//   - Type layouts are populated before any method bodies are lowered
//   - Class IDs are unique and monotonically increasing
//
// Ownership/Lifetime:
//   - Owned by Lowerer
//   - Persists for the entire module lowering
//
// Links: frontends/zia/Lowerer.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/zia/LowererTypes.hpp"
#include "il/core/Type.hpp"
#include "support/alignment.hpp"
#include <cstddef>
#include <string>
#include <unordered_map>

namespace il::frontends::zia
{

class LowererTypeLayout
{
  public:
    using Type = il::core::Type;
    LowererTypeLayout() = default;

    ValueTypeInfo *findValueType(const std::string &name)
    {
        auto it = valueTypes_.find(name);
        return it != valueTypes_.end() ? &it->second : nullptr;
    }
    const ValueTypeInfo *findValueType(const std::string &name) const
    {
        auto it = valueTypes_.find(name);
        return it != valueTypes_.end() ? &it->second : nullptr;
    }
    bool hasValueType(const std::string &name) const { return valueTypes_.count(name) > 0; }
    void registerValueType(const std::string &name, ValueTypeInfo info);
    std::unordered_map<std::string, ValueTypeInfo> &valueTypes() { return valueTypes_; }
    const std::unordered_map<std::string, ValueTypeInfo> &valueTypes() const { return valueTypes_; }

    EntityTypeInfo *findEntityType(const std::string &name)
    {
        auto it = entityTypes_.find(name);
        return it != entityTypes_.end() ? &it->second : nullptr;
    }
    const EntityTypeInfo *findEntityType(const std::string &name) const
    {
        auto it = entityTypes_.find(name);
        return it != entityTypes_.end() ? &it->second : nullptr;
    }
    bool hasEntityType(const std::string &name) const { return entityTypes_.count(name) > 0; }
    void registerEntityType(const std::string &name, EntityTypeInfo info);
    std::unordered_map<std::string, EntityTypeInfo> &entityTypes() { return entityTypes_; }
    const std::unordered_map<std::string, EntityTypeInfo> &entityTypes() const { return entityTypes_; }

    InterfaceTypeInfo *findInterfaceType(const std::string &name)
    {
        auto it = interfaceTypes_.find(name);
        return it != interfaceTypes_.end() ? &it->second : nullptr;
    }
    const InterfaceTypeInfo *findInterfaceType(const std::string &name) const
    {
        auto it = interfaceTypes_.find(name);
        return it != interfaceTypes_.end() ? &it->second : nullptr;
    }
    void registerInterfaceType(const std::string &name, InterfaceTypeInfo info);
    std::unordered_map<std::string, InterfaceTypeInfo> &interfaceTypes() { return interfaceTypes_; }
    const std::unordered_map<std::string, InterfaceTypeInfo> &interfaceTypes() const { return interfaceTypes_; }

    int nextClassId() { return nextClassId_++; }
    int peekNextClassId() const { return nextClassId_; }
    int nextIfaceId() { return nextIfaceId_++; }
    int peekNextIfaceId() const { return nextIfaceId_; }

    static size_t getILTypeSize(Type type)
    {
        switch (type.kind)
        {
            case Type::Kind::I64: case Type::Kind::F64: case Type::Kind::Ptr: case Type::Kind::Str: return 8;
            case Type::Kind::I32: return 4;
            case Type::Kind::I16: return 2;
            case Type::Kind::I1: return 1;
            default: return 8;
        }
    }

    static size_t getILTypeAlignment(Type type)
    {
        switch (type.kind)
        {
            case Type::Kind::I64: case Type::Kind::F64: case Type::Kind::Ptr: case Type::Kind::Str: return 8;
            case Type::Kind::I32: return 4;
            case Type::Kind::I16: return 2;
            case Type::Kind::I1: return 8;
            default: return 8;
        }
    }

    static size_t alignTo(size_t offset, size_t alignment)
    {
        return il::support::alignUp(offset, alignment);
    }

  private:
    std::unordered_map<std::string, ValueTypeInfo> valueTypes_;
    std::unordered_map<std::string, EntityTypeInfo> entityTypes_;
    std::unordered_map<std::string, InterfaceTypeInfo> interfaceTypes_;
    int nextClassId_{1};
    int nextIfaceId_{1};
};

} // namespace il::frontends::zia
