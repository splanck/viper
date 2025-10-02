// File: src/il/verify/EhVerifier.cpp
// Purpose: Implements the verifier pass that validates EH stack balance per function.
// Key invariants: Control-flow is explored to ensure every execution path maintains
// balanced eh.push/eh.pop pairs, delegating to ExceptionHandlerAnalysis helpers.
// Ownership/Lifetime: Operates on caller-owned modules; no allocations outlive
// verification. Diagnostics are returned via Expected or forwarded through sinks.
// Links: docs/il-guide.md#reference

#include "il/verify/EhVerifier.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/verify/ExceptionHandlerAnalysis.hpp"

#include <unordered_map>

using namespace il::core;

namespace il::verify
{
using il::support::Expected;

il::support::Expected<void> EhVerifier::run(const Module &module, DiagSink &sink) const
{
    (void)sink;
    for (const auto &fn : module.functions)
    {
        std::unordered_map<std::string, const BasicBlock *> blockMap;
        blockMap.reserve(fn.blocks.size());
        for (const auto &bb : fn.blocks)
            blockMap[bb.label] = &bb;

        if (auto result = checkEhStackBalance(fn, blockMap); !result)
            return result;
    }

    return {};
}

} // namespace il::verify
