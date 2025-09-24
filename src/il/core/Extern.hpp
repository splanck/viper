// File: src/il/core/Extern.hpp
// Purpose: Represents external function declarations in IL modules.
// Key invariants: Parameter count matches signature.
// Ownership/Lifetime: Module owns extern declarations.
// Links: docs/il-guide.md#reference
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
