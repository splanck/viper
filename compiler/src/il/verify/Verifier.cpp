//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/Verifier.cpp
// Purpose: Drive the multi-stage IL verifier and collate diagnostics into a
//          single Expected result.
// Links: docs/il-guide.md#verification
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the top-level IL verifier orchestration logic.
/// @details The verifier coordinates subsystem checks for externs, globals,
///          functions, and exception handlers before returning a consolidated
///          diagnostic outcome to tooling.

#include "il/verify/Verifier.hpp"

#include "il/core/Module.hpp"
#include "il/verify/DiagSink.hpp"
#include "il/verify/EhVerifier.hpp"
#include "il/verify/ExternVerifier.hpp"
#include "il/verify/FunctionVerifier.hpp"
#include "il/verify/GlobalVerifier.hpp"
#include "support/diag_expected.hpp"

#include <sstream>
#include <utility>

using namespace il::core;

namespace il::verify
{
namespace
{
using il::support::Diag;
using il::support::Expected;

/// @brief Combine a failing verification result with accumulated warnings.
///
/// @details When verification fails, diagnostics may already have been emitted
/// as warnings.  This helper prints each stored warning into a single message,
/// appends the original error diagnostic, and returns a new @c Expected<void>
/// that carries the aggregated text so callers can surface a consolidated
/// report.
///
/// @param failure Verification result that already represents an error.
/// @param sink Diagnostic sink containing any emitted warnings.
/// @return Updated failure result with warnings appended to the error message.
Expected<void> appendWarnings(Expected<void> failure, const CollectingDiagSink &sink)
{
    if (sink.diagnostics().empty())
        return failure;

    std::ostringstream oss;
    for (const auto &warning : sink.diagnostics())
        il::support::printDiag(warning, oss);
    il::support::printDiag(failure.error(), oss);

    Diag combined = failure.error();
    combined.message = oss.str();
    return Expected<void>{std::move(combined)};
}

} // namespace

/// @brief Run the full IL verifier pipeline over a module.
///
/// @details Executes the extern, global, function, and exception-handler
/// verifiers in sequence, stopping at the first failure.  Diagnostics are
/// captured via @c CollectingDiagSink so warnings can be appended to any error
/// returned to the caller.
///
/// @param m Module to verify.
/// @return @c Expected success on clean modules; otherwise an aggregated error diagnostic.
Expected<void> Verifier::verify(const Module &m)
{
    CollectingDiagSink sink;

    ExternVerifier externVerifier;
    if (auto result = externVerifier.run(m, sink); !result)
        return appendWarnings(result, sink);

    GlobalVerifier globalVerifier;
    if (auto result = globalVerifier.run(m, sink); !result)
        return appendWarnings(result, sink);

    FunctionVerifier functionVerifier(externVerifier.externs());
    if (auto result = functionVerifier.run(m, sink); !result)
        return appendWarnings(result, sink);

    EhVerifier ehVerifier;
    if (auto result = ehVerifier.run(m, sink); !result)
        return appendWarnings(result, sink);

    return {};
}

} // namespace il::verify
