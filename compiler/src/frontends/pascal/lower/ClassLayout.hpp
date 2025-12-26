//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/lower/ClassLayout.hpp
// Purpose: OOP class layout structures for Pascal IL lowering.
// Key invariants: Field offsets are byte offsets from object base.
// Ownership/Lifetime: Value types, copyable.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/pascal/sem/Types.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace il::frontends::pascal
{

//===----------------------------------------------------------------------===//
// Field and Class Layout
//===----------------------------------------------------------------------===//

/// @brief Layout information for a single field in a class.
struct ClassFieldLayout
{
    std::string name;   ///< Field name
    PasType type;       ///< Field type
    std::size_t offset; ///< Byte offset from object base
    std::size_t size;   ///< Size in bytes
    bool isWeak{false}; ///< Weak reference field (no refcount increment)
};

/// @brief Complete layout for a class including inherited fields.
struct ClassLayout
{
    std::string name;                     ///< Class name
    std::vector<ClassFieldLayout> fields; ///< All fields in layout order
    std::size_t size;                     ///< Total object size (8-byte aligned)
    std::int64_t classId;                 ///< Unique runtime type ID

    /// @brief Find a field by name.
    const ClassFieldLayout *findField(const std::string &name) const;
};

//===----------------------------------------------------------------------===//
// Vtable Layout
//===----------------------------------------------------------------------===//

/// @brief Vtable slot information.
struct VtableSlot
{
    std::string methodName; ///< Method name
    std::string implClass;  ///< Class that provides implementation
    int slot;               ///< Slot index in vtable
};

/// @brief Vtable layout for a class.
struct VtableLayout
{
    std::string className;         ///< Class this vtable belongs to
    std::vector<VtableSlot> slots; ///< Slots in order
    std::size_t slotCount;         ///< Number of slots
};

//===----------------------------------------------------------------------===//
// Interface Layout
//===----------------------------------------------------------------------===//

/// @brief Interface method slot.
struct InterfaceSlot
{
    std::string methodName; ///< Method name in the interface
    int slot;               ///< Slot index in interface table
};

/// @brief Interface layout (method table).
struct InterfaceLayout
{
    std::string name;                 ///< Interface name
    std::int64_t interfaceId;         ///< Unique interface ID
    std::vector<InterfaceSlot> slots; ///< Method slots in order
    std::size_t slotCount;            ///< Number of slots
};

/// @brief Interface implementation table for a class.
/// Maps interface method slots to actual class method implementations.
struct InterfaceImplTable
{
    std::string className;                ///< Class implementing the interface
    std::string interfaceName;            ///< Interface being implemented
    std::vector<std::string> implMethods; ///< Mangled names of implementing methods, in slot order
};

} // namespace il::frontends::pascal
