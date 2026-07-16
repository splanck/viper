//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: support/diag_catalog.hpp
// Purpose: Declares the diagnostic-code catalog backing `viper explain` and
//          `viper --print-error-codes`.
// Key invariants:
//   - Catalog entries are unique by code and stable across a release.
//   - Lookup never fails hard: unknown codes fall back to a prefix-family
//     description when the prefix is recognized.
// Ownership/Lifetime:
//   - All entries are static data with program lifetime.
// Links: support/diag_catalog.def, docs/tools/debugging.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace il::support {

/// @brief One catalog entry describing a diagnostic code.
/// @invariant code and subsystem are non-empty; summary is one sentence.
/// @ownership Static storage; string_views point at string literals.
struct DiagCatalogEntry {
    std::string_view code;      ///< Stable diagnostic code (e.g., "V-ZIA-UNDEFINED").
    std::string_view subsystem; ///< Emitting subsystem (e.g., "zia-sema").
    std::string_view summary;   ///< One-sentence description of the condition.
};

/// @brief All cataloged diagnostic codes in definition order.
const std::vector<DiagCatalogEntry> &diagCatalog();

/// @brief All cataloged diagnostic codes as a static contiguous view.
/// @return Span over program-lifetime catalog entries in definition order.
/// @details This avoids constructing or iterating a std::vector for callers that
///          only need a read-only view over the compiled-in catalog data.  The
///          legacy @ref diagCatalog API remains available for existing code.
std::span<const DiagCatalogEntry> diagCatalogEntries();

/// @brief Find the catalog entry for @p code.
/// @return The entry, or nullptr when the code is not cataloged.
const DiagCatalogEntry *findDiagCode(std::string_view code);

/// @brief Describe the subsystem family for @p code from its prefix.
/// @details Used as a fallback for codes that are not (yet) cataloged, so
///          tooling can always say which component emitted a diagnostic.
/// @return Subsystem description, or std::nullopt for unrecognized prefixes.
std::optional<std::string_view> diagCodeFamily(std::string_view code);

} // namespace il::support
