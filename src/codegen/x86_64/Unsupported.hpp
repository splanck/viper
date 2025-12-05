//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Unsupported.hpp
// Purpose: Provides diagnostic helpers for unsupported Phase A features.
// Key invariants: phaseAUnsupported() never returns; it always throws
//                 std::runtime_error with a descriptive message identifying
//                 the unsupported feature.
// Ownership/Lifetime: Header-only utility with no state; exceptions propagate
//                     to the caller who must handle or terminate.
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
