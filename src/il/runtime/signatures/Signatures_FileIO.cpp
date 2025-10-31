//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Signatures_FileIO.cpp
// Purpose: Register expected runtime helper signatures related to console and
//          file I/O for debug validation. This mapping ensures the IL verifier
//          can confirm compiler-emitted calls align with the runtime ABI used to
//          access terminals and file channels.
// Key invariants: Describes the coarse type layout for each runtime symbol in
//                 the I/O subsystem. Parameter kinds reflect the runtime
//                 structure definitions, so updates to the runtime must be
//                 mirrored here.  The registry deliberately retains duplicates
//                 to preserve a full registration log for debugging.
// Ownership/Lifetime: Registered shapes persist for the lifetime of the
//                     process via the shared registry.
// Links: docs/il-guide.md#reference, docs/architecture.md#runtime-signatures

/// @file
/// @brief Runtime signature definitions for console and file I/O helpers.
/// @details Compiler-generated IL frequently touches the runtime's I/O surface
///          area for PRINT/INPUT statements, channel manipulation, and terminal
///          control.  Centralising the signatures in this translation unit keeps
///          the mapping between symbol names and type categories close to the
///          runtime ABI, easing maintenance when the runtime evolves.
//
//===----------------------------------------------------------------------===//

#include "il/runtime/signatures/Registry.hpp"

namespace il::runtime::signatures
{
namespace
{
using Kind = SigParam::Kind;
}

/// @brief Publish expected runtime signature shapes for the file and console
///        subsystem helpers.
/// @details The function walks through logical groupings of runtime helpers and
///          records their parameter/return kinds:
///          - Console printing/input helpers that operate on strings, integers,
///            and floating-point values.
///          - Terminal control routines that manipulate colours, cursor
///            position, and screen clearing.
///          - Channel-based I/O helpers, including open/close primitives and
///            operations that query or mutate file state.
///          - Error-reporting routines that return status codes so BASIC
///            programs can branch on failure.
///          By routing every entry through @ref register_signature the mapping
///          becomes visible to the verification pipeline without requiring each
///          caller to know the registry internals.
void register_fileio_signatures()
{
    register_signature(make_signature("rt_abort", {Kind::Ptr}));
    register_signature(make_signature("rt_print_str", {Kind::Ptr}));
    register_signature(make_signature("rt_print_i64", {Kind::I64}));
    register_signature(make_signature("rt_print_f64", {Kind::F64}));
    register_signature(make_signature("rt_println_i32", {Kind::I32}));
    register_signature(make_signature("rt_println_str", {Kind::Ptr}));
    register_signature(make_signature("rt_input_line", {}, {Kind::Ptr}));
    register_signature(make_signature("rt_term_cls", {}));
    register_signature(make_signature("rt_term_color_i32", {Kind::I32, Kind::I32}));
    register_signature(make_signature("rt_term_locate_i32", {Kind::I32, Kind::I32}));
    register_signature(make_signature("rt_getkey_str", {}, {Kind::Ptr}));
    register_signature(make_signature("rt_inkey_str", {}, {Kind::Ptr}));
    register_signature(
        make_signature("rt_open_err_vstr", {Kind::Ptr, Kind::I32, Kind::I32}, {Kind::I32}));
    register_signature(make_signature("rt_close_err", {Kind::I32}, {Kind::I32}));
    register_signature(make_signature("rt_write_ch_err", {Kind::I32, Kind::Ptr}, {Kind::I32}));
    register_signature(make_signature("rt_println_ch_err", {Kind::I32, Kind::Ptr}, {Kind::I32}));
    register_signature(make_signature("rt_line_input_ch_err", {Kind::I32, Kind::Ptr}, {Kind::I32}));
    register_signature(make_signature("rt_eof_ch", {Kind::I32}, {Kind::I32}));
    register_signature(make_signature("rt_lof_ch", {Kind::I32}, {Kind::I32}));
    register_signature(make_signature("rt_loc_ch", {Kind::I32}, {Kind::I32}));
    register_signature(make_signature("rt_seek_ch_err", {Kind::I32, Kind::I64}, {Kind::I32}));
}

} // namespace il::runtime::signatures
