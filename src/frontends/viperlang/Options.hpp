//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Options.hpp
/// @brief Frontend options controlling ViperLang compilation behavior.
///
//===----------------------------------------------------------------------===//

#pragma once

namespace il::frontends::viperlang
{

/// @brief Options controlling ViperLang compilation behavior.
struct CompilerOptions
{
    /// @brief Enable runtime bounds checks for arrays/collections.
    bool boundsChecks{true};

    /// @brief Enable overflow and arithmetic domain checks.
    bool overflowChecks{true};

    /// @brief Enable null checks for optional access.
    bool nullChecks{true};

    /// @brief Dump AST after parsing (for debugging).
    bool dumpAst{false};

    /// @brief Dump IL after lowering (for debugging).
    bool dumpIL{false};
};

} // namespace il::frontends::viperlang
