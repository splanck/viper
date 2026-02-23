//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_oop.h
// Purpose: Minimal runtime ABI for object-oriented features in BASIC including class metadata, vtable-based virtual dispatch, interface support, and the class registry.
//
// Key invariants:
//   - vptr is always at offset 0 in every object; this is a stable ABI invariant.
//   - vtable slot indices are compile-time constants set by the codegen layer.
//   - The class registry is populated at startup before any objects are created.
//   - Interface support uses secondary vtable pointers for multi-interface dispatch.
//
// Ownership/Lifetime:
//   - Class metadata is allocated once per type and persists for the process lifetime.
//   - Object instances are reference-counted; the class metadata is not refcounted.
//
// Links: src/runtime/oop/rt_oop.c (implementation), src/runtime/core/rt_heap.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Compile-time class metadata descriptor.
    /// @details Describes a single class type for runtime type identification,
    ///          virtual dispatch, and inheritance queries. Populated by the
    ///          compiler and registered during module initialization.
    typedef struct rt_class_info
    {
        int type_id;                      ///< Stable type id assigned by the compiler.
        const char *qname;                ///< Fully-qualified class name, e.g. "A.B.C".
        const struct rt_class_info *base; ///< Base class metadata, or NULL for root classes.
        void **vtable;                    ///< Array of function pointers (virtual method slots).
        uint32_t vtable_len;              ///< Number of slots in the vtable.
    } rt_class_info;

    /// @brief Runtime object header with vptr at offset 0.
    /// @details Every heap-allocated object begins with this layout. Instance
    ///          fields follow immediately after the vptr (layout defined by the compiler).
    typedef struct rt_object
    {
        void **vptr; ///< Points into the class vtable (slot 0).
    } rt_object;

    /// @brief Register a class using an aggregate rt_class_info descriptor.
    /// @details Populates the runtime registry for type queries and virtual dispatch.
    ///          Inserts or verifies metadata keyed by type id and qualified name, and
    ///          binds the canonical vtable pointer and base class relationship.
    /// @param ci Pointer to class info describing type id, qname, base, vtable, and vtable_len.
    /// @pre Called during module initialization before objects of this class are created.
    /// @thread-safety Registration is expected to occur single-threaded at startup.
    void rt_register_class(const rt_class_info *ci);

    /// @brief Resolve a virtual function pointer by slot index from an object.
    /// @details Reads the object's vptr, optionally bounds-checks the slot against
    ///          vtable_len, and returns the function pointer stored in the vtable.
    ///          Enables fast indirect calls without duplicating vtable layout logic.
    /// @param obj  Instance pointer (opaque object starting with vptr field).
    /// @param slot 0-based vtable slot index assigned at compile time.
    /// @return Function pointer stored at the slot; NULL if unavailable.
    void *rt_get_vfunc(const rt_object *obj, uint32_t slot);

    /// @brief Map a vtable pointer to its owning class metadata.
    /// @details Looks up the vptr in the runtime registry and returns class info
    ///          when known. Used internally for bounds checking and diagnostic messages.
    /// @param vptr Canonical vtable pointer for a class.
    /// @return Class metadata, or NULL when the vptr is unregistered.
    const rt_class_info *rt_get_class_info_from_vptr(void **vptr);

    /// @brief Interface metadata descriptor for registration.
    typedef struct
    {
        int iface_id;      ///< Stable interface id assigned by the compiler.
        const char *qname; ///< Fully-qualified interface name.
        int slot_count;    ///< Number of methods in the interface slot table.
    } rt_iface_reg;

    /// @brief Register interface metadata.
    /// @details Establishes interface identity and slot count for binding.
    ///          Inserts metadata keyed by iface_id and qname.
    /// @param iface Pointer to the interface registration descriptor.
    void rt_register_interface(const rt_iface_reg *iface);

    /// @brief Bind a class to an interface with a concrete itable slots array.
    /// @details Stores the interface table in the registry, enabling interface dispatch
    ///          to call the correct method table for the given type. The itable_slots
    ///          array must have exactly slot_count entries.
    /// @param type_id     Class type id.
    /// @param iface_id    Interface id to bind.
    /// @param itable_slots Array of function pointers (must match the interface's slot_count).
    void rt_bind_interface(int type_id, int iface_id, void **itable_slots);

    /// @brief Resolve the dynamic type id of an object instance.
    /// @details Reads the type id associated with the object's vptr for RTTI
    ///          and fast type comparisons.
    /// @param obj Object instance pointer (must have vptr at offset 0).
    /// @return The compile-time-assigned type id of the object's dynamic type.
    int rt_typeid_of(void *obj);

    /// @brief Check whether type_id is-a test_type_id (same type or subclass).
    /// @details Consults class metadata and walks the inheritance chain to
    ///          determine if the type relationship holds.
    /// @param type_id      The type to test.
    /// @param test_type_id The target type to test against.
    /// @return Non-zero if type_id is the same as or a subclass of test_type_id; 0 otherwise.
    int rt_type_is_a(int type_id, int test_type_id);

    /// @brief Check whether a type implements an interface.
    /// @details Consults the runtime registry of (type, interface) bindings
    ///          to perform fast is-assignable checks at runtime.
    /// @param type_id  The class type id.
    /// @param iface_id The interface id to check.
    /// @return Non-zero if the type implements the interface; 0 otherwise.
    int rt_type_implements(int type_id, int iface_id);

    /// @brief Cast an object to a target class type, returning the object or NULL.
    /// @details Performs a safe downcast by comparing the object's dynamic type id
    ///          against the target. Returns the same pointer on success, or NULL
    ///          on failure (null-on-failure semantics).
    /// @param obj            Object instance pointer.
    /// @param target_type_id The class type id to cast to.
    /// @return The same @p obj pointer if the cast succeeds; NULL otherwise.
    void *rt_cast_as(void *obj, int target_type_id);

    /// @brief Cast an object to an interface, returning the object or NULL.
    /// @details Verifies that the object's dynamic type implements the interface.
    ///          Returns the same pointer on success, or NULL on failure.
    /// @param obj      Object instance pointer.
    /// @param iface_id The interface id to cast to.
    /// @return The same @p obj pointer if the cast succeeds; NULL otherwise.
    void *rt_cast_as_iface(void *obj, int iface_id);

    /// @brief Lookup the interface table pointer for an object and interface.
    /// @details Resolves the bound itable for direct method slot invocation
    ///          during interface calls.
    /// @param obj      Object instance pointer.
    /// @param iface_id The interface id to look up.
    /// @return Pointer to the interface method table, or NULL when unbound.
    void **rt_itable_lookup(void *obj, int iface_id);

    /// @brief Register an interface directly without an aggregate descriptor.
    /// @details Simplifies compiler-emitted registration during module init by
    ///          accepting individual parameters instead of a struct.
    /// @param iface_id   Stable interface id assigned by the compiler.
    /// @param qname      Fully-qualified interface name (e.g., "Ns.IFace").
    /// @param slot_count Number of method slots in the interface table.
    void rt_register_interface_direct(int iface_id, const char *qname, int slot_count);

    /// @brief Register an interface using a runtime string for the qualified name.
    /// @details Avoids lifetime issues with transient C strings in IL by extracting
    ///          the C-string from the runtime string and delegating to the direct form.
    /// @param iface_id   Stable interface id assigned by the compiler.
    /// @param qname      Runtime string containing the fully-qualified interface name.
    /// @param slot_count Number of method slots in the interface table.
    void rt_register_interface_direct_rs(int64_t iface_id, rt_string qname, int64_t slot_count);

    /// @brief Register a class directly with its vtable and qualified name.
    /// @details Allocates metadata and registers it in the per-VM registry so that
    ///          lookups by vtable pointer succeed and Object.ToString can render
    ///          the qualified name. Makes type-id and vtable lookups cheap and reliable.
    /// @param type_id     Stable class id assigned by the compiler.
    /// @param vtable      Canonical vtable pointer (array of function pointers).
    /// @param qname       Fully-qualified class name.
    /// @param vslot_count Number of vtable slots bound for this class.
    void rt_register_class_direct(int type_id, void **vtable, const char *qname, int vslot_count);

    /// @brief Register a class using a runtime string for the qualified name.
    /// @details Variant of rt_register_class_direct that accepts an rt_string to
    ///          avoid lifetime issues with transient C strings.
    /// @param type_id     Stable class id assigned by the compiler.
    /// @param vtable      Canonical vtable pointer.
    /// @param qname       Runtime string containing the fully-qualified class name.
    /// @param vslot_count Number of vtable slots bound for this class.
    void rt_register_class_direct_rs(int type_id,
                                     void **vtable,
                                     rt_string qname,
                                     int64_t vslot_count);

    /// @brief Register a class with its base class type id.
    /// @details Allocates metadata, sets the base pointer by looking up base_type_id
    ///          in the registry, and binds the class. Enables inheritance chains to
    ///          be wired at registration time.
    /// @param type_id      Stable class id assigned by the compiler.
    /// @param vtable       Canonical vtable pointer (array of function pointers).
    /// @param qname        Fully-qualified class name.
    /// @param vslot_count  Number of vtable slots bound for this class.
    /// @param base_type_id Type id of the base class, or -1 if none.
    void rt_register_class_with_base(
        int type_id, void **vtable, const char *qname, int vslot_count, int base_type_id);

    /// @brief Register a class with base class using a runtime string for the qualified name.
    /// @details Variant of rt_register_class_with_base that accepts an rt_string to
    ///          avoid lifetime issues with transient C strings.
    /// @param type_id      Stable class id assigned by the compiler.
    /// @param vtable       Canonical vtable pointer.
    /// @param qname        Runtime string containing the fully-qualified class name.
    /// @param vslot_count  Number of vtable slots bound for this class.
    /// @param base_type_id Type id of the base class, or -1 if none.
    void rt_register_class_with_base_rs(
        int type_id, void **vtable, rt_string qname, int64_t vslot_count, int64_t base_type_id);

    /// @brief Lookup the canonical vtable pointer for a class type id.
    /// @details Allows the VM and runtime to resolve method tables by type id
    ///          without holding a direct reference to the vtable.
    /// @param type_id Stable class id.
    /// @return Canonical vtable pointer, or NULL if the type id is unknown.
    void **rt_get_class_vtable(int type_id);

    /// @brief Register an interface implementation table for a class.
    /// @details Wraps rt_bind_interface with i64 parameters for IL compatibility.
    ///          Enables interface dispatch for classes implementing interfaces.
    /// @param type_id  Class type id.
    /// @param iface_id Interface id.
    /// @param itable   Interface method table (array of function pointers).
    void rt_register_interface_impl(int64_t type_id, int64_t iface_id, void **itable);

    /// @brief Lookup the interface implementation table for a class type.
    /// @details Looks up the binding by (type_id, iface_id), walking base classes
    ///          if needed. Enables interface assignment to resolve the itable at
    ///          compile-known types.
    /// @param type_id  Class type id.
    /// @param iface_id Interface id.
    /// @return Interface method table, or NULL if no binding exists.
    void **rt_get_interface_impl(int64_t type_id, int64_t iface_id);

#ifdef __cplusplus
}
#endif
