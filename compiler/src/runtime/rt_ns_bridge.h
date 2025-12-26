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
// Ownership/Lifetime: Objects returned are managed by the runtime object heap
//                     and must be released according to refcounting rules.
// Links: docs/oop.md
//
//===----------------------------------------------------------------------===//

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /// What: Allocate an opaque object instance for Viper.Text.StringBuilder.
    /// Why:  Bridge OOP allocation to the C runtime for use by the VM.
    /// How:  Creates a heap-managed object with appropriate vtable and state.
    void *rt_ns_stringbuilder_new(void);

#ifdef __cplusplus
}
#endif
