//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/zia/ZiaLocationScope.hpp
// Purpose: RAII helper for managing source location context in Zia Lowerer.
// Key invariants: Restores previous location on scope exit.
// Ownership/Lifetime: Stack-based RAII, non-copyable, non-movable.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/source_location.hpp"

namespace il::frontends::zia
{

class Lowerer;

/// @brief RAII helper to set and restore source location context in Lowerer.
/// @details Automatically sets Lowerer::curLoc_ to a new location on
///          construction and restores the previous location on destruction.
/// @invariant Restores original location on scope exit.
class ZiaLocationScope
{
  public:
    ZiaLocationScope(Lowerer &lowerer, il::support::SourceLoc loc);
    ~ZiaLocationScope();

    ZiaLocationScope(const ZiaLocationScope &) = delete;
    ZiaLocationScope &operator=(const ZiaLocationScope &) = delete;
    ZiaLocationScope(ZiaLocationScope &&) = delete;
    ZiaLocationScope &operator=(ZiaLocationScope &&) = delete;

  private:
    Lowerer &lowerer_;
    il::support::SourceLoc previousLoc_;
};

} // namespace il::frontends::zia
