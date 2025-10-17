// File: src/il/core/Global.cpp
// License: MIT License (c) 2024 The Viper Project Authors. See LICENSE in the
//          project root for details.
// Purpose: Reserve an extension point for `il::core::Global` helpers so future
//          additions avoid disturbing downstream targets.
// Ownership/Lifetime: No runtime code; behaviour remains inline today.
// Links: docs/codemap.md#il-core

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
