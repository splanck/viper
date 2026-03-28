//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/DiagnosticUtils.cpp
// Purpose: Implementation of shared diagnostic extraction utility.
// Key invariants:
//   - Severity mapping: Note=0, Warning=1, Error=2
// Ownership/Lifetime:
//   - All returned data is fully owned
// Links: tools/lsp-common/DiagnosticUtils.hpp
//
//===----------------------------------------------------------------------===//

#include "tools/lsp-common/DiagnosticUtils.hpp"

#include "support/diagnostics.hpp"

namespace viper::server {

std::vector<DiagnosticInfo> extractDiagnostics(const il::support::DiagnosticEngine &diag) {
    std::vector<DiagnosticInfo> result;
    for (const auto &d : diag.diagnostics()) {
        DiagnosticInfo info;
        switch (d.severity) {
            case il::support::Severity::Note:
                info.severity = 0;
                break;
            case il::support::Severity::Warning:
                info.severity = 1;
                break;
            case il::support::Severity::Error:
                info.severity = 2;
                break;
        }
        info.message = d.message;
        info.line = d.loc.line;
        info.column = d.loc.column;
        info.code = d.code;
        result.push_back(std::move(info));
    }
    return result;
}

} // namespace viper::server
