//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Serves as the extension point for il::core::Module.  The class currently
// implements all behavior inline, yet this translation unit communicates where
// multi-function helpers, verification glue, or serialization adapters should
// be added.  Maintaining the stub keeps linkers stable today while giving the
// team freedom to grow the module abstraction without reorganizing headers.
//
//===----------------------------------------------------------------------===//

#include "il/core/Module.hpp"

namespace il::core
{

// No out-of-line logic.
} // namespace il::core
