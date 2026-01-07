//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/text/search.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tui/text/text_buffer.hpp"

namespace viper::tui::text
{
/// @brief Match result range in bytes.
struct Match
{
    size_t start{0};
    size_t length{0};
};

/// @brief Find all matches of query in buffer.
/// @param buf TextBuffer to search.
/// @param query Literal text or regex pattern.
/// @param useRegex Interpret query as regex when true.
[[nodiscard]] std::vector<Match> findAll(const TextBuffer &buf,
                                         std::string_view query,
                                         bool useRegex);

/// @brief Find next match starting at or after offset.
/// @param buf TextBuffer to search.
/// @param query Literal text or regex pattern.
/// @param from Starting byte offset within buffer.
/// @param useRegex Interpret query as regex when true.
/// @return Match if found.
[[nodiscard]] std::optional<Match> findNext(const TextBuffer &buf,
                                            std::string_view query,
                                            size_t from,
                                            bool useRegex);
} // namespace viper::tui::text
