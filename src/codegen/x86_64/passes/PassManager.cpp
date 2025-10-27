//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/passes/PassManager.cpp
// Purpose: Implement the lightweight pass manager for the x86-64 codegen pipeline.
// Key invariants: Passes execute in registration order; diagnostics are preserved when a
//                 pass fails and no further passes are run.
// Ownership/Lifetime: PassManager owns pass instances while callers own the Module state and
//                     diagnostic sinks supplied to run().
// Links: docs/codemap.md, src/codegen/x86_64/CodegenPipeline.hpp
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Definitions for the x86-64 backend's pass orchestration helpers.
/// @details Provides diagnostics collection utilities together with the
///          `PassManager` driver that sequences transformation passes while
///          honouring early exits on failure and reporting accumulated
///          warnings/errors to callers.

#include "codegen/x86_64/passes/PassManager.hpp"

#include <ostream>
#include <utility>

namespace viper::codegen::x64::passes
{

/// @brief Record an error message reported by a pass.
/// @details Errors are stored verbatim and surfaced when @ref flush is called.
///          The pass manager treats the presence of errors as a fatal
///          condition, short-circuiting the pipeline.
/// @param message Human-readable description of the failure.
void Diagnostics::error(std::string message)
{
    errors_.push_back(std::move(message));
}

/// @brief Record a warning message emitted by a pass.
/// @details Warnings do not stop the pipeline but remain available to callers
///          via @ref flush so front ends can present them to users.
/// @param message Advisory diagnostic text.
void Diagnostics::warning(std::string message)
{
    warnings_.push_back(std::move(message));
}

/// @brief Check whether any errors were recorded.
/// @details Used by the pass manager to decide whether execution should stop
///          after a pass completes.
/// @return @c true when at least one error has been reported.
bool Diagnostics::hasErrors() const noexcept
{
    return !errors_.empty();
}

/// @brief Check whether any warnings were recorded.
/// @details Enables callers to present non-fatal advisories even when the
///          pipeline finishes successfully.
/// @return @c true when one or more warnings were captured.
bool Diagnostics::hasWarnings() const noexcept
{
    return !warnings_.empty();
}

/// @brief Access the collected error messages.
/// @details Exposes the underlying storage so CLI front ends can surface every
///          message even if they want to format output differently from @ref flush.
/// @return Reference to the vector storing error diagnostics.
const std::vector<std::string> &Diagnostics::errors() const noexcept
{
    return errors_;
}

/// @brief Access the collected warning messages.
/// @details Allows embedders to retrieve recorded warnings for custom
///          presentation while preserving the ability to flush them through
///          the helper.
/// @return Reference to the vector storing warning diagnostics.
const std::vector<std::string> &Diagnostics::warnings() const noexcept
{
    return warnings_;
}

/// @brief Stream recorded diagnostics to the provided output sinks.
/// @details Errors are always written to @p err. When @p warn is non-null,
///          warnings are written to that stream; otherwise they remain stored
///          in the diagnostics object so callers can handle them manually.
/// @param err   Destination stream for fatal diagnostics.
/// @param warn  Optional destination stream for warnings.
void Diagnostics::flush(std::ostream &err, std::ostream *warn) const
{
    for (const auto &msg : errors_)
    {
        err << msg;
        if (!msg.empty() && msg.back() != '\n')
        {
            err << '\n';
        }
    }
    if (warn != nullptr)
    {
        for (const auto &msg : warnings_)
        {
            *warn << msg;
            if (!msg.empty() && msg.back() != '\n')
            {
                *warn << '\n';
            }
        }
    }
}

/// @brief Register a new pass with the manager.
/// @details Ownership of the dynamically allocated pass is transferred to the
///          manager, ensuring the pass outlives the pipeline execution. Passes
///          are executed in the order they were added.
/// @param pass Unique pointer to the pass to enqueue.
void PassManager::addPass(std::unique_ptr<Pass> pass)
{
    passes_.push_back(std::move(pass));
}

/// @brief Execute the registered passes against the supplied module state.
/// @details Invokes each pass sequentially, stopping immediately when a pass
///          signals failure or records an error. Passes receive a shared
///          diagnostics instance so they can collaborate on reporting.
/// @param module Module state mutated by passes.
/// @param diags  Diagnostics sink shared across passes.
/// @return @c true when every pass completes successfully.
bool PassManager::run(Module &module, Diagnostics &diags) const
{
    for (const auto &pass : passes_)
    {
        if (!pass->run(module, diags))
        {
            return false;
        }
    }
    return true;
}

} // namespace viper::codegen::x64::passes

