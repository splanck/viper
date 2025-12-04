//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Options.hpp
// Purpose: Feature flags for the BASIC frontend that can be toggled by tools.
// Key invariants:
//   - Defaults are conservative and stable across the frontend.
//   - Flags are process-global and intended for testing/experiments.
// Ownership/Lifetime: Simple process-global storage with thread-safe access.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//
//
// THREADING MODEL
// ===============
//
// All FrontendOptions flags use std::atomic<bool> with relaxed memory ordering.
// This provides the following guarantees:
//
// - **Thread-safe reads and writes**: Multiple threads may concurrently read
//   and write these flags without data races.
//
// - **No synchronization**: Relaxed ordering means there is no
//   happens-before relationship between accesses. A write on thread A may
//   not be immediately visible to thread B.
//
// - **Recommended usage pattern**: Configure all options on the main thread
//   before spawning worker threads. This ensures all workers see consistent
//   values. Changing options while workers are active is safe (no UB) but
//   may result in inconsistent behavior across compilation units.
//
// - **No notification mechanism**: There is no callback or notification when
//   an option changes. Code reading these options gets a snapshot value.
//
//===----------------------------------------------------------------------===//

#pragma once

namespace il::frontends::basic
{

/**
 * @brief Process-global BASIC front-end feature flags.
 *
 * These flags control optional frontend features and are intended for
 * testing, experiments, and tool configuration. All flags use atomic
 * storage and are safe to access from multiple threads.
 *
 * ## Threading Guarantees
 *
 * - All getters and setters are thread-safe (no data races).
 * - Uses relaxed memory ordering for minimal overhead.
 * - Best practice: configure options before starting compilation threads.
 *
 * ## Default Values
 *
 * | Flag                        | Default | Description                      |
 * |-----------------------------|---------|----------------------------------|
 * | enableRuntimeNamespaces     | true    | Allow USING Viper.* imports      |
 * | enableRuntimeTypeBridging   | true    | Direct runtime type constructors |
 * | enableSelectCaseConstLabels | true    | CONST labels in SELECT CASE     |
 */
struct FrontendOptions
{
    /// @brief Enable treating the reserved root namespace 'Viper' as a readable
    ///        runtime namespace for imports and references.
    /// @details When enabled, USING Viper.* and calls/references to Viper.* are
    ///          permitted, while declaring namespaces/types under 'Viper' remains
    ///          prohibited. When disabled, the legacy behavior blocks USING and
    ///          references to 'Viper'.
    /// @note Thread-safe: uses atomic load with relaxed ordering.
    static bool enableRuntimeNamespaces();

    /// @brief Set @ref enableRuntimeNamespaces() for this process.
    /// @note Thread-safe: uses atomic store with relaxed ordering.
    static void setEnableRuntimeNamespaces(bool on);

    /// @brief Enable minimal bridging for namespaced runtime types (constructors).
    /// @details When enabled, selected NEW expressions for built-in types may
    ///          be lowered directly to runtime helpers (catalog-only types).
    /// @note Thread-safe: uses atomic load with relaxed ordering.
    static bool enableRuntimeTypeBridging();

    /// @brief Set @ref enableRuntimeTypeBridging() for this process.
    /// @note Thread-safe: uses atomic store with relaxed ordering.
    static void setEnableRuntimeTypeBridging(bool on);

    /// @brief Enable CONST/CHR$ case labels in SELECT CASE.
    /// @details When enabled, the parser accepts identifiers bound via CONST
    ///          (integer/string) and folded CHR/CHR$ calls as CASE labels.
    /// @note Thread-safe: uses atomic load with relaxed ordering.
    static bool enableSelectCaseConstLabels();

    /// @brief Set @ref enableSelectCaseConstLabels() for this process.
    /// @note Thread-safe: uses atomic store with relaxed ordering.
    static void setEnableSelectCaseConstLabels(bool on);
};

} // namespace il::frontends::basic
