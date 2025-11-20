//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Options.hpp
// Purpose: Feature flags for the BASIC frontend that can be toggled by tools.
// Key invariants:
//   - Defaults are conservative and stable across the frontend.
//   - Flags are process-local and intended for testing/experiments.
// Ownership/Lifetime: Simple process-global storage; not thread-safe.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

namespace il::frontends::basic
{

/// @brief Process-global BASIC front-end feature flags.
struct FrontendOptions
{
    /// @brief Enable treating the reserved root namespace 'Viper' as a readable
    ///        runtime namespace for imports and references.
    /// @details When enabled, USING Viper.* and calls/references to Viper.* are
    ///          permitted, while declaring namespaces/types under 'Viper' remains
    ///          prohibited. When disabled, the legacy behavior blocks USING and
    ///          references to 'Viper'.
    static bool enableRuntimeNamespaces();

    /// @brief Set @ref enableRuntimeNamespaces() for this process.
    static void setEnableRuntimeNamespaces(bool on);

    /// @brief Enable minimal bridging for namespaced runtime types (constructors).
    /// @details When enabled, selected NEW expressions for built-in types may
    ///          be lowered directly to runtime helpers (catalog-only types).
    static bool enableRuntimeTypeBridging();

    /// @brief Set @ref enableRuntimeTypeBridging() for this process.
    static void setEnableRuntimeTypeBridging(bool on);
};

} // namespace il::frontends::basic
