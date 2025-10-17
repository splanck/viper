// File: src/il/core/Function.cpp
// License: MIT License (c) 2024 The Viper Project Authors. See LICENSE in the
//          project root for details.
// Purpose: Maintain a stable extension point for future `Function` utilities
//          without disturbing downstream builds today.
// Ownership/Lifetime: Contains no runtime logic; all behaviour is inline for now.
// Links: docs/codemap.md#il-core

/// @file
/// @brief Placeholder translation unit for `il::core::Function` extensions.
/// @details All current `Function` members are defined inline in the header, yet
///          keeping this file in the build makes the extension point explicit for
///          future helpers (metadata attachment, verifier glue, etc.) and prevents
///          disruptive build churn when those helpers eventually materialise.


#include "il/core/Function.hpp"

namespace il::core
{

// No out-of-line methods yet.
} // namespace il::core
