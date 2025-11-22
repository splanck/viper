//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Unsupported.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>
#include <string>

/// @file src/codegen/x86_64/Unsupported.hpp
/// @brief Declares helpers for signaling unsupported x86-64 Phase A features.
/// @invariant Functions in this header never return; they throw diagnostics for
///             unsupported features.
/// @ownership Shared utility owned by the x86-64 code generation backend.

namespace viper::codegen::x64
{

/// @brief Raise a standardized diagnostic for Phase A unsupported features.
/// @param feature The name of the unsupported feature encountered.
[[noreturn]] inline void phaseAUnsupported(const char *feature)
{
    throw std::runtime_error(std::string("x86-64 Phase A does not support: ") + feature);
}

} // namespace viper::codegen::x64
