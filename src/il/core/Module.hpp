// File: src/il/core/Module.hpp
// Purpose: Container for IL externs, globals, and functions.
// Key invariants: Names are unique within a module.
// Ownership/Lifetime: Module owns contained objects by value.
// Links: docs/il-spec.md
#pragma once
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include <vector>

namespace il::core
{

/// @brief IL module aggregating externs, globals, and functions.
struct Module
{
    std::vector<Extern> externs;
    std::vector<Global> globals;
    std::vector<Function> functions;
};

} // namespace il::core
