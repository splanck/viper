//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/ilc/cmd_codegen_x64.hpp
// Purpose: Declare the x86-64 code generation subcommand glue for ilc.
// Key invariants: Interfaces remain minimal shims around the backend facade.
// Ownership/Lifetime: Functions borrow CLI state and return status codes by value.
// Links: docs/codemap.md, src/codegen/x86_64/Backend.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

namespace viper::tools::ilc
{
class CLI;

/// \brief Handle `ilc codegen x64` command line invocations.
///
/// The handler coordinates parsing of code generation specific flags, loads the
/// requested IL module, converts it into the temporary adapter representation
/// consumed by the Phase A backend, and emits assembly or native binaries as
/// directed by the command line.
///
/// \param argc Number of arguments following the `codegen x64` tokens.
/// \param argv Argument vector beginning with the input IL file path.
/// \return `0` on success or a non-zero diagnostic code on failure.
int cmdCodegenX64(int argc, char **argv);

/// \brief Register the codegen x64 subcommand with a higher level CLI driver.
///
/// Current builds dispatch via hand-written argument parsing and therefore do
/// not integrate with a structured CLI object yet. The function exists to keep
/// the surface area stable as the driver evolves.
///
/// \param cli CLI facade receiving the new subcommand registration.
void register_codegen_x64_commands(CLI &cli);

} // namespace viper::tools::ilc

//===----------------------------------------------------------------------===//
