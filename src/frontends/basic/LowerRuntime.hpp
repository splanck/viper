//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares runtime tracking and declaration helpers for BASIC
// lowering, managing the interface between generated IL and the Viper runtime
// library.
//
// Runtime Library Integration:
// Many BASIC operations cannot be expressed directly in IL and require calls
// to runtime library functions:
//
// - String operations: Concatenation, comparison, substring extraction
// - I/O operations: PRINT, INPUT, file operations
// - Array operations: Dynamic allocation, bounds checking, element access
// - Built-in functions: Mathematical functions (SIN, COS), string functions
//   (LEFT$, MID$), type conversions
//
// Runtime Declaration Management:
// The LowerRuntime helper ensures that:
// - Runtime functions are declared in IL before use
// - Each runtime function is declared exactly once per IL module
// - Runtime function signatures match the actual runtime library exports
// - Calls to runtime functions use correct calling conventions
//
// Runtime Function Categories:
// - viper_string_*: String manipulation (concat, compare, substring, etc.)
// - viper_io_*: I/O operations (print, input, file operations)
// - viper_array_*: Array operations (alloc, get, set, bounds check)
// - viper_math_*: Mathematical functions (sin, cos, tan, sqrt, etc.)
// - viper_convert_*: Type conversion (int to string, string to int, etc.)
//
// Declaration Tracking:
// To ensure each runtime function is declared exactly once:
// - First use of a runtime function triggers declaration
// - Subsequent uses reference the existing declaration
// - Bitset tracks which runtime functions have been declared
//
// Integration:
// - Used by: Lowering helpers (LowerExprBuiltin, LowerStmt_IO, etc.)
// - Operates on: Lowerer state (IRBuilder, IL module)
// - Coordinates with: RuntimeSignatures for function signatures
//
// Design Notes:
// - Runtime declarations are module-scoped, not per-function
// - Tracking prevents duplicate declarations
// - Signature validation ensures ABI compatibility
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/runtime/RuntimeSignatures.hpp"
#include <bitset>
#include <unordered_set>
#include <vector>

namespace il::build
{
class IRBuilder;
} // namespace il::build

namespace il::frontends::basic
{

/// @brief Tracks runtime helper usage across scanning and lowering.
/// @invariant Helpers are declared at most once and maintain first-use order.
/// @ownership Owned by Lowerer; stores transient state per lowering run.
class RuntimeHelperTracker
{
  public:
    using RuntimeFeature = il::runtime::RuntimeFeature;

    /// @brief Reset helper tracking to an empty state.
    void reset();

    /// @brief Mark a runtime helper as required.
    void requestHelper(RuntimeFeature feature);

    /// @brief Query whether a runtime helper has been requested.
    [[nodiscard]] bool isHelperNeeded(RuntimeFeature feature) const;

    /// @brief Record an ordered runtime helper requirement.
    void trackRuntime(RuntimeFeature feature);

    /// @brief Declare all helpers requested during the current lowering run.
    void declareRequiredRuntime(build::IRBuilder &b, bool boundsChecks) const;

  private:
    struct RuntimeFeatureHash
    {
        std::size_t operator()(RuntimeFeature f) const;
    };

    static constexpr std::size_t kRuntimeFeatureCount =
        static_cast<std::size_t>(RuntimeFeature::Count);

    std::bitset<kRuntimeFeatureCount> requested_;
    std::vector<RuntimeFeature> ordered_;
    std::unordered_set<RuntimeFeature, RuntimeFeatureHash> tracked_;
    // Names of runtime callees referenced directly in IL (alias-aware).
    std::unordered_set<std::string> usedNames_;

  public:
    /// @brief Record a runtime callee name observed during lowering.
    /// @details When a call targets a known runtime helper, remember the exact
    ///          symbol spelling so extern declarations can match call sites.
    void trackCalleeName(std::string_view name);
    /// @brief Enumerate callee names recorded so far.
    const std::unordered_set<std::string> &usedNames() const { return usedNames_; }
};

} // namespace il::frontends::basic
