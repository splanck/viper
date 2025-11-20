//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Extern struct, which represents external function
// declarations in IL modules. Externs provide the interface between IL code
// and the runtime library or host environment by declaring foreign functions
// that are defined outside the current module.
//
// External function declarations are required for:
// - Runtime library calls (string operations, math functions, I/O)
// - Host environment integration (platform APIs, system calls)
// - Inter-module references (functions defined in other compilation units)
//
// Each Extern specifies a function signature consisting of a name, return type,
// and parameter type list. The IL verifier ensures that all calls to external
// functions match the declared signature. At link time or runtime, these names
// are resolved to actual implementations in the runtime library or host binary.
//
// Key Responsibilities:
// - Signature declaration: Specifies name, return type, and parameter types
// - Type checking: Enables verifier to check call sites for type correctness
// - Symbol resolution: Provides the name used for linking or runtime lookup
// - ABI compatibility: Documents the calling convention expected by the callee
//
// Common External Functions:
// - String operations: rt_concat, rt_str_len, rt_str_eq
// - Math functions: rt_pow_f64_chkdom, rt_sqrt_chk_f64, rt_abs_i64_chk
// - I/O operations: rt_print_str, rt_print_i64, rt_input_str
// - Memory management: rt_array_alloc, rt_string_from_bytes
//
// Ownership Model:
// - Module owns Extern structs by value in a std::vector
// - Each Extern owns its name string and parameter type vector
// - External declarations persist for the module's lifetime
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Type.hpp"
#include <string>
#include <vector>

namespace il::core
{

/// @brief External function declaration.
struct Extern
{
    /// @brief Identifier of the external function.
    /// @invariant Unique among externs in a module and non-empty.
    /// @ownership String data is owned by this struct and lives for the
    /// lifetime of the parent `Module`.
    std::string name;

    /// @brief Declared return type of the external function.
    /// @invariant Must correspond to the callee's actual ABI; use `void` for no
    /// return value.
    Type retType;

    /// @brief Ordered list of parameter types forming the extern's signature.
    /// @invariant Arity and order must match the target function's signature.
    /// @ownership Vector and contained `Type` values are owned by this struct
    /// for the module's lifetime.
    std::vector<Type> params;
};

} // namespace il::core
