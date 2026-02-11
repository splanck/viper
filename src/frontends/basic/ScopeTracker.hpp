//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ScopeTracker.hpp
// Purpose: Backward-compatible alias re-exporting the shared ScopeTracker.
//          New code should include frontends/common/ScopeTracker.hpp directly.
// Key invariants: Delegates entirely to common::ScopeTracker; adds no new state.
// Ownership/Lifetime: Header-only alias; no owned resources.
// Links: frontends/common/ScopeTracker.hpp
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
