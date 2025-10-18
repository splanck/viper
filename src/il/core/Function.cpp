//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Maintains the out-of-line definition site for il::core::Function, the IR
// container that models SSA procedures.  All functionality is currently
// header-only, but keeping this translation unit reserved clarifies where future
// helpers (debug metadata, verifier bridges, serialization hooks) should live so
// downstream consumers remain insulated from include churn when they arrive.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Stub translation unit for `il::core::Function` helpers.
/// @details Function behaviour is presently inline.  This file documents the
///          extension point for any forthcoming out-of-line utilities that need
///          to augment the type.

#include "il/core/Function.hpp"

namespace il::core
{

// Intentionally empty – see file header for rationale.

} // namespace il::core

