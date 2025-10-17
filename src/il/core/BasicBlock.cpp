// File: src/il/core/BasicBlock.cpp
// License: MIT License (c) 2024 The Viper Project Authors. See LICENSE in the
//          project root for details.
// Purpose: Mark the extension point for future `BasicBlock` utilities without
//          introducing build churn today.
// Ownership/Lifetime: Contains no runtime code; all members remain inline.
// Links: docs/codemap.md#il-core

/// @file
/// @brief Stub translation unit for `il::core::BasicBlock` helpers.
/// @details `BasicBlock` currently relies entirely on inline member definitions.
///          The presence of this file documents the sanctioned location for
///          future out-of-line utilities (verification hooks, metadata helpers,
///          etc.) so build dependencies remain stable when that functionality
///          arrives.

#include "il/core/BasicBlock.hpp"

namespace il::core
{

// No out-of-line methods.
} // namespace il::core
