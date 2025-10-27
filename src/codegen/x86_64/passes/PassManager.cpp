// File: src/codegen/x86_64/passes/PassManager.cpp
// Purpose: Implement the lightweight pass manager for the x86-64 codegen pipeline.
// Key invariants: Passes execute in registration order; diagnostics are preserved when a
//                 pass fails and no further passes are run.
// Ownership/Lifetime: PassManager owns pass instances while callers own the Module state and
//                     diagnostic sinks supplied to run().
// Links: docs/codemap.md, src/codegen/x86_64/CodegenPipeline.hpp

#include "codegen/x86_64/passes/PassManager.hpp"

#include <ostream>
#include <utility>

namespace viper::codegen::x64::passes
{

void Diagnostics::error(std::string message)
{
    errors_.push_back(std::move(message));
}

void Diagnostics::warning(std::string message)
{
    warnings_.push_back(std::move(message));
}

bool Diagnostics::hasErrors() const noexcept
{
    return !errors_.empty();
}

bool Diagnostics::hasWarnings() const noexcept
{
    return !warnings_.empty();
}

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

const std::vector<std::string> &Diagnostics::errors() const noexcept
{
    return errors_;
}

const std::vector<std::string> &Diagnostics::warnings() const noexcept
{
    return warnings_;
}

void PassManager::addPass(std::unique_ptr<Pass> pass)
{
    passes_.push_back(std::move(pass));
}

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
