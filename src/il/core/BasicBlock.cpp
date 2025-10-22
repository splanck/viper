//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/core/BasicBlock.cpp
// Purpose: Reserve the out-of-line extension point for il::core::BasicBlock.
// Key invariants: The translation unit intentionally contains no behaviour until
//                 dedicated helpers are required, ensuring the eventual
//                 implementation lands in a predictable location.
// Ownership/Lifetime: BasicBlock remains header-only today; future additions
//                     should avoid owning global state and instead operate on
//                     caller-provided IR objects.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Stub translation unit for `il::core::BasicBlock` helpers.
/// @details `BasicBlock` currently relies entirely on inline member definitions.
///          Keeping this file in the build advertises where verifier utilities or
///          analysis helpers should eventually live without disturbing existing
///          include graphs.

#include "il/core/BasicBlock.hpp"

namespace il::core
{

// No out-of-line methods.
} // namespace il::core
