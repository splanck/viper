// File: src/il/core/Param.hpp
// Purpose: Defines function and block parameter representation.
// Key invariants: Type matches defining signature; id is unique within a function.
// Ownership/Lifetime: Parameters stored by value.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Type.hpp"
#include <string>

namespace il::core
{

/// @brief Function or block parameter.
struct Param
{
    std::string name;
    Type type;
    unsigned id{0}; ///< SSA value id within the function
};

} // namespace il::core
