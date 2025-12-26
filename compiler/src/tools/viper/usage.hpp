//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Declarations for ViperLang CLI usage and version text.
/// @details Used by the `viper` driver and legacy compatibility shims to keep
///          help output and version reporting centralized.

#pragma once

namespace viperlang
{

/// @brief Print usage information for the ViperLang CLI.
/// @details Writes the supported invocation forms, option descriptions, and
///          examples to stderr. The text is intended to be user-facing and is
///          kept in sync with the command-line parser.
void printUsage();

/// @brief Print version information for the ViperLang CLI.
/// @details Writes the tool version, product name, and IL version to stdout so
///          scripts can capture the output without scanning stderr.
void printVersion();

} // namespace viperlang
