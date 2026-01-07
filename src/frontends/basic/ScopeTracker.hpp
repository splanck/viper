//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief BASIC-frontend alias for the shared scope tracker.
/// @details This header preserves a legacy include path by re-exporting the
///          common `ScopeTracker` used across frontends. New code should include
///          `frontends/common/ScopeTracker.hpp` directly to make dependencies
///          explicit and reduce alias indirection.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/common/ScopeTracker.hpp"

namespace il::frontends::basic
{

/// @brief Backward-compatible alias for the shared scope tracker.
/// @details The aliased type tracks scope nesting and symbol visibility during
///          semantic analysis and lowering. Prefer the common namespace in new
///          code to avoid duplicating frontend-specific entry points.
using ScopeTracker = ::il::frontends::common::ScopeTracker;

} // namespace il::frontends::basic
