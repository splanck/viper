//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/signatures/Signatures_FileIO.cpp
// Purpose: Register expected runtime helper signatures related to console and
//          file I/O for debug validation.
// Key invariants: Describes the coarse type layout for each runtime symbol in
//                 the I/O subsystem.
// Ownership/Lifetime: Registered shapes persist for the lifetime of the
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

/// @brief Publish expected runtime signature shapes for the file and console
///        subsystem helpers.
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
    register_signature(make_signature("rt_open_err_vstr",
                                      {Kind::Ptr, Kind::I32, Kind::I32},
                                      {Kind::I32}));
    register_signature(make_signature("rt_close_err", {Kind::I32}, {Kind::I32}));
    register_signature(make_signature("rt_write_ch_err",
                                      {Kind::I32, Kind::Ptr},
                                      {Kind::I32}));
    register_signature(make_signature("rt_println_ch_err",
                                      {Kind::I32, Kind::Ptr},
                                      {Kind::I32}));
    register_signature(make_signature("rt_line_input_ch_err",
                                      {Kind::I32, Kind::Ptr},
                                      {Kind::I32}));
    register_signature(make_signature("rt_eof_ch", {Kind::I32}, {Kind::I32}));
    register_signature(make_signature("rt_lof_ch", {Kind::I32}, {Kind::I32}));
    register_signature(make_signature("rt_loc_ch", {Kind::I32}, {Kind::I32}));
    register_signature(make_signature("rt_seek_ch_err",
                                      {Kind::I32, Kind::I64},
                                      {Kind::I32}));
}

} // namespace il::runtime::signatures

