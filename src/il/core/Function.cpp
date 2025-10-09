//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Hosts the out-of-line helpers associated with il::core::Function.  At the
// moment every routine remains inline in the header, yet keeping this
// translation unit in the build tree documents where future functionality—such
// as metadata attachment helpers or verification utilities—should live.  Doing
// so also prevents churn in downstream code that already expects the file to be
// part of the core IL library.
//
//===----------------------------------------------------------------------===//

#include "il/core/Function.hpp"

namespace il::core
{

// No out-of-line methods yet.
} // namespace il::core
