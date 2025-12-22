//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/ilc/cmd_codegen_arm64.hpp
// Purpose: Declare the arm64 (AArch64) code generation subcommand for ilc.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Declaration of the `ilc codegen arm64` subcommand entry point.
/// @details Exposes the CLI hook that drives AArch64 code generation from IL.
///          The implementation accepts argv-style arguments, emits assembly,
///          and can optionally assemble, link, and run native output.

#pragma once

namespace viper::tools::ilc
{

/// @brief Execute the `ilc codegen arm64` subcommand.
/// @details Parses the command-line arguments for the arm64 backend, then
///          lowers IL to AArch64 assembly and optionally assembles/links native
///          output depending on flags such as `-S`, `-o`, and `-run-native`.
///          Errors are reported to stderr and surfaced via a non-zero return
///          code to align with the rest of the ilc toolchain.
/// @param argc Number of command-line arguments in @p argv.
/// @param argv Argument vector where argv[0] is the subcommand name.
/// @return Zero on success; non-zero on argument parsing, IO, or codegen errors.
int cmd_codegen_arm64(int argc, char **argv);

} // namespace viper::tools::ilc
