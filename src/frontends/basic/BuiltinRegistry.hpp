// File: src/frontends/basic/BuiltinRegistry.hpp
// Purpose: Central registry of BASIC built-in functions mapping names to
//          semantic and lowering hooks.
// Key invariants: Table order matches BuiltinCallExpr::Builtin enum.
// Ownership/Lifetime: Static compile-time data only; no dynamic allocation.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/Lowerer.hpp"
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace il::frontends::basic
{
/// @brief Primitive value categories supported by builtin metadata.
enum class BuiltinValueKind
{
    Int,   ///< Integral values.
    Float, ///< Floating-point values.
    String, ///< String values.
    Bool,  ///< Boolean values.
};

/// @brief Bitmask describing allowed argument types.
enum class BuiltinTypeSet : unsigned
{
    None = 0,
    Int = 1u << 0,
    Float = 1u << 1,
    String = 1u << 2,
    Bool = 1u << 3,
};

constexpr BuiltinTypeSet operator|(BuiltinTypeSet lhs, BuiltinTypeSet rhs)
{
    return static_cast<BuiltinTypeSet>(static_cast<unsigned>(lhs) |
                                       static_cast<unsigned>(rhs));
}

constexpr BuiltinTypeSet operator&(BuiltinTypeSet lhs, BuiltinTypeSet rhs)
{
    return static_cast<BuiltinTypeSet>(static_cast<unsigned>(lhs) &
                                       static_cast<unsigned>(rhs));
}

constexpr BuiltinTypeSet operator~(BuiltinTypeSet value)
{
    return static_cast<BuiltinTypeSet>(~static_cast<unsigned>(value));
}

inline bool contains(BuiltinTypeSet set, BuiltinTypeSet mask)
{
    return (set & mask) == mask;
}

/// @brief Declarative semantic rules for builtin analysis.
struct BuiltinSemantics
{
    /// @brief Result computation strategy.
    struct ResultSpec
    {
        enum class Kind
        {
            Fixed,   ///< Always returns the specified @ref type.
            FromArg, ///< Mirrors the type of @ref argIndex when available.
        } kind{Kind::Fixed};

        BuiltinValueKind type{BuiltinValueKind::Int}; ///< Fixed result or fallback type.
        std::size_t argIndex{0}; ///< Argument index inspected for FromArg results.

        static ResultSpec fixed(BuiltinValueKind t)
        {
            return {Kind::Fixed, t, 0};
        }

        static ResultSpec fromArg(std::size_t idx, BuiltinValueKind fallback)
        {
            return {Kind::FromArg, fallback, idx};
        }
    };

    /// @brief Argument specification for a single arity signature.
    struct Signature
    {
        std::vector<BuiltinTypeSet> args{}; ///< Allowed types for each argument.
    };

    std::size_t minArgs{0}; ///< Minimum accepted arguments.
    std::size_t maxArgs{0}; ///< Maximum accepted arguments.
    std::vector<Signature> signatures{}; ///< Supported arity/type combinations.
    ResultSpec result{}; ///< Result computation rule.
};

/// @brief Metadata for a BASIC built-in function.
struct BuiltinInfo
{
    const char *name;                    ///< BASIC source spelling.
    const BuiltinSemantics *semantics;   ///< Declarative semantic rules.

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
