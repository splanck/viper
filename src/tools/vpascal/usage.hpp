//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/vpascal/usage.hpp
// Purpose: Declarations for vpascal help and usage text.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// Links: docs/cli-redesign-plan.md
//
//===----------------------------------------------------------------------===//

#pragma once

namespace vpascal
{

/// @brief Print usage information for the vpascal command-line tool.
///
/// @details Displays synopsis, common usage patterns, and available options.
/// Designed to be user-friendly for newcomers to Viper Pascal.
void printUsage();

/// @brief Print version information for vpascal.
void printVersion();

} // namespace vpascal
