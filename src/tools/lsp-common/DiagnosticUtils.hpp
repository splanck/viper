//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/lsp-common/DiagnosticUtils.hpp
// Purpose: Shared utility for converting DiagnosticEngine results to
//          server-agnostic DiagnosticInfo structs.
// Key invariants:
//   - Works with any frontend's DiagnosticEngine output
// Ownership/Lifetime:
//   - All returned data is fully owned
// Links: tools/lsp-common/ServerTypes.hpp, support/diagnostics.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "tools/lsp-common/ServerTypes.hpp"

#include <vector>

namespace il::support
{
class DiagnosticEngine;
}

namespace viper::server
{

/// @brief Extract structured diagnostics from a DiagnosticEngine.
std::vector<DiagnosticInfo> extractDiagnostics(const il::support::DiagnosticEngine &diag);

} // namespace viper::server
