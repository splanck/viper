//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Signatures_Arrays.cpp
// Purpose: Register expected runtime helper signatures for array and object
//          memory management to support debug validation. By centralising the
//          declarations here, the IL verifier can guarantee that generated call
//          sites match the runtime ABI used by the BASIC front end.
// Key invariants: Entries describe helpers that manipulate heap storage for
//                 arrays or reference-counted objects. Signature metadata must
//                 stay in sync with the runtime C implementations so verifier
//                 checks remain sound.  The registration function is idempotent
//                 with respect to observable behaviour; calling it multiple
//                 times appends duplicate entries without mutating prior
//                 snapshots.
// Ownership/Lifetime: Registered metadata persists for the lifetime of the
//                     process via the shared registry.
// Links: docs/il-guide.md#reference, docs/architecture.md#runtime-signatures

/// @file
/// @brief Runtime signature definitions for array and object helpers.
/// @details The BASIC runtime exposes a suite of allocation, retention, and
///          bounds-checking utilities for heap-managed containers.  This file
///          enumerates the corresponding IL-facing signatures so verifier code
///          can ensure compiler-emitted calls pass the correct argument counts
///          and value categories.  Documenting the categories in one translation
///          unit keeps maintenance straightforward when the runtime evolves.
//
//===----------------------------------------------------------------------===//

#include "il/runtime/signatures/Registry.hpp"

namespace il::runtime::signatures
{
namespace
{
using Kind = SigParam::Kind;
}

/// @brief Publish expected runtime signature shapes for array/object helpers.
/// @details The registration proceeds in themed batches that mirror the
///          lifecycle of heap-managed containers:
///          - Allocation helpers return payload pointers sized according to the
///            requested length or capacity.
///          - Retain/release routines manage reference counts so the compiler
///            can emit balanced calls when values escape or die.
///          - Accessors encapsulate bounds checking and metadata queries for
///            array length, indexing, and resizing.
///          - Object helpers cover the runtime's boxed-object support, ensuring
///            IL-level code can interoperate with reference-counted handles.
///          Each call funnels through @ref register_signature, appending a
///          @ref Signature entry that downstream verification utilities inspect
///          when validating call sites.
void register_array_signatures()
{
    register_signature(make_signature("rt_alloc", {Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_arr_i32_new", {Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_arr_i32_retain", {Kind::Ptr}));
    register_signature(make_signature("rt_arr_i32_release", {Kind::Ptr}));
    register_signature(make_signature("rt_arr_i32_len", {Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_arr_i32_get", {Kind::Ptr, Kind::I64}, {Kind::I64}));
    register_signature(make_signature("rt_arr_i32_set", {Kind::Ptr, Kind::I64, Kind::I64}));
    register_signature(make_signature("rt_arr_i32_resize", {Kind::Ptr, Kind::I64}, {Kind::Ptr}));
    // I64 array operations (for LONG arrays in BASIC)
    register_signature(make_signature("rt_arr_i64_new", {Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_arr_i64_retain", {Kind::Ptr}));
    register_signature(make_signature("rt_arr_i64_release", {Kind::Ptr}));
    register_signature(make_signature("rt_arr_i64_len", {Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_arr_i64_get", {Kind::Ptr, Kind::I64}, {Kind::I64}));
    register_signature(make_signature("rt_arr_i64_set", {Kind::Ptr, Kind::I64, Kind::I64}));
    register_signature(make_signature("rt_arr_i64_resize", {Kind::Ptr, Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_arr_oob_panic", {Kind::I64, Kind::I64}));
    register_signature(make_signature("rt_obj_new_i64", {Kind::I64, Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_obj_retain_maybe", {Kind::Ptr}));
    register_signature(make_signature("rt_obj_release_check0", {Kind::Ptr}, {Kind::I1}));
    register_signature(make_signature("rt_obj_free", {Kind::Ptr}));
    register_signature(make_signature("rt_obj_class_id", {Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_heap_mark_disposed", {Kind::Ptr}, {Kind::I1}));

    // String array operations
    register_signature(make_signature("rt_arr_str_alloc", {Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_arr_str_release", {Kind::Ptr, Kind::I64}));
    // String array element access: string parameters map to Ptr at the ABI level
    // (mapToSigParamKind(Str) -> Ptr), so use Ptr here for validation.
    register_signature(make_signature("rt_arr_str_get", {Kind::Ptr, Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_arr_str_put", {Kind::Ptr, Kind::I64, Kind::Ptr}));
    register_signature(make_signature("rt_arr_str_len", {Kind::Ptr}, {Kind::I64}));
}

} // namespace il::runtime::signatures
