// File: src/il/core/Param.hpp
// Purpose: Defines function parameter representation.
// Key invariants: Type matches function signature.
// Ownership/Lifetime: Parameters stored by value.
// Links: docs/il-spec.md
#pragma once
#include "il/core/Type.hpp"
#include <string>

namespace il::core
{

/// @brief Function parameter.
struct Param
{
    std::string name;
    Type type;
};

} // namespace il::core
