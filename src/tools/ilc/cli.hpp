// File: src/tools/ilc/cli.hpp
// Purpose: Declarations for ilc subcommand handlers and usage helper.
// Key invariants: None.
// Ownership/Lifetime: N/A.
// Links: docs/class-catalog.md

#pragma once

/// @brief Handle `ilc front basic` subcommands.
int cmdFrontBasic(int argc, char **argv);

/// @brief Handle `ilc -run`.
int cmdRunIL(int argc, char **argv);

/// @brief Handle `ilc il-opt`.
int cmdILOpt(int argc, char **argv);

/// @brief Print usage information for ilc.
void usage();
