//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/pass/PassManager.hpp
// Purpose: Declare a lightweight, instrumentation-friendly pass manager façade.
// Key invariants: Pass callbacks are invoked in registration order; instrumentation
// hooks run around each pass invocation when provided.
// Ownership/Lifetime: The façade stores pass callbacks by value and does not own
// pass implementations beyond those callbacks. Links: docs/architecture.md#passes
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace viper::pass
{

/// @brief Shared façade for transformation pipelines across subsystems.
/// @details Provides a minimal API that maps string identifiers to callbacks and
///          executes them with optional instrumentation hooks. Subsystems can
///          register arbitrary pass callbacks that capture their execution
///          context (modules, diagnostics, etc.) while the façade sequences the
///          pipeline uniformly.
class PassManager
{
  public:
    /// @brief Ordered list of pass identifiers forming a pipeline.
    using Pipeline = std::vector<std::string>;

    /// @brief Callback type invoked to run an individual pass.
    /// @return @c true when the pass executed successfully, @c false on failure.
    using PassCallback = std::function<bool()>;

    /// @brief Instrumentation hook executed before or after a pass.
    /// @param id Identifier of the pass about to run or that just completed.
    using PrintHook = std::function<void(std::string_view id)>;

    /// @brief Hook used to verify state after each pass.
    /// @param id Identifier of the pass that just completed.
    /// @return @c true when verification succeeds, @c false otherwise.
    using VerifyHook = std::function<bool(std::string_view id)>;

    /// @brief Register or replace the callback associated with @p id.
    /// @param id Stable identifier for the pass.
    /// @param callback Callable invoked when @p id appears in a pipeline.
    void registerPass(std::string id, PassCallback callback);

    /// @brief Install a hook executed before each pass.
    /// @param hook Callback invoked prior to running a pass; cleared when empty.
    void setPrintBeforeHook(PrintHook hook);

    /// @brief Install a hook executed after each pass.
    /// @param hook Callback invoked after running a pass; cleared when empty.
    void setPrintAfterHook(PrintHook hook);

    /// @brief Install a verification hook executed after each pass.
    /// @param hook Callback invoked after running a pass; cleared when empty.
    void setVerifyEachHook(VerifyHook hook);

    /// @brief Execute @p pipeline, invoking instrumentation hooks when present.
    /// @param pipeline Ordered sequence of pass identifiers.
    /// @return @c true when all passes executed and verification succeeded.
    bool runPipeline(const Pipeline &pipeline) const;

  private:
    std::unordered_map<std::string, PassCallback> passes_;
    PrintHook printBefore_{};
    PrintHook printAfter_{};
    VerifyHook verifyEach_{};
};

} // namespace viper::pass
