// File: src/il/core/Extern.cpp
// License: MIT License (c) 2024 The Viper Project Authors. See LICENSE in the
//          project root for details.
// Purpose: Reserve the extension point for future `il::core::Extern` helpers
//          without forcing churn in downstream build graphs.
// Ownership/Lifetime: Contains no runtime state; all behaviour remains inline.
// Links: docs/codemap.md#il-core

/// @file
/// @brief Placeholder translation unit for `il::core::Extern` out-of-line code.
/// @details The `Extern` abstraction presently lives entirely in the header, but
///          keeping this file in the build tree advertises where verifier,
///          serializer, or analysis helpers should be added without forcing
///          downstream include churn.

#include "il/core/Extern.hpp"

namespace il::core
{

// No out-of-line logic.
} // namespace il::core
