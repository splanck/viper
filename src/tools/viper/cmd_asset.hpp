//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/viper/cmd_asset.hpp
// Purpose: `viper asset` subcommand entry points (offline 3D asset bake and
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

/// @brief `viper asset <bake|validate> ...`; returns the process exit code.
int cmdAsset(int argc, char **argv);

/// @brief Print `viper asset` usage to @p out (used by `viper help asset`).
int cmdAssetHelp(std::FILE *out);

