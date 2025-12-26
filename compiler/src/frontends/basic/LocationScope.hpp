//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/LocationScope.hpp
// Purpose: RAII helper for managing source location context in Lowerer.
// Key invariants: Restores previous location on scope exit.
// Ownership/Lifetime: Stack-based RAII, non-copyable, non-movable.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/source_location.hpp"

namespace il::frontends::basic
{

class Lowerer;

/// @brief RAII helper to set and restore source location context in Lowerer.
/// @details Automatically sets Lowerer::curLoc to a new location on construction
///          and restores the previous location on destruction. This eliminates
///          manual curLoc assignments throughout lowering visitor methods.
/// @invariant Restores original location on scope exit.
/// @ownership Stack-based RAII; does not transfer ownership of Lowerer.
///
/// Usage example:
/// @code
/// void Lowerer::visit(const BeepStmt &s) {
///     LocationScope loc(*this, s.loc);
///     // curLoc is now set to s.loc
///     requestHelper(RuntimeFeature::TermBell);
///     emitCallRet(Type(Type::Kind::Void), "rt_bell", {});
/// } // curLoc is automatically restored here
/// @endcode
class LocationScope
{
  public:
    /// @brief Construct a location scope that sets Lowerer::curLoc.
    /// @param lowerer The lowerer instance whose curLoc will be managed.
    /// @param loc The new source location to set.
    LocationScope(Lowerer &lowerer, il::support::SourceLoc loc);

    /// @brief Restore the previous source location.
    ~LocationScope();

    // Delete copy and move operations
    LocationScope(const LocationScope &) = delete;
    LocationScope &operator=(const LocationScope &) = delete;
    LocationScope(LocationScope &&) = delete;
    LocationScope &operator=(LocationScope &&) = delete;

  private:
    Lowerer &lowerer_;                   ///< Reference to the lowerer
    il::support::SourceLoc previousLoc_; ///< Location to restore on destruction
};

} // namespace il::frontends::basic
