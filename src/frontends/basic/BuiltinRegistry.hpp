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

};

/// @brief Lookup builtin info by enum.
const BuiltinInfo &getBuiltinInfo(BuiltinCallExpr::Builtin b);

/// @brief Find builtin enum by BASIC name.
std::optional<BuiltinCallExpr::Builtin> lookupBuiltin(std::string_view name);

/// @brief Declarative scan rule describing runtime requirements for a builtin.
struct BuiltinScanRule
{
    /// @brief How the builtin's result type should be computed.
    struct ResultSpec
    {
        /// @brief Result strategy used during scanning.
        enum class Kind
        {
            Fixed,    ///< Always yields the @ref type supplied below.
            FromArg,  ///< Mirrors the type inferred for a specific argument.
        } kind{Kind::Fixed};

        Lowerer::ExprType type{Lowerer::ExprType::I64}; ///< Fixed type or fallback.
        std::size_t argIndex{0};                        ///< Argument index for FromArg.
    } result{};

    /// @brief How scanning should traverse the builtin's argument list.
    enum class ArgTraversal
    {
        All,       ///< Visit every provided argument in order.
        Explicit,  ///< Visit only the indexes listed in @ref explicitArgs.
    } traversal{ArgTraversal::All};

    /// @brief Specific argument indexes to traverse when @ref traversal is Explicit.
    std::vector<std::size_t> explicitArgs{};

    /// @brief Runtime feature toggles evaluated after scanning arguments.
    struct Feature
    {
        /// @brief Which Lowerer helper should be invoked for the feature.
        enum class Action
        {
            Request, ///< Call Lowerer::requestHelper.
            Track,   ///< Call Lowerer::trackRuntime.
        } action{Action::Request};

        /// @brief Conditional guard controlling whether the feature fires.
        enum class Condition
        {
            Always,          ///< Unconditionally perform the action.
            IfArgPresent,    ///< Fire when the argument at @ref argIndex exists.
            IfArgMissing,    ///< Fire when the argument at @ref argIndex is absent.
            IfArgTypeIs,     ///< Fire when argument type matches @ref type.
            IfArgTypeIsNot,  ///< Fire when argument type differs from @ref type.
        } condition{Condition::Always};

        il::runtime::RuntimeFeature feature{il::runtime::RuntimeFeature::Count}; ///< Runtime feature to toggle.
        std::size_t argIndex{0};                        ///< Argument inspected by the condition.
        Lowerer::ExprType type{Lowerer::ExprType::I64}; ///< Type operand for type-guarded rules.
    };

    std::vector<Feature> features{}; ///< Ordered feature actions evaluated after argument scans.
};

/// @brief Fetch the declarative scan rule for a builtin.
const BuiltinScanRule &getBuiltinScanRule(BuiltinCallExpr::Builtin b);

} // namespace il::frontends::basic
