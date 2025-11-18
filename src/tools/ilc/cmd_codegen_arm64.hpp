// File: src/tools/ilc/cmd_codegen_arm64.hpp
// Purpose: Declare the arm64 (AArch64) code generation subcommand for ilc.

#pragma once

namespace viper::tools::ilc
{

// Handle `ilc codegen arm64` argv-style invocation. Currently supports only
// emitting assembly (`-S <out.s>`) using the minimal AArch64 AsmEmitter.
int cmd_codegen_arm64(int argc, char **argv);

} // namespace viper::tools::ilc

