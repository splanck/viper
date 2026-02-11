//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/runtime/rt_oop.h
// Purpose: Public OOP runtime ABI for interface registration, binding, and RTTI.
// Key invariants: type_id and iface_id are process-local, assigned at module load;
//                 objects embed a vptr at offset 0; interface slots follow
//                 interface declaration order and bind per (class, interface).
// Ownership/Lifetime: Class/interface metadata is owned by the runtime registry
//                     for the duration of the process. Callers must ensure any
//                     itable slot arrays passed to bind are live for as long as
//                     the class remains loaded.
// Links: docs/oop.md, docs/grammar.md, docs/CHANGELOG.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Interface metadata used during registration.
    /// @details Describes a single interface so the runtime can allocate
    ///          dispatch slots and validate binding requests.
    typedef struct rt_iface_reg
    {
        int iface_id;      ///< Process-local stable interface id.
        const char *qname; ///< Fully-qualified interface name (e.g., "Ns.IFace").
        int slot_count;    ///< Number of method slots in the interface.
    } rt_iface_reg;

    /// @brief Register interface metadata with the runtime.
    /// @details Establishes a stable interface identity and slot count for binding.
    ///          Inserts or verifies an entry keyed by @c iface_id and @c qname.
    ///          Re-registering an already known interface is idempotent.
    /// @param iface Pointer to interface registration descriptor (must not be NULL).
    void rt_register_interface(const rt_iface_reg *iface);

    /// @brief Bind a class to an interface implementation.
    /// @details Associates @p itable_slots (size must equal registered @c slot_count)
    ///          with the (type_id, iface_id) pair in the runtime registry, enabling
    ///          interface dispatch to resolve to the class's method table.
    /// @param type_id  Process-local type identifier for the class.
    /// @param iface_id Interface identifier previously registered via rt_register_interface.
    /// @param itable_slots Array of function pointers implementing the interface slots;
    ///                     must remain live for the lifetime of the class registration.
    void rt_bind_interface(int type_id, int iface_id, void **itable_slots);

    /// @brief Resolve the dynamic type id of @p obj.
    /// @details Reads the vptr/type metadata embedded at object offset 0 to enable
    ///          RTTI queries and interface dispatch at runtime.
    /// @param obj Pointer to the object instance; may be NULL.
    /// @return The process-local type id of the object, or 0 for NULL instances.
    int rt_typeid_of(void *obj);

    /// @brief Test whether @p type_id is the same as or derived from @p test_type_id.
    /// @param type_id      Type identifier to test.
    /// @param test_type_id Base type identifier to compare against.
    /// @return Non-zero when @p type_id is-a @p test_type_id; 0 otherwise.
    int rt_type_is_a(int type_id, int test_type_id);

    /// @brief Test whether the given type implements an interface.
    /// @param type_id  Type identifier to query.
    /// @param iface_id Interface identifier to check.
    /// @return Non-zero when @p type_id implements @p iface_id; 0 otherwise.
    int rt_type_implements(int type_id, int iface_id);

    /// @brief Safe downcast: returns @p obj when is-a holds, NULL on failure.
    /// @param obj             Pointer to the object instance; may be NULL.
    /// @param target_type_id  Desired target type identifier.
    /// @return @p obj if its dynamic type is-a @p target_type_id; NULL otherwise.
    void *rt_cast_as(void *obj, int target_type_id);

#ifdef __cplusplus
} // extern "C"
#endif
