// File: src/il/core/Module.hpp
// Purpose: Container for IL externs, globals, and functions.
// Key invariants: Names are unique within a module.
// Ownership/Lifetime: Module owns contained objects by value.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include <string>
#include <vector>

namespace il::core
{

/// @brief IL module aggregating externs, globals, and functions.
struct Module
{
    /// @brief Module format version string.
    ///
    /// Defaults to "0.1.2" for newly constructed modules and may be
    /// overwritten by parsers when reading serialized IL.
    std::string version = "0.1.2";

    /// @brief Declared external functions available to the module.
    ///
    /// Starts empty and is populated as extern declarations are processed.
    std::vector<Extern> externs;

    /// @brief Global variable declarations.
    ///
    /// Initialized empty; parsers append entries for each global encountered.
    std::vector<Global> globals;

    /// @brief Function definitions contained in the module.
    ///
    /// Begins empty and receives entries as functions are defined or parsed.
    std::vector<Function> functions;
};

} // namespace il::core
