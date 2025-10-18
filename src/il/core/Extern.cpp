//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Maintains the out-of-line extension point for il::core::Extern, the IL node
// describing externally provided symbols.  The class is currently header-only,
// but reserving this translation unit documents where serialization, verifier,
// or metadata helpers should live once the type grows auxiliary behaviour.  The
// file exists to keep downstream build dependencies stable when that happens.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Placeholder translation unit for `il::core::Extern` helpers.
/// @details `Extern` is presently implemented entirely inline.  Retaining this
///          file keeps the extension point visible and avoids include churn when
///          future helper routines gain out-of-line definitions.

#include "il/core/Extern.hpp"

namespace il::core
{

// Intentionally empty – see file header for rationale.

} // namespace il::core

