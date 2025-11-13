//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Module struct, which serves as the top-level container
// for an IL compilation unit. A Module aggregates all externs (runtime function
// declarations), globals (constant data and variables), and function definitions
// that make up a complete program or library.
//
// The Module is the primary abstraction for IL code. It owns all contained
// entities by value through std::vector containers. During compilation, frontends
// construct Module instances using the IRBuilder API, parsers deserialize them
// from IL text files, and backends consume them to generate native code or
// execute in the VM.
//
// Key Responsibilities:
// - Version tracking: Records the IL spec version for format compatibility
// - Target specification: Optional target triple for platform-specific code
// - Name uniqueness: Enforces that function, extern, and global names are unique
// - Ownership: Contains all program entities with clear lifetime semantics
//
// The Module struct is designed for simple value semantics. It can be moved
// efficiently but copying is expensive (deep copy of all contained functions).
// Most code works with Module by reference or pointer.
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
