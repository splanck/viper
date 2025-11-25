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

#pragma once

namespace viper::tools::ilc
{

// Handle `ilc codegen arm64` argv-style invocation. Currently supports only
// emitting assembly (`-S <out.s>`) using the minimal AArch64 AsmEmitter.
int cmd_codegen_arm64(int argc, char **argv);

} // namespace viper::tools::ilc
