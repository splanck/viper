//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/il/Verify.hpp
// Purpose: Stable façade exposing IL verifier entry points.
// Key invariants: Mirrors il::verify::Verifier public API only.
// Ownership/Lifetime: Caller retains ownership of modules and diagnostics.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/verify/Verifier.hpp"

/// @file include/viper/il/Verify.hpp
/// @brief Public forwarding header providing structured verification entry
///        points without requiring downstreams to include src/il paths.
