//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Signatures_Math.cpp
// Purpose: Register expected runtime helper signatures for numeric conversion
//          and math routines to support debug validation.
// Key invariants: Entries cover helpers that operate purely on numeric data
//                 without string dependencies.  The table intentionally mirrors
//                 the runtime's categorisation—conversion, rounding, pure math,
//                 and pseudo-randomness—so new helpers slot naturally into the
//                 appropriate section.
// Ownership/Lifetime: Registered metadata persists for the lifetime of the
//                     process via the shared registry.
// Links: docs/il-guide.md#reference, docs/architecture.md#runtime-signatures

/// @file
/// @brief Runtime signature definitions for numeric helpers.
/// @details BASIC's numeric library spans conversion between integer and
///          floating-point types, general math functions, and pseudo-random
///          number generation.  This translation unit documents the expected
///          parameter/return kinds for those runtime hooks so verification code
///          can validate generated call sites without understanding the runtime
///          implementation details.
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
/// @details The registration routine records signatures in four logical
///          segments:
///          1. Conversion helpers that translate doubles into integer formats
///             while reporting overflow via out-parameters.
///          2. General-purpose rounding utilities that mirror BASIC semantics
///             for INT, FIX, and ROUND.
///          3. Core transcendental functions (ABS, SQRT, SIN, COS, POW) that
///             operate purely on floating-point values.
///          4. Pseudo-random number generation entry points for RANDOMIZE and
///             RND.
///          Ordering the registrations this way keeps the intent readable and
///          simplifies diffing when new helpers join a category.  Each call to
///          @ref register_signature appends metadata consumed later by runtime
///          verification passes.
void register_math_signatures()
{
    register_signature(make_signature("rt_cint_from_double", {Kind::F64, Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_clng_from_double", {Kind::F64, Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_csng_from_double", {Kind::F64, Kind::Ptr}, {Kind::F64}));
    register_signature(
        make_signature("rt_cdbl_from_any", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_int_floor", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_fix_trunc", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(
        make_signature("rt_round_even", {Kind::F64, Kind::I32}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_sqrt", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_abs_i64", {Kind::I64}, {Kind::I64}));
    register_signature(make_signature("rt_abs_f64", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_floor", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_ceil", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_sin", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_cos", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_tan", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_atan", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_exp", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_log", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_sgn_i64", {Kind::I64}, {Kind::I64}, true, false, true));
    register_signature(make_signature("rt_sgn_f64", {Kind::F64}, {Kind::F64}, true, false, true));
    register_signature(make_signature("rt_pow_f64_chkdom", {Kind::F64, Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_pow_f64", {Kind::F64, Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_math_pow", {Kind::F64, Kind::F64}, {Kind::F64}));
    register_signature(make_signature("rt_randomize_i64", {Kind::I64}));
    register_signature(make_signature("rt_rnd", {}, {Kind::F64}));
}

} // namespace il::runtime::signatures
