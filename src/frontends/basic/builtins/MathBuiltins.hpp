//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/builtins/MathBuiltins.hpp
// Purpose: Declares helpers for registering BASIC math builtins in the central 
// Key invariants: Registration writes entries matching BuiltinCallExpr::Builtin
// Ownership/Lifetime: Functions mutate caller-provided tables in-place.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include "frontends/basic/BuiltinRegistry.hpp"

namespace il::frontends::basic::builtins
{

/// @brief Populate builtin metadata for math helpers (INT/FIX/ABS/etc.).
/// @param infos Dense table indexed by BuiltinCallExpr::Builtin.
void registerMathBuiltinInfos(std::span<BuiltinInfo> infos);

/// @brief Populate scan rules describing math builtin traversal.
/// @param rules Dense table indexed by BuiltinCallExpr::Builtin.
void registerMathBuiltinScanRules(std::span<BuiltinScanRule> rules);

/// @brief Populate lowering rules describing math builtin emission.
/// @param rules Dense table indexed by BuiltinCallExpr::Builtin.
void registerMathBuiltinLoweringRules(std::span<BuiltinLoweringRule> rules);

} // namespace il::frontends::basic::builtins
