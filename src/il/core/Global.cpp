//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//
// Hosts future out-of-line helpers for il::core::Global.  Globals today expose
// only inline utilities, but keeping this translation unit in the build allows
// the IR library to grow without disturbing includers or project structure when
// richer behavior (e.g., metadata materialization) becomes necessary.
//===----------------------------------------------------------------------===//

#include "il/core/Global.hpp"

namespace il::core
{

// No out-of-line logic.
} // namespace il::core
