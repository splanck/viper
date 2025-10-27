//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Signatures_Math.cpp
// Purpose: Register expected runtime helper signatures for numeric conversion
//          and math routines to support debug validation.
// Key invariants: Entries cover helpers that operate purely on numeric data
//                 without string dependencies.
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

/// @brief Publish expected runtime signature shapes for math-related helpers.
void register_math_signatures()
{
    register_signature(make_signature("rt_cint_from_double", {Kind::F64, Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_clng_from_double", {Kind::F64, Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_csng_from_double", {Kind::F64, Kind::Ptr}, {Kind::F64}));
    register_signature(make_signature("rt_cdbl_from_any", {Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_int_floor", {Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_fix_trunc", {Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_round_even", {Kind::F64, Kind::I32}, {Kind::F64}));
    register_signature(make_signature("rt_sqrt", {Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_abs_i64", {Kind::I64}, {Kind::I64}));
    register_signature(make_signature("rt_abs_f64", {Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_floor", {Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_ceil", {Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_sin", {Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_cos", {Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_pow_f64_chkdom", {Kind::F64, Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_randomize_i64", {Kind::I64}));
    register_signature(make_signature("rt_rnd", {}, {Kind::F64}));
}

} // namespace il::runtime::signatures

