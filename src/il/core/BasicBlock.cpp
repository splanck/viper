//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Keeps the extension point for il::core::BasicBlock, the aggregate that owns
// instruction lists and block parameters.  All behaviour is currently defined
// inline for efficiency, but this translation unit documents where future
// out-of-line helpers (analytics, metadata attachment, verification glue) should
// live so that downstream dependencies remain stable when the implementation
// eventually grows.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Stub translation unit for `il::core::BasicBlock` helpers.
/// @details The block abstraction is presently header-only.  Keeping this file
///          visible clarifies the sanctioned location for any forthcoming
///          utilities that need out-of-line definitions.

#include "il/core/BasicBlock.hpp"

namespace il::core
{

// Intentionally empty – see file header for rationale.

} // namespace il::core

