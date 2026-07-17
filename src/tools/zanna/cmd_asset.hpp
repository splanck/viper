//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/zanna/cmd_asset.hpp
// Purpose: `zanna asset` subcommand entry points (offline 3D asset bake and
//   validation).
// Key invariants:
//   - cmdAsset receives argv positioned at the subcommand token.
// Ownership/Lifetime:
//   - Stateless entry points.
// Links: cmd_asset.cpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdio>

/// @brief `zanna asset <bake|validate> ...`; returns the process exit code.
int cmdAsset(int argc, char **argv);

/// @brief Print `zanna asset` usage to @p out (used by `zanna help asset`).
int cmdAssetHelp(std::FILE *out);

