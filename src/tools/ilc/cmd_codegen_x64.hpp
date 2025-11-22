//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tools/ilc/cmd_codegen_x64.hpp
// Purpose: Declare the x86-64 code generation subcommand glue for ilc. 
// Key invariants: Interfaces expose configuration and command entry points without owning
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

namespace viper::tools::ilc
{

struct CLI; ///< Forward declaration for future structured CLI integration.

/// \brief Handle `ilc codegen x64` invocations using argv-style arguments.
///
/// The handler parses command-line flags controlling assembly emission, optional
/// linking, and native execution before dispatching to the x86-64 backend. It is
/// intentionally minimal and focuses on orchestrating file I/O and subprocess
/// calls while more advanced argument validation is wired into the main driver.
///
/// \param argc Number of arguments in the `argv` array.
/// \param argv Argument array beginning with the IL input path.
/// \return `0` on success; non-zero exit codes propagate backend, linker, or
///         execution failures.
int cmd_codegen_x64(int argc, char **argv);

/// \brief Register the x86-64 code generation commands with a structured CLI front end.
///
/// Projects embedding ilc within a richer command-line parser can wire the
/// x86-64 backend by invoking this helper. The current implementation is a
/// placeholder until the CLI abstraction lands.
void register_codegen_x64_commands(CLI &cli);

} // namespace viper::tools::ilc
