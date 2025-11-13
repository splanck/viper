//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Signatures_Strings.cpp
// Purpose: Register expected runtime helper signatures for string processing
//          and textual conversions used by debug validation.
// Key invariants: Entries cover helpers that operate on runtime string values
//                 or provide textual conversions.  The table mixes pure string
//                 manipulations with bridge routines that convert between
//                 textual and numeric representations; both categories are kept
//                 together because they share reference-counted string handles.
// Ownership/Lifetime: Registered metadata persists via the shared registry for
//                     the life of the process.
// Links: docs/il-guide.md#reference, docs/architecture.md#runtime-signatures
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Runtime signature definitions for string-related helpers.
/// @details Centralises every string-oriented runtime symbol so that
///          verification tooling can register the expected parameter/return
///          shapes in one location.  The comments explain the breadth of
///          coverage—from allocation helpers through parsing/conversion—to make
///          future maintenance straightforward.

#include "il/runtime/signatures/Registry.hpp"

namespace il::runtime::signatures
{
namespace
{
using Kind = SigParam::Kind;
}

/// @brief Publish expected runtime signature shapes for string-related helpers.
/// @details The registration sweep intentionally interleaves related helpers so
///          maintainers can see at a glance how the runtime surface area maps to
///          IL-visible contracts:
///          - Fundamental allocation utilities produce or retain the reference-
///            counted runtime string structure.
///          - Substring and trimming helpers operate on positional arguments and
///            return fresh handles.
///          - Comparison and search routines expose boolean or index results
///            while consuming string handles by pointer.
///          - Conversions to and from numeric types handle parsing failures via
///            out-parameters and status codes.
///          Each call to @ref register_signature simply appends metadata to the
///          global registry; consumers that snapshot the registry after
///          registration obtain a process-wide view of the runtime string ABI.
void register_string_signatures()
{
    register_signature(make_signature("rt_len", {Kind::Ptr}, {Kind::I64}, true, true));
    register_signature(make_signature("rt_substr", {Kind::Ptr, Kind::I64, Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_trap", {Kind::Ptr}));
    register_signature(make_signature("rt_concat", {Kind::Ptr, Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_csv_quote_alloc", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(
        make_signature("rt_split_fields", {Kind::Ptr, Kind::Ptr, Kind::I64}, {Kind::I64}));
    register_signature(make_signature("rt_to_int", {Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_to_double", {Kind::Ptr}, {Kind::F64}));
    register_signature(make_signature("rt_parse_int64", {Kind::Ptr, Kind::Ptr}, {Kind::I32}));
    register_signature(make_signature("rt_parse_double", {Kind::Ptr, Kind::Ptr}, {Kind::I32}));
    register_signature(make_signature("rt_int_to_str", {Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_f64_to_str", {Kind::F64}, {Kind::Ptr}));
    register_signature(make_signature("rt_str_i16_alloc", {Kind::I32}, {Kind::Ptr}));
    register_signature(make_signature("rt_str_i32_alloc", {Kind::I32}, {Kind::Ptr}));
    register_signature(make_signature("rt_str_f_alloc", {Kind::F64}, {Kind::Ptr}));
    register_signature(make_signature("rt_str_d_alloc", {Kind::F64}, {Kind::Ptr}));
    register_signature(make_signature("rt_str_empty", {}, {Kind::Ptr}));
    register_signature(make_signature("rt_const_cstr", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_str_retain_maybe", {Kind::Ptr}));
    register_signature(make_signature("rt_str_release_maybe", {Kind::Ptr}));
    register_signature(make_signature("rt_left", {Kind::Ptr, Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_right", {Kind::Ptr, Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_mid2", {Kind::Ptr, Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_mid3", {Kind::Ptr, Kind::I64, Kind::I64}, {Kind::Ptr}));
    register_signature(
        make_signature("rt_instr2", {Kind::Ptr, Kind::Ptr}, {Kind::I64}, true, true));
    register_signature(
        make_signature("rt_instr3", {Kind::I64, Kind::Ptr, Kind::Ptr}, {Kind::I64}, true, true));
    register_signature(make_signature("rt_ltrim", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_rtrim", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_trim", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_ucase", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_lcase", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_chr", {Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_asc", {Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_str_eq", {Kind::Ptr, Kind::Ptr}, {Kind::I1}, true, true));
    register_signature(make_signature("rt_str_lt", {Kind::Ptr, Kind::Ptr}, {Kind::I1}, true, true));
    register_signature(make_signature("rt_str_le", {Kind::Ptr, Kind::Ptr}, {Kind::I1}, true, true));
    register_signature(make_signature("rt_str_gt", {Kind::Ptr, Kind::Ptr}, {Kind::I1}, true, true));
    register_signature(make_signature("rt_str_ge", {Kind::Ptr, Kind::Ptr}, {Kind::I1}, true, true));
    register_signature(make_signature("rt_val", {Kind::Ptr}, {Kind::F64}));
    register_signature(make_signature("rt_val_to_double", {Kind::Ptr, Kind::Ptr}, {Kind::F64}));
    register_signature(make_signature("rt_string_cstr", {Kind::Ptr}, {Kind::Ptr}));
}

} // namespace il::runtime::signatures
