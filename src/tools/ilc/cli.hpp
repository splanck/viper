// File: src/tools/ilc/cli.hpp
// Purpose: Declarations for ilc subcommand handlers.
// Key invariants: None.
// Ownership/Lifetime: Callers retain ownership of argv strings.
// Links: docs/class-catalog.md

#pragma once

/// @brief Handle BASIC front-end commands after `ilc front basic`.
/// @param argc Count of arguments following `ilc front basic`.
/// @param argv Argument array starting after `ilc front basic`.
/// @return Process exit code.
int cmdFrontBasic(int argc, char **argv);

/// @brief Run an IL module.
/// @param argc Count of arguments following `ilc -run`.
/// @param argv Argument array starting after `ilc -run`.
/// @return Process exit code.
int cmdRunIL(int argc, char **argv);

/// @brief Optimize an IL module with transformation passes.
/// @param argc Count of arguments following `ilc il-opt`.
/// @param argv Argument array starting after `ilc il-opt`.
/// @return Process exit code.
int cmdILOpt(int argc, char **argv);
