// File: include/viper/il/IRBuilder.hpp
// Purpose: Stable façade for constructing IL modules without depending on src paths.
// Key invariants: Mirrors il::build::IRBuilder API; no additional behavior.
// Ownership/Lifetime: IRBuilder lifetime remains managed by callers.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/build/IRBuilder.hpp"

/// @file include/viper/il/IRBuilder.hpp
/// @brief Public forwarding header exposing il::build::IRBuilder for clients
///        that need to generate IL programmatically.

