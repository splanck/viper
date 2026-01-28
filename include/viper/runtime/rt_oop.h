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

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Interface metadata used during registration.
    typedef struct rt_iface_reg
    {
        int iface_id;      // process-local stable interface id
        const char *qname; // fully-qualified interface name (e.g., "Ns.IFace")
        int slot_count;    // number of method slots in the interface
    } rt_iface_reg;

    /// What: Register interface metadata with the runtime.
    /// Why:  Establish a stable interface identity and slot count for binding.
    /// How:  Inserts or verifies an entry keyed by @c iface_id and @c qname.
    ///       Re-registering an already known interface is idempotent.
    void rt_register_interface(const rt_iface_reg *iface);

    /// What: Bind a class (by @p type_id) to an interface implementation.
    /// Why:  Make interface dispatch resolve to the class's method table.
    /// How:  Associates @p itable_slots (size must equal registered @c slot_count)
    ///       with the (type_id, iface_id) pair in the runtime registry.
    void rt_bind_interface(int type_id, int iface_id, void **itable_slots);

    /// What: Resolve the dynamic type id of @p obj.
    /// Why:  Enable RTTI queries and interface dispatch at runtime.
    /// How:  Reads the vptr/type metadata embedded at object offset 0; returns 0
    ///       for null instances.
    int rt_typeid_of(void *obj);

    // Return non-zero when type_id is-a test_type_id (same type or derived class).
    int rt_type_is_a(int type_id, int test_type_id);

    // Return non-zero when the given type implements the interface.
    int rt_type_implements(int type_id, int iface_id);

    // Safe cast: returns obj when is-a holds; returns NULL on failure.
    void *rt_cast_as(void *obj, int target_type_id);

#ifdef __cplusplus
} // extern "C"
#endif
