//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_ns_bridge.h
// Purpose: Prototypes for runtime namespace bridging helpers.
// Key invariants: Constructors return heap-managed, refcounted object pointers
//                 with vptr at offset 0.
// Ownership: Objects returned are managed by the runtime object heap
//            and must be released according to refcounting rules.
// Lifetime: Returned objects live until their reference count reaches zero.
// Links: docs/oop.md, rt_heap.h
//
//===----------------------------------------------------------------------===//

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Allocate an opaque object instance for Viper.Text.StringBuilder.
    /// @details Bridges OOP allocation to the C runtime for use by the VM.
    ///          Creates a heap-managed object with the appropriate vtable and
    ///          internal state (an embedded rt_string_builder).
    /// @return Opaque pointer to the new StringBuilder object; NULL on failure.
    void *rt_ns_stringbuilder_new(void);

#ifdef __cplusplus
}
#endif
