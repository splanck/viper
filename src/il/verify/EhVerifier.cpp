//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// File: src/il/verify/EhVerifier.cpp
//
// Purpose:
//   Implement the exception-handling verifier entry point. The verifier now
//   orchestrates reusable EhModel/EhChecks helpers to perform stack balance and
//   resume-edge validation while preserving historic diagnostics.
//
// Key invariants:
//   * Each function is analysed independently using a freshly constructed
//     EhModel.
//   * Only functions containing EH operations trigger additional checks.
//
// Ownership/Lifetime:
//   The verifier borrows IR nodes from the inspected module and never assumes
//   ownership. Diagnostics are surfaced via Expected results.
//
// Links:
//   docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/verify/EhVerifier.hpp"

#include "il/core/Module.hpp"
#include "il/verify/EhChecks.hpp"
#include "il/verify/EhModel.hpp"

using namespace il::core;

namespace il::verify
{

il::support::Expected<void> EhVerifier::run(const Module &module, DiagSink &sink) const
{
    (void)sink;

    for (const auto &fn : module.functions)
    {
        EhModel model(fn);
        if (!model.hasEhInstructions())
            continue;

        if (auto result = checkEhStackBalance(model); !result)
            return result;

        if (auto result = checkDominanceOfHandlers(model); !result)
            return result;

        if (auto result = checkUnreachableHandlers(model); !result)
            return result;

        if (auto result = checkResumeEdges(model); !result)
            return result;
    }

    return {};
}

} // namespace il::verify

