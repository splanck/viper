//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/placeholder.cpp
// Purpose: Provide a stable linkage point for the not-yet-implemented x86-64
//          backend so the code generation library still exports a symbol.
// Ownership/Lifetime: No state; the function returns an integral status code.
// Links: docs/architecture.md#codegen
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Defines a stub symbol that keeps the x86-64 backend linkable.
/// @details Downstream tools expect the code generation library to export at
///          least one function.  The placeholder satisfies that requirement
///          while developers build out the real backend, enabling incremental
///          integration testing without linker failures.

namespace il::codegen::x86_64
{

/// @brief Report that the x86-64 backend has not been implemented yet.
/// @details The helper intentionally performs no work beyond returning a fixed
///          status code.  By remaining side-effect free the symbol can be used
///          by sanity checks that merely need to confirm the library loaded.
/// @return Always returns 0 to signal "not yet implemented" without failure.
int placeholder()
{
    return 0;
}
} // namespace il::codegen::x86_64
