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
    if (diag.stage.empty())
        diag.stage = "verify";
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
    auto primaryIt =
        std::find_if(diagnostics.begin(), diagnostics.end(), [](const Diag &diag) {
            return diag.severity == il::support::Severity::Error;
        });
    if (primaryIt == diagnostics.end())
        return {};

    Diag primary = *primaryIt;
    if (diagnostics.size() > 1) {
        primary.message += "\n";
        primary.message += std::to_string(diagnostics.size() - 1);
        primary.message += " additional verifier diagnostic";
        if (diagnostics.size() != 2)
            primary.message += "s";
        primary.message += ":";
        for (const auto &diag : diagnostics) {
            if (sameDiagnostic(diag, primary))
                continue;
            primary.notes.push_back({diag.loc, diag.message});
        }
    }
    return Expected<void>{std::move(primary)};
}

std::vector<Diag> Verifier::verifyAll(const Module &m, size_t maxDiagnostics) {
    CollectingDiagSink sink;
    std::vector<Diag> diagnostics;

    auto appendDiagnostics = [&](const Expected<void> &result) {
        for (const auto &diag : sink.diagnostics()) {
            if (diagnostics.size() >= maxDiagnostics)
                return;
            Diag normalized = normalizeVerifierDiag(diag);
            const bool duplicate =
                std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diag &existing) {
                    return sameDiagnostic(existing, normalized);
                });
            if (!duplicate)
                diagnostics.push_back(std::move(normalized));
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
    appendDiagnostics(externVerifier.run(m, sink));
    if (diagnostics.size() >= maxDiagnostics)
        return diagnostics;

    GlobalVerifier globalVerifier;
    appendDiagnostics(globalVerifier.run(m, sink));
    if (diagnostics.size() >= maxDiagnostics)
        return diagnostics;

    FunctionVerifier functionVerifier(externVerifier.externs(), globalVerifier.globals());
    appendDiagnostics(functionVerifier.run(m, sink));
    if (diagnostics.size() >= maxDiagnostics)
        return diagnostics;

    EhVerifier ehVerifier;
    appendDiagnostics(ehVerifier.run(m, sink));

    return diagnostics;
}

} // namespace il::verify
