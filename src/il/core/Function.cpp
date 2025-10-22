//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/core/Function.cpp
// Purpose: Provide the canonical home for il::core::Function out-of-line helpers.
// Key invariants: The unit intentionally exports no symbols yet; it exists so
//                 future implementations (metadata accessors, verifier bridges,
//                 etc.) can land without reorganising the build.
// Ownership/Lifetime: Function instances remain header-defined; upcoming helpers
//                     should respect caller-owned IR without introducing global
//                     state.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Placeholder translation unit for `il::core::Function` extensions.
/// @details All current `Function` members are defined inline in the header, yet
///          keeping this file in the build makes the extension point explicit for
///          future helpers (metadata attachment, verifier glue, etc.) and
///          prevents disruptive build churn when those helpers eventually
///          materialise.


#include "il/core/Function.hpp"

namespace il::core
{

// No out-of-line methods yet.
} // namespace il::core
