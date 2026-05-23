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

namespace il::frontends::zia {

/// @brief In-memory layout of a single struct/class field.
struct FieldLayout {
    std::string name;    ///< Field name.
    TypeRef type;        ///< Semantic type of the field.
    size_t offset;       ///< Byte offset within the owning object/struct.
    size_t size;         ///< Byte size of the field's storage.
    bool isWeak{false};  ///< True for weak reference fields (released via WeakRef.Free).
};

/// @brief Resolved layout and members of a value type (`struct`).
/// @details Field offsets start at 0 (value types have no object header). @c fieldIndex and
///          @c methodMap accelerate name lookups into @c fields / @c methods.
struct StructTypeInfo {
    std::string name;                              ///< Qualified type name.
    std::vector<FieldLayout> fields;               ///< Fields in declaration order.
    std::vector<MethodDecl *> methods;             ///< Declared methods.
    size_t totalSize;                              ///< Total byte size of the struct.
    int classId{-1};                               ///< Runtime class id (for interface binding).
    std::unordered_map<std::string, size_t> fieldIndex;     ///< Field name -> index into @c fields.
    std::unordered_map<std::string, MethodDecl *> methodMap; ///< Method name -> declaration.
    std::set<std::string> implementedInterfaces;   ///< Names of implemented interfaces.

    /// @brief Look up a field by name.
    /// @return Pointer to the field layout, or nullptr if absent.
    const FieldLayout *findField(const std::string &n) const {
        auto it = fieldIndex.find(n);
        return it != fieldIndex.end() ? &fields[it->second] : nullptr;
    }

    /// @brief Look up a method by name.
    /// @return The method declaration, or nullptr if absent.
    MethodDecl *findMethod(const std::string &n) const {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }
};

/// @brief Resolved layout and members of a reference type (`class`).
/// @details Fields start after the 16-byte object header and include inherited fields. The
///          @c vtable lists slot method names (with @c vtableIndex mapping slot keys to slots);
///          property getter/setter names are tracked for synthesis.
struct ClassTypeInfo {
    std::string name;                              ///< Qualified type name.
    std::string baseClass;                         ///< Qualified base-class name ("" if none).
    std::vector<FieldLayout> fields;               ///< Fields (inherited first, then own).
    std::vector<MethodDecl *> methods;             ///< Declared methods.
    size_t totalSize;                              ///< Total byte size including header.
    int classId;                                   ///< Unique runtime class id.
    std::unordered_map<std::string, size_t> fieldIndex;     ///< Field name -> index into @c fields.
    std::unordered_map<std::string, MethodDecl *> methodMap; ///< Method name -> declaration.
    std::vector<std::string> vtable;               ///< Lowered method name per vtable slot.
    std::unordered_map<std::string, size_t> vtableIndex;    ///< Method slot key -> vtable index.
    std::string vtableName;                        ///< Symbol name of the vtable global.
    std::set<std::string> implementedInterfaces;   ///< Names of implemented interfaces.
    std::set<std::string> propertyGetters;         ///< Synthesized `get_<Prop>` names.
    std::set<std::string> propertySetters;         ///< Synthesized `set_<Prop>` names.

    /// @brief Look up a field by name.
    /// @return Pointer to the field layout, or nullptr if absent.
    const FieldLayout *findField(const std::string &n) const {
        auto it = fieldIndex.find(n);
        return it != fieldIndex.end() ? &fields[it->second] : nullptr;
    }

    /// @brief Look up a method by name.
    /// @return The method declaration, or nullptr if absent.
    MethodDecl *findMethod(const std::string &n) const {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }

    /// @brief Resolve a method slot key to its vtable index.
    /// @return The slot index, or SIZE_MAX if the key is not in the vtable.
    size_t findVtableSlot(const std::string &n) const {
        auto it = vtableIndex.find(n);
        return it != vtableIndex.end() ? it->second : SIZE_MAX;
    }
};

/// @brief Resolved methods and itable slot layout of an interface.
struct InterfaceTypeInfo {
    std::string name;                              ///< Qualified interface name.
    int ifaceId{-1};                               ///< Unique runtime interface id.
    std::vector<MethodDecl *> methods;             ///< Interface methods in slot order.
    std::unordered_map<std::string, MethodDecl *> methodMap; ///< Method name -> declaration.
    std::unordered_map<std::string, size_t> slotIndex;       ///< Method slot key -> itable slot.

    /// @brief Look up an interface method by name.
    /// @return The method declaration, or nullptr if absent.
    MethodDecl *findMethod(const std::string &n) const {
        auto it = methodMap.find(n);
        return it != methodMap.end() ? it->second : nullptr;
    }

    /// @brief Resolve a method slot key to its itable slot index.
    /// @return The slot index, or SIZE_MAX if the key is unknown.
    size_t findSlot(const std::string &n) const {
        auto it = slotIndex.find(n);
        return it != slotIndex.end() ? it->second : SIZE_MAX;
    }
};

} // namespace il::frontends::zia
