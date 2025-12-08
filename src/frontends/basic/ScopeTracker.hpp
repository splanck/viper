//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ScopeTracker.hpp
// Purpose: Re-exports common ScopeTracker for BASIC frontend compatibility.
//
// NOTE: This file re-exports the common ScopeTracker for backwards
//       compatibility. New code should use frontends/common/ScopeTracker.hpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/common/ScopeTracker.hpp"

namespace il::frontends::basic
{

// Re-export common ScopeTracker for backwards compatibility
using ScopeTracker = ::il::frontends::common::ScopeTracker;

} // namespace il::frontends::basic
