// File: src/il/core/Param.hpp
// Purpose: Defines parameter representation for functions and blocks.
// Key invariants: Type matches associated signature or block.
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
    unsigned id = 0; ///< Value identifier assigned during IR construction
};

} // namespace il::core
