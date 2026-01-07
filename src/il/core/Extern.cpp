//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/core/Extern.cpp
// Purpose: Document the sanctioned out-of-line home for il::core::Extern helpers.
// Key invariants: The implementation intentionally remains empty until concrete
//                 behaviour is required; the translation unit merely anchors the
//                 location for future utilities so build dependencies stay
//                 stable.
// Ownership/Lifetime: All Extern data continues to live in headers; this file
//                     should only gain stateless helpers or diagnostics wiring.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Placeholder translation unit for `il::core::Extern` out-of-line code.
/// @details The current implementation exposes no free functions, but retaining
///          this translation unit communicates where upcoming verifier hooks or
///          serializer extensions must live without forcing churn across
///          downstream build graphs.

#include "il/core/Extern.hpp"

namespace il::core
{

// No out-of-line logic.
} // namespace il::core
