//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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
//                 checks remain sound.
// Ownership/Lifetime: Registered metadata persists for the lifetime of the
//                     process via the shared registry.
// Links: docs/il-guide.md#reference
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
/// @details The registration feeds the global @ref signatures::Registry so the
///          verifier can compare call sites against the runtime ABI. Each entry
///          specifies the parameter and result kinds consumed by helpers that
///          allocate, retain, release, or index into BASIC arrays and
///          heap-allocated objects.
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
    register_signature(make_signature("rt_arr_oob_panic", {Kind::I64, Kind::I64}));
    register_signature(make_signature("rt_obj_new_i64", {Kind::I64, Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_obj_retain_maybe", {Kind::Ptr}));
    register_signature(make_signature("rt_obj_release_check0", {Kind::Ptr}, {Kind::I1}));
    register_signature(make_signature("rt_obj_free", {Kind::Ptr}));
}

} // namespace il::runtime::signatures

