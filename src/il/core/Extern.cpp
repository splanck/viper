//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//
// Provides the extension point for il::core::Extern.  Extern declarations are
// currently implemented entirely inline, yet the dedicated translation unit
// ensures downstream components can gain new behaviors (e.g., verifier hooks or
// serialization helpers) without forcing wide recompilation due to header
// changes.
//===----------------------------------------------------------------------===//

#include "il/core/Extern.hpp"

namespace il::core
{

// No out-of-line logic.
} // namespace il::core
