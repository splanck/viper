// File: src/il/runtime/RuntimeSignatureParser.hpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Declares helpers for parsing runtime signature specifications.
// Key invariants: Parsing utilities interpret specifications used in runtime data tables.
// Ownership/Lifetime: Stateless free functions operating on provided string views.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/runtime/RuntimeSignatures.hpp"

#include <string_view>
#include <vector>

namespace il::runtime
{

/// @brief Trim ASCII whitespace from both ends of the provided view.
std::string_view trim(std::string_view text);

/// @brief Split a delimited type list into individual type tokens.
std::vector<std::string_view> splitTypeList(std::string_view text);

/// @brief Parse a runtime signature specification string.
RuntimeSignature parseSignatureSpec(std::string_view spec);

} // namespace il::runtime
