// File: src/il/core/Param.hpp
// Purpose: Defines parameter representation for functions and blocks.
// Key invariants: Type matches its declaration and @p id is unique within function.
// Ownership/Lifetime: Parameters stored by value.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Type.hpp"
#include <string>

namespace il::core
{

/// @brief Parameter for functions and basic blocks.
struct Param
{
    std::string name; ///< Parameter name
    Type type;        ///< Parameter type
    unsigned id = 0;  ///< SSA value id assigned during construction
};

} // namespace il::core
