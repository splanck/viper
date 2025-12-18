//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_oop_dispatch.c
/// @brief Virtual method dispatch for Viper's object-oriented runtime.
///
/// This file implements the virtual method dispatch mechanism that enables
/// polymorphism in Viper programs. When a method is called on an object, the
/// runtime looks up the correct implementation based on the object's actual
/// type at runtime.
///
/// **What is Virtual Dispatch?**
/// Virtual dispatch allows calling methods through a base class reference
/// while executing the derived class's implementation:
/// ```vb
/// Class Animal
///     Overridable Sub Speak()
///         Print "..."
///     End Sub
/// End Class
///
/// Class Dog Inherits Animal
///     Overrides Sub Speak()
///         Print "Woof!"
///     End Sub
/// End Class
///
/// Dim pet As Animal = New Dog()
/// pet.Speak()  ' Prints "Woof!" - Dog's implementation is called
/// ```
///
/// **VTable Architecture:**
/// Each class has a virtual table (vtable) containing pointers to its method
/// implementations. Objects store a pointer to their class's vtable (vptr):
/// ```
/// ┌─────────────────────────────────────────────────────────────────────────┐
/// │                         Virtual Dispatch                                │
/// │                                                                         │
/// │  ┌─────────────────┐           ┌─────────────────────────────────────┐  │
/// │  │     Object      │           │          Dog VTable                 │  │
/// │  │ ┌─────────────┐ │           │ ┌─────────────────────────────────┐ │  │
/// │  │ │ vptr ───────┼─┼───────────┼▶│ slot 0: Dog_ToString           │ │  │
/// │  │ │ field1      │ │           │ │ slot 1: Dog_Equals             │ │  │
/// │  │ │ field2      │ │           │ │ slot 2: Dog_Speak  ◀── override │ │  │
/// │  │ └─────────────┘ │           │ │ slot 3: Animal_Run             │ │  │
/// │  └─────────────────┘           │ └─────────────────────────────────┘ │  │
/// │                                └─────────────────────────────────────┘  │
/// │                                                                         │
/// │  Call sequence: obj.Speak()                                             │
/// │    1. Load vptr from object                                             │
/// │    2. Load slot 2 from vtable → Dog_Speak                               │
/// │    3. Call Dog_Speak(obj)                                               │
/// └─────────────────────────────────────────────────────────────────────────┘
/// ```
///
/// **Slot Assignment:**
/// Virtual method slots are assigned during class lowering:
/// | Slot | Method                                   |
/// |------|------------------------------------------|
/// | 0    | Object.ToString                          |
/// | 1    | Object.Equals                            |
/// | 2    | Object.GetHashCode                       |
/// | 3+   | Class-specific virtual methods           |
///
/// **Safety Checks:**
/// The dispatch function performs runtime validation:
/// - NULL object check → returns NULL
/// - NULL vptr check → returns NULL
/// - Slot bounds check → returns NULL if out of range
///
/// **Thread Safety:**
/// VTable lookups are read-only and thread-safe. The vtable contents are
/// established at class registration and never modified.
///
/// @see rt_type_registry.c For class and vtable registration
/// @see rt_object.c For object allocation and lifecycle
/// @see rt_oop.h For OOP type definitions
///
//===----------------------------------------------------------------------===//

#include "rt_oop.h"

/// @brief Look up a virtual function pointer from an object's vtable.
///
/// Retrieves the function pointer at a specific slot in the object's vtable.
/// This is the core operation for virtual method dispatch. The slot index
/// corresponds to the virtual method's position in the class hierarchy.
///
/// **Dispatch sequence:**
/// ```
/// obj.Method()
///   ↓
/// rt_get_vfunc(obj, METHOD_SLOT)
///   ↓
/// obj->vptr[slot] → function pointer
///   ↓
/// Indirect call through function pointer
/// ```
///
/// **Safety behavior:**
/// - NULL object → returns NULL (no crash)
/// - NULL vptr → returns NULL (uninitialized object)
/// - Unknown class → returns NULL (no bounds check possible)
/// - Out of bounds slot → returns NULL (prevents buffer overread)
///
/// @param obj Object whose vtable is queried. May be NULL.
/// @param slot Zero-based vtable slot index to fetch.
///
/// @return Function pointer at the slot, or NULL on any error condition.
///
/// @note Callers must check for NULL before calling the returned pointer.
/// @note O(1) time complexity (array index lookup).
void *rt_get_vfunc(const rt_object *obj, uint32_t slot)
{
    if (!obj || !obj->vptr)
        return (void *)0;

    // Bounds check: retrieve class info and validate slot index
    const rt_class_info *ci = rt_get_class_info_from_vptr(obj->vptr);
    if (!ci)
        return (void *)0; // Unknown class, cannot validate

    if (slot >= ci->vtable_len)
        return (void *)0; // Out of bounds

    return obj->vptr[slot];
}
