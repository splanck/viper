// File: src/tools/vbasic/usage.hpp
// Purpose: Declarations for vbasic help and usage text.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// Links: docs/cli-redesign-plan.md

#pragma once

namespace vbasic
{

/// @brief Print usage information for the vbasic command-line tool.
///
/// @details Displays synopsis, common usage patterns, and available options.
/// Designed to be user-friendly for newcomers to Viper BASIC.
void printUsage();

/// @brief Print version information for vbasic.
void printVersion();

} // namespace vbasic
