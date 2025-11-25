//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/ilc/break_spec.hpp
// Purpose: Helpers for parsing --break flag specifications.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace ilc
{

/// @brief Determine whether a --break argument refers to a source line.
///
/// A source break specification is written as `<file>:<line>` where the left
/// side contains at least one non-whitespace character and the right side is a
/// decimal line number.
/// @param spec Single token supplied to the `--break` flag.
/// @returns `true` when the token matches the source break format; `false`
/// otherwise.
/// @note This check is purely syntactic and does not verify file existence or
/// line bounds.
bool isSrcBreakSpec(const std::string &spec);

} // namespace ilc
