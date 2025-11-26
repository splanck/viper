//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ProcedureSymbolTracker.hpp
// Purpose: Centralizes symbol usage tracking for procedure-level lowering.
// Key invariants: All symbol tracking goes through this helper to avoid
//                 duplicated logic in VarCollectWalker and RuntimeNeedsScanner.
// Ownership/Lifetime: Operates on borrowed Lowerer state; does not own data.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <string_view>

namespace il::frontends::basic
{

class Lowerer;
class SemanticAnalyzer;

/// @brief Centralizes symbol usage tracking during procedure lowering.
/// @details Provides a unified API for recording symbol references, array usage,
///          and cross-procedure global tracking. This avoids duplicated logic
///          between VarCollectWalker (variable discovery) and RuntimeNeedsScanner
///          (runtime helper tracking).
///
/// Key responsibilities:
/// - Recording symbol usage (scalar vs array)
/// - Marking cross-procedure global usage for runtime-backed storage
/// - Checking field scope to skip class members
/// - Enforcing module-level symbol sharing rules
///
/// NOTE: Currently all referenced variables get local slots.
/// TODO: Skip allocation for module-level globals once IL supports mutable
///       module-scope globals (beyond string constants). This is about IL
///       representation, not OOP runtime capability.
class ProcedureSymbolTracker
{
  public:
    /// @brief Construct a tracker bound to the lowering context.
    /// @param lowerer Owning lowering driver whose symbol tables are updated.
    /// @param trackCrossProc If true, marks module-level symbols used outside
    ///        @main as cross-procedure globals. Should be true for variable
    ///        collection, false for runtime needs scanning.
    explicit ProcedureSymbolTracker(Lowerer &lowerer, bool trackCrossProc = true) noexcept;

    /// @brief Check if a symbol name should be skipped (empty or field in scope).
    /// @param name Symbol name to check.
    /// @return True if the symbol should be skipped from tracking.
    [[nodiscard]] bool shouldSkip(std::string_view name) const;

    /// @brief Record usage of a scalar variable.
    /// @details Marks the symbol as referenced and optionally checks for
    ///          cross-procedure global usage when outside @main.
    /// @param name Variable name to track.
    void trackScalar(std::string_view name);

    /// @brief Record usage of an array variable.
    /// @details Marks the symbol as both referenced and an array, and optionally
    ///          checks for cross-procedure global usage when outside @main.
    /// @param name Array name to track.
    void trackArray(std::string_view name);

    /// @brief Record usage of a variable that may be scalar or array.
    /// @details Unified entry point that marks referenced and optionally array.
    /// @param name Variable name to track.
    /// @param isArray True if the variable is used with array semantics.
    void track(std::string_view name, bool isArray);

    /// @brief Check and mark cross-procedure global usage if applicable.
    /// @details Called when a symbol is referenced outside @main to record
    ///          that it needs runtime-backed storage for sharing.
    /// @param name Symbol name to check.
    void trackCrossProcGlobalIfNeeded(std::string_view name);

  private:
    /// @brief Check if currently lowering the @main function.
    [[nodiscard]] bool isInMain() const;

    Lowerer &lowerer_;
    bool trackCrossProc_;
};

} // namespace il::frontends::basic
