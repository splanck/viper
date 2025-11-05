//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/pass/PassManager.cpp
// Purpose: Define the shared pass manager façade used across IL and codegen.
// Links: docs/architecture.md#passes
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the instrumentation-friendly pass manager façade.
/// @details Provides the plumbing to register pass callbacks, configure
///          instrumentation hooks, and execute ordered pipelines while
///          propagating success/failure information back to callers.

#include "viper/pass/PassManager.hpp"

#include <utility>

namespace viper::pass
{

void PassManager::registerPass(std::string id, PassCallback callback)
{
    passes_[std::move(id)] = std::move(callback);
}

void PassManager::setPrintBeforeHook(PrintHook hook)
{
    printBefore_ = std::move(hook);
}

void PassManager::setPrintAfterHook(PrintHook hook)
{
    printAfter_ = std::move(hook);
}

void PassManager::setVerifyEachHook(VerifyHook hook)
{
    verifyEach_ = std::move(hook);
}

bool PassManager::runPipeline(const Pipeline &pipeline) const
{
    for (const auto &passId : pipeline)
    {
        if (printBefore_)
            printBefore_(passId);

        auto it = passes_.find(passId);
        if (it == passes_.end())
            return false;

        if (!it->second())
            return false;

        if (verifyEach_ && !verifyEach_(passId))
            return false;

        if (printAfter_)
            printAfter_(passId);
    }
    return true;
}

} // namespace viper::pass

