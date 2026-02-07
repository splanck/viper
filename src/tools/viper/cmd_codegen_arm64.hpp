//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the entry point for the `ilc codegen arm64` CLI
// subcommand, which drives AArch64 native code generation from Viper IL.
//
// The subcommand accepts an IL file path and optional flags controlling
// output format (-S for assembly, -o for object/executable), optimization
// level, and native execution (-run-native). It loads the IL module,
// invokes the AArch64 codegen pipeline, and optionally assembles and
// links the output.
//
// Key invariants:
//   - Returns 0 on success, non-zero on any error.
//   - Error diagnostics are written to stderr.
//   - The subcommand does not modify the input IL file.
//
// Ownership: The function borrows argv strings for the duration of the call.
// All generated files are written to the paths specified by command-line flags.
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
