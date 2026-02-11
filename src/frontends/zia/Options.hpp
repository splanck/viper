//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Options.hpp
/// @brief Frontend options controlling Zia compilation behavior.
///
/// @details Defines the CompilerOptions struct and OptLevel enum used to
/// configure the Zia compilation pipeline. Options control safety checks
/// (bounds, overflow, null), debug output (AST/IL dumps), and the
/// optimization level applied to generated IL. These options are typically
/// populated from command-line flags by the Viper driver and passed into
/// the Zia Compiler constructor.
///
/// @invariant Default-constructed CompilerOptions enable all safety checks
///            (bounds, overflow, null) and use O0 optimization.
///
/// Ownership/Lifetime: Value type, typically constructed once and passed
/// by const reference through the compilation pipeline.
///
/// @see Compiler.hpp — accepts CompilerOptions to configure compilation.
/// @see Lowerer.hpp — reads options to decide whether to emit safety checks.
///
//===----------------------------------------------------------------------===//

#pragma once

namespace il::frontends::zia
{

/// @brief Optimization level for IL transformations.
enum class OptLevel
{
    O0 = 0, ///< Minimal optimization (simplify-cfg, dce only).
    O1 = 1, ///< Standard optimization (mem2reg, sccp, licm, peephole).
    O2 = 2  ///< Aggressive optimization (includes inlining, gvn, dse).
};

/// @brief Options controlling Zia compilation behavior.
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

    /// @brief Optimization level for IL transformations.
    /// @details O1 is the default, providing a good balance of compilation
    ///          speed and runtime performance. Use O0 for debugging or O2
    ///          for maximum performance.
    OptLevel optLevel{OptLevel::O0};
};

} // namespace il::frontends::zia
