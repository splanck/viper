//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/core/Extern.hpp
// Purpose: Declares the Extern struct -- external function declarations in IL
//          modules. Provides the interface between IL code and the runtime
//          library or host environment by declaring foreign function signatures
//          (name, return type, parameter types) for type-checked call sites.
// Key invariants:
//   - Extern names must be unique among externs in a module and non-empty.
//   - Arity and types must match the target function's actual signature.
// Ownership/Lifetime: Module owns Extern structs by value in a std::vector.
//          Each Extern owns its name string and parameter type vector.
//          External declarations persist for the module's lifetime.
// Links: docs/il-guide.md#reference, il/core/Type.hpp
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
