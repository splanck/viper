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
//                 or provide textual conversions.
// Ownership/Lifetime: Registered metadata persists via the shared registry for
//                     the life of the process.
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

/// @brief Publish expected runtime signature shapes for string-related helpers.
/// @details Populates the signature registry with all string-processing runtime
///          functions.  Grouping the registrations in a single function keeps
///          the mapping between runtime symbol names and their parameter/return
///          contracts easy to audit.  The collection spans pure string utilities
///          (length, slicing, trimming), conversions, and retain/release hooks so
///          debugger tooling can validate every call site uniformly.
void register_string_signatures()
{
    register_signature(make_signature("rt_len", {Kind::Ptr}, {Kind::I64}));
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
    register_signature(make_signature("rt_instr2", {Kind::Ptr, Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_instr3", {Kind::I64, Kind::Ptr, Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_ltrim", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_rtrim", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_trim", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_ucase", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_lcase", {Kind::Ptr}, {Kind::Ptr}));
    register_signature(make_signature("rt_chr", {Kind::I64}, {Kind::Ptr}));
    register_signature(make_signature("rt_asc", {Kind::Ptr}, {Kind::I64}));
    register_signature(make_signature("rt_str_eq", {Kind::Ptr, Kind::Ptr}, {Kind::I1}));
    register_signature(make_signature("rt_val", {Kind::Ptr}, {Kind::F64}));
    register_signature(make_signature("rt_val_to_double", {Kind::Ptr, Kind::Ptr}, {Kind::F64}));
    register_signature(make_signature("rt_string_cstr", {Kind::Ptr}, {Kind::Ptr}));
}

} // namespace il::runtime::signatures
