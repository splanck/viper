//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file provides centralized constants for Viper runtime library function
// names used during IL code generation.
//
// Runtime Function Names:
// When lowering BASIC code to IL, many operations require calls to the Viper
// runtime library. This header defines the canonical names for these runtime
// functions, ensuring consistency between IL code generation and the actual
// runtime library exports.
//
// Name Categories:
// - String operations: rt_concat, rt_str_eq, rt_str_cmp, rt_substring, etc.
// - I/O operations: rt_print, rt_input, rt_read_file, rt_write_file, etc.
// - Array operations: rt_array_alloc, rt_array_get, rt_array_set, etc.
// - Math functions: rt_sin, rt_cos, rt_tan, rt_sqrt, rt_pow, etc.
// - Type conversion: rt_int_to_str, rt_str_to_int, rt_float_to_str, etc.
//
// ABI Contract:
// The names defined in this header MUST match the actual runtime library
// exports exactly. Any mismatch will result in:
// - Link-time errors (undefined symbols)
// - Runtime crashes (calling wrong functions)
// - ABI incompatibility between IL and runtime
//
// Usage:
// During lowering, these constants are used to generate IL calls:
//   builder.call(viper::basic::runtime::Concat, {lhs, rhs})
//
// This ensures:
// - No typos in runtime function names
// - Centralized name management
// - Easy refactoring if runtime API changes
// - Type-safe constant access
//
// Integration:
// - Used by: Lowering helpers (LowerExprBuiltin, LowerStmt_IO, LowerRuntime)
// - Must match: Runtime library exports in src/runtime/
// - Documented in: docs/runtime-api.md (runtime API specification)
//
// Design Notes:
// - Static constexpr constants for zero overhead
// - std::string_view avoids unnecessary allocations
// - Names use consistent prefix convention (rt_)
// - Grouped by functional category for organization
//
//===----------------------------------------------------------------------===//
#pragma once

#include <string_view>

namespace viper::basic::runtime
{

/// String operations
constexpr std::string_view Concat = "rt_concat";
constexpr std::string_view StrEq = "rt_str_eq";
constexpr std::string_view StrLt = "rt_str_lt";
constexpr std::string_view StrLe = "rt_str_le";
constexpr std::string_view StrGt = "rt_str_gt";
constexpr std::string_view StrGe = "rt_str_ge";
constexpr std::string_view StrLen = "rt_str_len";
constexpr std::string_view StrMid = "rt_str_mid";
constexpr std::string_view StrLeft = "rt_str_left";
constexpr std::string_view StrRight = "rt_str_right";
constexpr std::string_view StrUpper = "rt_str_upper";
constexpr std::string_view StrLower = "rt_str_lower";
constexpr std::string_view StrTrim = "rt_str_trim";
constexpr std::string_view StrLTrim = "rt_str_ltrim";
constexpr std::string_view StrRTrim = "rt_str_rtrim";
constexpr std::string_view StrChr = "rt_chr";
constexpr std::string_view StrAsc = "rt_asc";
constexpr std::string_view StrInstr = "rt_instr";
constexpr std::string_view StrStr = "rt_str_str";
constexpr std::string_view StrVal = "rt_val";

/// Math operations
constexpr std::string_view PowF64 = "rt_pow_f64_chkdom";
constexpr std::string_view SqrtF64 = "rt_sqrt_chk_f64";
constexpr std::string_view AbsI64 = "rt_abs_i64_chk";
constexpr std::string_view Sin = "rt_sin";
constexpr std::string_view Cos = "rt_cos";
constexpr std::string_view Tan = "rt_tan";
constexpr std::string_view Atn = "rt_atn";
constexpr std::string_view Log = "rt_log";
constexpr std::string_view Exp = "rt_exp";
constexpr std::string_view Rnd = "rt_rnd";
constexpr std::string_view Randomize = "rt_randomize";
constexpr std::string_view Int = "rt_int";
constexpr std::string_view Fix = "rt_fix";
constexpr std::string_view Sgn = "rt_sgn";

/// Conversion operations
constexpr std::string_view I64ToStr = "rt_i64_to_str";
constexpr std::string_view F64ToStr = "rt_f64_to_str";
constexpr std::string_view StrToI64 = "rt_str_to_i64";
constexpr std::string_view StrToF64 = "rt_str_to_f64";

/// Array operations
constexpr std::string_view ArrI32Alloc = "rt_arr_i32_alloc";
constexpr std::string_view ArrI32Release = "rt_arr_i32_release";
constexpr std::string_view ArrI32Get = "rt_arr_i32_get";
constexpr std::string_view ArrI32Put = "rt_arr_i32_put";

/// String array operations
constexpr std::string_view ArrStrAlloc = "rt_arr_str_alloc";
constexpr std::string_view ArrStrRelease = "rt_arr_str_release";
constexpr std::string_view ArrStrGet = "rt_arr_str_get";
constexpr std::string_view ArrStrPut = "rt_arr_str_put";
constexpr std::string_view ArrStrLen = "rt_arr_str_len";

/// IO operations
constexpr std::string_view Print = "rt_print";
constexpr std::string_view PrintLn = "rt_print_ln";
constexpr std::string_view Input = "rt_input";
constexpr std::string_view InputLine = "rt_input_line";
constexpr std::string_view FileOpen = "rt_file_open";
constexpr std::string_view FileClose = "rt_file_close";
constexpr std::string_view FileRead = "rt_file_read";
constexpr std::string_view FileWrite = "rt_file_write";
constexpr std::string_view FileEof = "rt_file_eof";
constexpr std::string_view Inkey = "rt_inkey";

/// Error handling
constexpr std::string_view ErrorClear = "rt_error_clear";
constexpr std::string_view ErrorGet = "rt_error_get";
constexpr std::string_view ErrorSet = "rt_error_set";

/// Misc operations
constexpr std::string_view Sleep = "rt_sleep";
constexpr std::string_view Timer = "rt_timer";

} // namespace viper::basic::runtime
