//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/core/Module.hpp
// Purpose: Declares the Module struct -- the top-level container for an IL
//          compilation unit aggregating externs, globals, and function
//          definitions. Tracks version and optional target triple.
// Key invariants:
//   - Function, extern, and global names must be unique within the module.
//   - version defaults to VIPER_IL_VERSION_STR for new modules.
// Ownership/Lifetime: Module owns all contained entities by value through
//          std::vector containers. Movable efficiently; copying is expensive
//          (deep copy of all functions). Most code works with Module by
//          reference or pointer.
// Links: docs/il-guide.md#reference, il/core/Extern.hpp,
//        il/core/Function.hpp, il/core/Global.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "viper/version.hpp"
#include <optional>
#include <string>
#include <vector>

namespace il::core
{

/// @brief IL module aggregating externs, globals, and functions.
struct Module
{
    /// @brief Module format version string.
    ///
    /// Defaults to configured IL spec version for newly constructed modules and
    /// may be overwritten by parsers when reading serialized IL.
    std::string version = VIPER_IL_VERSION_STR;

    /// @brief Optional target triple directive associated with the module.
    ///
    /// Absent by default; populated when a `target "triple"` directive is
    /// encountered during parsing or assigned programmatically.
    std::optional<std::string> target;

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
