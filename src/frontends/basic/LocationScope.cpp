//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/LocationScope.hpp"
#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

/// @brief Construct a location scope that sets Lowerer's source location.
/// @details Uses the public sourceLocation() accessor instead of direct member
///          access, eliminating the need for LocationScope to be a friend of
///          Lowerer. Saves the previous location for restoration on destruction.
/// @param lowerer The lowerer instance whose source location will be managed.
/// @param loc The new source location to set.
LocationScope::LocationScope(Lowerer &lowerer, il::support::SourceLoc loc)
    : lowerer_(lowerer), previousLoc_(lowerer.sourceLocation())
{
    lowerer_.setSourceLocation(loc);
}

/// @brief Restore the previous source location on scope exit.
LocationScope::~LocationScope()
{
    lowerer_.setSourceLocation(previousLoc_);
}

} // namespace il::frontends::basic
