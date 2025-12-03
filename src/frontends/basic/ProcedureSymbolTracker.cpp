//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ProcedureSymbolTracker.cpp
// Purpose: Implementation of centralized symbol usage tracking.
// Key invariants: Delegates to Lowerer methods; no direct symbol table mutation.
// Ownership/Lifetime: Stateless helper bound to a Lowerer instance.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/ProcedureSymbolTracker.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"

namespace il::frontends::basic
{

ProcedureSymbolTracker::ProcedureSymbolTracker(Lowerer &lowerer, bool trackCrossProc) noexcept
    : lowerer_(lowerer), trackCrossProc_(trackCrossProc)
{
}

bool ProcedureSymbolTracker::shouldSkip(std::string_view name) const
{
    if (name.empty())
        return true;
    if (lowerer_.isFieldInScope(name))
        return true;
    return false;
}

void ProcedureSymbolTracker::trackScalar(std::string_view name)
{
    if (shouldSkip(name))
        return;
    lowerer_.markSymbolReferenced(name);
    trackCrossProcGlobalIfNeeded(name);
}

void ProcedureSymbolTracker::trackArray(std::string_view name)
{
    if (shouldSkip(name))
        return;
    lowerer_.markSymbolReferenced(name);
    lowerer_.markArray(name);
    trackCrossProcGlobalIfNeeded(name);
}

void ProcedureSymbolTracker::track(std::string_view name, bool isArray)
{
    if (isArray)
        trackArray(name);
    else
        trackScalar(name);
}

void ProcedureSymbolTracker::trackCrossProcGlobalIfNeeded(std::string_view name)
{
    if (!trackCrossProc_)
        return;

    if (name.empty())
        return;

    auto sema = lowerer_.semanticAnalyzer();
    if (!sema)
        return;

    // Track cross-proc globals when either:
    // 1. fn == nullptr (early scan phase before function context is set)
    // 2. fn->name != "main" (inside a procedure other than @main)
    // This ensures module-level symbols used in procedures get runtime-backed storage.
    const auto *fn = lowerer_.context().function();
    if (fn == nullptr || fn->name != "main")
    {
        // Construct the string once and reuse for both checks
        std::string nameStr(name);
        if (sema->isModuleLevelSymbol(nameStr))
            lowerer_.markCrossProcGlobal(std::move(nameStr));
    }
}

bool ProcedureSymbolTracker::isInMain() const
{
    const auto *fn = lowerer_.context().function();
    return fn != nullptr && fn->name == "main";
}

} // namespace il::frontends::basic
