//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/options.hpp
// Purpose: Declares command-line option parsing helpers. 
// Key invariants: None.
// Ownership/Lifetime: Caller owns option values.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace il::support
{

/// @brief Holds global command-line settings that influence compiler behavior.
/// These options control tracing, verification, and target architecture.
/// @invariant Flags are independent booleans.
/// @ownership Value type.
struct Options
{
    /// @brief Enable verbose tracing of compilation steps.
    bool trace = false;

    /// @brief Run the IL verifier before execution or code generation.
    bool verify = true;

    /// @brief Target architecture triple for code generation.
    std::string target = "x86_64";
};
} // namespace il::support
