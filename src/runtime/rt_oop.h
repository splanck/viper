//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file defines the minimal runtime ABI for object-oriented programming
// features in BASIC, including class metadata, virtual dispatch, and interface
// support. The design provides vtable-based polymorphism with stable ABI for
// compiled IL programs.
//
// Viper's OOP runtime uses C-compatible structures and calling conventions to
// enable seamless interoperation between IL-generated code and runtime libraries.
// Each object instance begins with a vptr (virtual pointer) that points into
// the class's vtable, enabling efficient virtual method dispatch. Class metadata
// structures store type information, inheritance relationships, and method tables.
//
// Key Design Elements:
// - vtable-based dispatch: Each class has a stable vtable with fixed slot assignments
//   for virtual methods, computed at compile time
// - vptr at offset 0: Every object's first field is the vptr, enabling trivial
//   virtual dispatch with a single memory dereference and indirect call
// - Class metadata: rt_class_info structures store type ID, qualified name,
//   base class pointer, and vtable pointer for runtime type queries
// - Interface support: Dynamic interface casting and method resolution for
//   interface-based polymorphism
//
// The runtime maintains a registry of class metadata that the compiler populates
// during module initialization. This enables runtime type checking, dynamic casts,
// and reflection-like queries while maintaining efficient compiled dispatch.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct rt_class_info
    {
        int type_id;                      // stable type id assigned by compiler
        const char *qname;                // fully-qualified class name, e.g. "A.B.C"
        const struct rt_class_info *base; // base class metadata, or NULL
        void **vtable;                    // array of function pointers (slots)
        uint32_t vtable_len;              // number of slots in vtable
    } rt_class_info;

    typedef struct rt_object
    {
        void **vptr; // points into class vtable (slot 0)
        // instance fields follow (layout defined by compiler)
    } rt_object;

    // Registration API (no-op placeholder if registry is not enabled)
    /// What: Register a class using an aggregate @p rt_class_info descriptor.
    /// Why:  Populate the runtime registry for type queries and virtual dispatch.
    /// How:  Inserts or verifies metadata keyed by type id and qualified name; binds the
    ///       canonical vtable pointer and base class relationship.
    ///
    /// @param ci Pointer to class info describing type id, qname, base, vtable, vtable_len.
    /// @pre Called during module initialization before objects of this class are created.
    /// @thread-safety Registration is expected to occur single-threaded at startup.
    void rt_register_class(const rt_class_info *ci);

    // Virtual dispatch helper: fetch vfunc pointer by slot from object
    /// What: Resolve a virtual function pointer at @p slot for @p obj.
    /// Why:  Enable fast indirect calls without duplicating vtable layout logic.
    /// How:  Reads vptr, optionally bounds-checks @p slot against vtable_len, and returns
    ///       the function pointer stored in the vtable.
    ///
    /// @param obj  Instance pointer (opaque object starting with vptr field).
    /// @param slot 0-based vtable slot index assigned at compile time.
    /// @return Function pointer stored at the slot; NULL if unavailable.
    void *rt_get_vfunc(const rt_object *obj, uint32_t slot);

    // Internal helper: get class info from vptr (used for bounds checking)
    /// What: Map a vtable pointer to its owning class metadata.
    /// Why:  Support bounds checks and diagnostic messages.
    /// How:  Looks up @p vptr in the runtime registry and returns class info when known.
    ///
    /// @param vptr Canonical vtable pointer for a class.
    /// @return Class metadata or NULL when unregistered.
    const rt_class_info *rt_get_class_info_from_vptr(void **vptr);

    // Interface registration --------------------------------------------------
    typedef struct
    {
        int iface_id;      // stable interface id assigned by compiler
        const char *qname; // fully-qualified interface name
        int slot_count;    // number of methods in interface slot table
    } rt_iface_reg;

    // Register interface metadata
    /// What: Register interface metadata.
    /// Why:  Establish interface identity and slot count for binding.
    /// How:  Inserts metadata keyed by iface_id and qname.
    void rt_register_interface(const rt_iface_reg *iface);

    // Bind class (type_id) to interface with concrete itable slots array
    // itable_slots must point to an array of slot_count function pointers.
    /// What: Bind @p type_id to @p iface_id with @p itable_slots.
    /// Why:  Enable interface dispatch to call the correct method table.
    /// How:  Stores the itable in the registry; size must match @c slot_count.
    void rt_bind_interface(int type_id, int iface_id, void **itable_slots);

    // RTTI helpers ------------------------------------------------------------
    // Resolve a type id from an object instance
    /// What: Resolve the dynamic type id of @p obj.
    /// Why:  Enable RTTI and fast type comparisons.
    /// How:  Reads the type id associated with the object's vptr.
    int rt_typeid_of(void *obj);

    // True if type_id is-a test_type_id (same type or subclass)
    /// What: Check whether @p type_id is-a @p test_type_id.
    /// Why:  Support subtype checks for dynamic casts and runtime validation.
    /// How:  Consults class metadata and inheritance relationships.
    int rt_type_is_a(int type_id, int test_type_id);

    /// What: Check whether @p type_id implements @p iface_id.
    /// Why:  Enable fast is-assignable checks at runtime.
    /// How:  Consults the runtime registry of (type, interface) bindings.
    int rt_type_implements(int type_id, int iface_id);

    /// What: Cast @p obj to @p target_type_id, returning @p obj or NULL.
    /// Why:  Support safe downcasts with null-on-failure semantics.
    /// How:  Compares dynamic type id against @p target_type_id.
    void *rt_cast_as(void *obj, int target_type_id);

    /// What: Cast @p obj to interface @p iface_id, returning @p obj or NULL.
    /// Why:  Support safe interface casts under OOP semantics.
    /// How:  Verifies interface implementation via the registry.
    void *rt_cast_as_iface(void *obj, int iface_id);

    /// What: Lookup the interface table pointer for (@p obj, @p iface_id).
    /// Why:  Enable direct method slot invocation for interface calls.
    /// How:  Resolves the bound itable or returns NULL when unbound.
    void **rt_itable_lookup(void *obj, int iface_id);

    /// What: Register an interface directly without an aggregate descriptor.
    /// Why:  Simplify compiler-emitted registration during module init.
    /// How:  Inserts metadata keyed by @p iface_id and @p qname with @p slot_count.
    ///
    /// @param iface_id   Stable interface id assigned by the compiler.
    /// @param qname      Fully-qualified interface name (e.g., "Ns.IFace").
    /// @param slot_count Number of method slots in the interface table.
    void rt_register_interface_direct(int iface_id, const char *qname, int slot_count);

    /// What: Register interface using a runtime string for @p qname.
    /// Why:  Avoid lifetime issues with transient C strings in IL.
    /// How:  Extracts C-string from @p qname and delegates.
    ///
    /// @param iface_id   Stable interface id assigned by the compiler.
    /// @param qname      Runtime string containing fully-qualified interface name.
    /// @param slot_count Number of method slots in the interface table.
    void rt_register_interface_direct_rs(int64_t iface_id, rt_string qname, int64_t slot_count);

    // Convenience: direct class registration used by compiler-emitted module init.
    // Allocates metadata and registers it in the per-VM registry so lookups by
    // vtable pointer succeed and Object.ToString can render the qualified name.
    /// What: Register a class directly with its vtable and qualified name.
    /// Why:  Make type-id and vtable lookups cheap and reliable.
    /// How:  Allocates metadata and binds it in the runtime registry.
    ///
    /// @param type_id     Stable class id assigned by the compiler.
    /// @param vtable      Canonical vtable pointer (array of function pointers).
    /// @param qname       Fully-qualified class name.
    /// @param vslot_count Number of vtable slots bound for this class.
    void rt_register_class_direct(int type_id, void **vtable, const char *qname, int vslot_count);
    /// What: Register a class using a runtime string for @p qname.
    /// Why:  Avoids lifetime issues with transient C strings.
    /// How:  Copies/binds @p qname and registers as above.
    ///
    /// @param type_id     Stable class id assigned by the compiler.
    /// @param vtable      Canonical vtable pointer.
    /// @param qname       Runtime string containing fully-qualified class name.
    /// @param vslot_count Number of vtable slots bound for this class.
    void rt_register_class_direct_rs(int type_id,
                                     void **vtable,
                                     rt_string qname,
                                     int64_t vslot_count);

    /// What: Register a class with its base class type id.
    /// Why:  Enable inheritance chains to be wired at registration time.
    /// How:  Allocates metadata, sets base pointer by looking up @p base_type_id
    ///       in the registry, and binds the class.
    ///
    /// @param type_id      Stable class id assigned by the compiler.
    /// @param vtable       Canonical vtable pointer (array of function pointers).
    /// @param qname        Fully-qualified class name.
    /// @param vslot_count  Number of vtable slots bound for this class.
    /// @param base_type_id Type id of the base class, or -1 if none.
    void rt_register_class_with_base(
        int type_id, void **vtable, const char *qname, int vslot_count, int base_type_id);

    /// What: Register a class with base class using a runtime string for @p qname.
    /// Why:  Variant of rt_register_class_with_base that accepts rt_string.
    /// How:  Copies/binds @p qname and delegates to rt_register_class_with_base.
    ///
    /// @param type_id      Stable class id assigned by the compiler.
    /// @param vtable       Canonical vtable pointer.
    /// @param qname        Runtime string containing fully-qualified class name.
    /// @param vslot_count  Number of vtable slots bound for this class.
    /// @param base_type_id Type id of the base class, or -1 if none.
    void rt_register_class_with_base_rs(
        int type_id, void **vtable, rt_string qname, int64_t vslot_count, int64_t base_type_id);

    /// What: Lookup the canonical vtable pointer for @p type_id.
    /// Why:  Allow VM/runtime to resolve method tables by type id.
    /// How:  Returns the registered vtable or NULL when unknown.
    ///
    /// @param type_id Stable class id.
    /// @return Canonical vtable pointer or NULL.
    void **rt_get_class_vtable(int type_id);

    /// What: Register an interface implementation table for a class.
    /// Why:  Enable interface dispatch for classes implementing interfaces.
    /// How:  Wraps rt_bind_interface with i64 parameters for IL compatibility.
    ///
    /// @param type_id   Class type id.
    /// @param iface_id  Interface id.
    /// @param itable    Interface method table (array of function pointers).
    void rt_register_interface_impl(int64_t type_id, int64_t iface_id, void **itable);

    /// What: Lookup the interface implementation table for a class type.
    /// Why:  Enable interface assignment to resolve itable at compile-known types.
    /// How:  Looks up the binding by (type_id, iface_id), walking base classes.
    ///
    /// @param type_id  Class type id.
    /// @param iface_id Interface id.
    /// @return Interface method table or NULL.
    void **rt_get_interface_impl(int64_t type_id, int64_t iface_id);

#ifdef __cplusplus
}
#endif
