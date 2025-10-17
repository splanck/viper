// File: src/il/core/Module.cpp
// License: MIT License (c) 2024 The Viper Project Authors. See LICENSE in the
//          project root for details.
// Purpose: Preserve an extension point for `il::core::Module` utilities without
//          forcing downstream rebuilds when those helpers appear.
// Ownership/Lifetime: Contains no runtime code; module functionality remains
//                     header-defined today.
// Links: docs/codemap.md#il-core

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
