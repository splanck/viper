// File: src/frontends/basic/BuiltinRegistry.hpp
// Purpose: Central registry of BASIC built-in functions mapping names to
//          semantic and lowering hooks.
// Key invariants: Table order matches BuiltinCallExpr::Builtin enum.
// Ownership/Lifetime: Static compile-time data only; no dynamic allocation.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace il::frontends::basic
{
/// @brief Metadata for a BASIC built-in function.
struct BuiltinInfo
{
    const char *name;    ///< BASIC source spelling.
    std::size_t minArgs; ///< Minimum accepted arguments.
    std::size_t maxArgs; ///< Maximum accepted arguments.

    using AnalyzeFn = SemanticAnalyzer::Type (SemanticAnalyzer::*)(
        const BuiltinCallExpr &, const std::vector<SemanticAnalyzer::Type> &);
    AnalyzeFn analyze; ///< Semantic analysis hook.

    using LowerFn = typename Lowerer::RVal (Lowerer::*)(const BuiltinCallExpr &);
    LowerFn lower; ///< Lowering hook.

    using ScanFn = typename Lowerer::ExprType (Lowerer::*)(const BuiltinCallExpr &);
    ScanFn scan; ///< Pre-lowering scan hook.
};

/// @brief Lookup builtin info by enum.
const BuiltinInfo &getBuiltinInfo(BuiltinCallExpr::Builtin b);

/// @brief Find builtin enum by BASIC name.
std::optional<BuiltinCallExpr::Builtin> lookupBuiltin(std::string_view name);

} // namespace il::frontends::basic
