//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/core/Global.cpp
// Purpose: Reserve the extension point for il::core::Global helper routines.
// Key invariants: The translation unit stays empty until global-specific logic
//                 is required, preserving stable include relationships for
//                 downstream tools that depend on the core IR model.
// Ownership/Lifetime: Globals continue to be managed by header-only types;
//                     future helpers should respect caller-owned storage.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Stub translation unit for `il::core::Global` helpers.
/// @details All global declaration behaviour currently lives inline in the
///          header. Retaining this file signals where future utilities (metadata
///          materialisation, verification glue, etc.) belong without disrupting
///          build dependencies when they are introduced.

#include "il/core/Global.hpp"

namespace il::core
{

// No out-of-line logic.
} // namespace il::core
