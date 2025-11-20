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

LocationScope::LocationScope(Lowerer &lowerer, il::support::SourceLoc loc)
    : lowerer_(lowerer), previousLoc_(lowerer.curLoc)
{
    lowerer_.curLoc = loc;
}

LocationScope::~LocationScope()
{
    lowerer_.curLoc = previousLoc_;
}

} // namespace il::frontends::basic
