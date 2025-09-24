// File: src/il/core/Param.hpp
// Purpose: Defines parameter representation for functions and blocks.
// Key invariants: Type matches associated signature or block.
// Ownership/Lifetime: Parameters stored by value.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Type.hpp"
#include <string>

namespace il::core
{

/// @brief Describes a function or basic block parameter.
/// Holds the metadata required to reference and type-check a parameter.
struct Param
{
    /// @brief Name used for diagnostics and debugging.
    /// Owned by the Param and stored by value; may be empty for unnamed parameters.
    std::string name;

    /// @brief Static type of the parameter.
    /// Owned by the Param and stored by value.
    /// Must match the containing function or block signature.
    Type type;

    /// @brief Ordinal identifier assigned during IR construction.
    /// Unique within its parent function or block; defaults to 0 before assignment.
    unsigned id = 0;
};

} // namespace il::core
