//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/zia/ZiaLocationScope.cpp
// Purpose: RAII guard that sets/restores source location on the Zia lowerer.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/ZiaLocationScope.hpp"
#include "frontends/zia/Lowerer.hpp"

namespace il::frontends::zia {

ZiaLocationScope::ZiaLocationScope(Lowerer &lowerer, il::support::SourceLoc loc)
    : lowerer_(lowerer), previousLoc_(lowerer.sourceLocation()) {
    lowerer_.setSourceLocation(loc);
}

ZiaLocationScope::~ZiaLocationScope() {
    lowerer_.setSourceLocation(previousLoc_);
}

} // namespace il::frontends::zia
