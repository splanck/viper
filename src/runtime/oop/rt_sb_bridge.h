//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/oop/rt_sb_bridge.h
// Purpose: Prototypes for the StringBuilder bridge that creates a heap-managed,
// reference-counted StringBuilder object for Viper.Text.StringBuilder.
//
// Key invariants:
//   - Constructors return heap-managed, reference-counted object pointers.
//   - The vptr field is always at offset 0 in every returned object.
//   - All returned objects start with refcount 1.
//   - This header is used internally by the runtime to bridge namespace constructors.
//
// Ownership/Lifetime:
//   - Objects returned are managed by the runtime object heap.
//   - Callers must release objects according to refcounting rules.
//
// Links: src/runtime/oop/rt_sb_bridge.c (implementation), src/runtime/core/rt_heap.h
//
//===----------------------------------------------------------------------===//
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Allocate an opaque object instance for Viper.Text.StringBuilder.
/// @details Bridges OOP allocation to the C runtime for use by the VM.
///          Creates a heap-managed object with the appropriate vtable and
///          internal state (an embedded rt_string_builder).
/// @return Opaque pointer to the new StringBuilder object; NULL on failure.
void *rt_sb_new(void);

#ifdef __cplusplus
}
#endif
