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
// Links: docs/internals/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

namespace il::support {

/// @brief Holds global command-line settings that influence compiler behavior.
/// These options control tracing, verification, and target architecture.
/// @invariant Flags are independent booleans.
/// @ownership Value type.
struct Options {
    /// @brief Default target selector used when callers do not choose a backend target.
    /// @details The support-level options type is intentionally generic and does
    ///          not own backend-specific target triples.  The value "host" lets
    ///          downstream command layers resolve the actual platform/architecture
    ///          through their cross-platform capability adapters.
    static constexpr std::string_view kDefaultTarget = "host";

    /// @brief Enable verbose tracing of compilation steps.
    bool trace = false;

    /// @brief Run the IL verifier before execution or code generation.
    bool verify = true;

    /// @brief Target architecture triple for code generation.
    std::string target = std::string{kDefaultTarget};
};
} // namespace il::support
