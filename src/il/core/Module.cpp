//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/core/Module.cpp
// Purpose: Anchor the expansion point for il::core::Module helper routines.
// Key invariants: The compilation unit intentionally exports no symbols until
//                 concrete helpers are required, keeping downstream include
//                 graphs stable while signalling where cross-cutting utilities
//                 (verification bridges, serialization aids, etc.) should live.
// Ownership/Lifetime: Module remains header-only today; future additions should
//                     operate on caller-owned IR without introducing global
//                     state.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Stub translation unit for `il::core::Module` helpers.
/// @details `Module` currently defines all behaviour inline. Keeping this file in
///          the build advertises the sanctioned location for future cross-cutting
///          helpers (verification adapters, serialization glue, etc.) while
///          maintaining stable build dependencies for downstream projects.

#include "il/core/Module.hpp"

namespace il::core
{

// No out-of-line logic.
} // namespace il::core
