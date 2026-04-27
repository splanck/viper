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

#include <algorithm>
#include <string>
#include <utility>

using namespace il::core;

namespace il::verify {
namespace {
using il::support::Diag;
using il::support::Expected;

Diag normalizeVerifierDiag(Diag diag) {
    if (diag.code.empty()) {
        diag.code = diag.severity == il::support::Severity::Warning ? "V-IL-WARN" : "V-IL-VERIFY";
    }
    return diag;
}

bool sameDiagnostic(const Diag &lhs, const Diag &rhs) {
    return lhs.severity == rhs.severity && lhs.code == rhs.code && lhs.message == rhs.message &&
           lhs.loc.file_id == rhs.loc.file_id && lhs.loc.line == rhs.loc.line &&
           lhs.loc.column == rhs.loc.column;
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
Expected<void> Verifier::verify(const Module &m) {
    auto diagnostics = verifyAll(m, 50);
    if (diagnostics.empty())
        return {};

    Diag primary = diagnostics.front();
    if (diagnostics.size() > 1) {
        primary.message += "\n";
        primary.message += std::to_string(diagnostics.size() - 1);
        primary.message += " additional verifier diagnostic";
        if (diagnostics.size() != 2)
            primary.message += "s";
        primary.message += ":";
        for (size_t index = 1; index < diagnostics.size(); ++index) {
            primary.notes.push_back({diagnostics[index].loc, diagnostics[index].message});
        }
    }
    return Expected<void>{std::move(primary)};
}

std::vector<Diag> Verifier::verifyAll(const Module &m, size_t maxDiagnostics) {
    CollectingDiagSink sink;
    std::vector<Diag> diagnostics;

    auto appendFailure = [&](const Expected<void> &result) {
        if (result) {
            sink.clear();
            return;
        }
        for (const auto &diag : sink.diagnostics()) {
            if (diagnostics.size() >= maxDiagnostics)
                return;
            diagnostics.push_back(normalizeVerifierDiag(diag));
        }
        sink.clear();
        if (!result && diagnostics.size() < maxDiagnostics) {
            Diag resultDiag = normalizeVerifierDiag(result.error());
            const bool duplicate =
                std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diag &existing) {
                    return sameDiagnostic(existing, resultDiag);
                });
            if (!duplicate)
                diagnostics.push_back(std::move(resultDiag));
        }
    };

    ExternVerifier externVerifier;
    appendFailure(externVerifier.run(m, sink));
    if (diagnostics.size() >= maxDiagnostics)
        return diagnostics;

    GlobalVerifier globalVerifier;
    appendFailure(globalVerifier.run(m, sink));
    if (diagnostics.size() >= maxDiagnostics)
        return diagnostics;

    FunctionVerifier functionVerifier(externVerifier.externs());
    appendFailure(functionVerifier.run(m, sink));
    if (diagnostics.size() >= maxDiagnostics)
        return diagnostics;

    EhVerifier ehVerifier;
    appendFailure(ehVerifier.run(m, sink));

    return diagnostics;
}

} // namespace il::verify
