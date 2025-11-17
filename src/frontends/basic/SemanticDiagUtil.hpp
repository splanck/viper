//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/SemanticDiagUtil.hpp
// Purpose: Small helpers to standardize formatting and emission of common
//          semantic diagnostics across the BASIC frontend.
// Key invariants:
//   - Candidate lists are sorted case-insensitively for determinism.
//   - Diagnostic codes and severities come from the generated catalog.
// Ownership/Lifetime: Header-only helpers; no state.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "viper/diag/BasicDiag.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace il::frontends::basic::semutil
{

/// @brief Format a candidate list for ambiguity diagnostics.
/// @details Sorts case-insensitively and uppercases items; joins with ", ".
inline std::string formatCandidateList(std::vector<std::string> candidates)
{
    auto toLower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    std::sort(candidates.begin(), candidates.end(), [&](const std::string &a, const std::string &b) {
        const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i)
        {
            char ca = toLower(a[i]);
            char cb = toLower(b[i]);
            if (ca != cb)
                return ca < cb;
        }
        return a.size() < b.size();
    });
    // Uppercase and join
    std::string out;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (i)
            out += ", ";
        for (char ch : candidates[i])
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return out;
}

/// @brief Emit E_NS_003 ambiguous type diagnostic via the shared emitter.
inline void emitAmbiguousType(il::frontends::basic::DiagnosticEmitter &emitter,
                              il::support::SourceLoc loc,
                              uint32_t length,
                              const std::string &typeName,
                              const std::vector<std::string> &candidates)
{
    using il::frontends::basic::diag::BasicDiag;
    const auto sev = il::frontends::basic::diag::getSeverity(BasicDiag::NsAmbiguousType);
    const auto code = std::string(il::frontends::basic::diag::getCode(BasicDiag::NsAmbiguousType));
    const auto cand = formatCandidateList(candidates);
    const auto msg = il::frontends::basic::diag::formatMessage(
        BasicDiag::NsAmbiguousType,
        {{"type", typeName}, {"candidates", cand}});
    emitter.emit(sev, code, loc, length, msg);
}

} // namespace il::frontends::basic::semutil
