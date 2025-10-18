//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Preserves the out-of-line definition point for il::core::Global, the IL node
// that represents module-scope data objects.  Today the type is header-only, but
// this translation unit marks where serialization helpers, verifier bridges, or
// metadata utilities should accumulate without disturbing existing include
// relationships once additional behaviour becomes necessary.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Stub translation unit for `il::core::Global` helpers.
/// @details Global declarations currently expose inline-only behaviour.  Keeping
///          this file checked in documents the canonical place for future
///          utilities that merit out-of-line definitions.

#include "il/core/Global.hpp"

namespace il::core
{

// Intentionally empty – see file header for rationale.

} // namespace il::core

