// File: src/frontends/basic/BuiltinRegistry.hpp
// Purpose: Central registry of BASIC built-in functions mapping names to
//          semantic and lowering hooks.
// Key invariants: Table order matches BuiltinCallExpr::Builtin enum.
// Ownership/Lifetime: Static compile-time data only; no dynamic allocation.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include "il/core/Opcode.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace il::frontends::basic
{
namespace lower
{
class BuiltinLowerContext;
} // namespace lower

/// @brief Declarative scan rule describing runtime requirements for a builtin.
struct BuiltinScanRule
{
    /// @brief How the builtin's result type should be computed.
    struct ResultSpec
    {
        /// @brief Result strategy used during scanning.
        enum class Kind
        {
            Fixed,   ///< Always yields the @ref type supplied below.
            FromArg, ///< Mirrors the type inferred for a specific argument.
        } kind{Kind::Fixed};

        Lowerer::ExprType type{Lowerer::ExprType::I64}; ///< Fixed type or fallback.
        std::size_t argIndex{0};                        ///< Argument index for FromArg.
    } result{};

    /// @brief How scanning should traverse the builtin's argument list.
    enum class ArgTraversal
    {
        All,      ///< Visit every provided argument in order.
        Explicit, ///< Visit only the indexes listed in @ref explicitArgs.
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
            Always,         ///< Unconditionally perform the action.
            IfArgPresent,   ///< Fire when the argument at @ref argIndex exists.
            IfArgMissing,   ///< Fire when the argument at @ref argIndex is absent.
            IfArgTypeIs,    ///< Fire when argument type matches @ref type.
            IfArgTypeIsNot, ///< Fire when argument type differs from @ref type.
        } condition{Condition::Always};

        il::runtime::RuntimeFeature feature{
            il::runtime::RuntimeFeature::Count};        ///< Runtime feature to toggle.
        std::size_t argIndex{0};                        ///< Argument inspected by the condition.
        Lowerer::ExprType type{Lowerer::ExprType::I64}; ///< Type operand for type-guarded rules.
    };

    std::vector<Feature> features{}; ///< Ordered feature actions evaluated after argument scans.
};

/// @brief Fetch the declarative scan rule for a builtin.
const BuiltinScanRule &getBuiltinScanRule(BuiltinCallExpr::Builtin b);

/// @brief Declarative lowering rule describing runtime invocation for a builtin.
struct BuiltinLoweringRule
{
    /// @brief Result strategy mirroring @ref BuiltinScanRule::ResultSpec.
    struct ResultSpec
    {
        /// @brief How the result type is determined at lowering time.
        enum class Kind
        {
            Fixed,   ///< Always use the supplied @ref type.
            FromArg, ///< Mirror the type of an argument index.
        } kind{Kind::Fixed};

        Lowerer::ExprType type{Lowerer::ExprType::I64}; ///< Result type when fixed.
        std::size_t argIndex{0};                        ///< Referenced argument index.
    };

    /// @brief Runtime helper invocation tracking used during lowering.
    struct Feature
    {
        /// @brief Which runtime tracking API to invoke.
        enum class Action
        {
            Request, ///< Call @ref Lowerer::requestHelper.
            Track,   ///< Call @ref Lowerer::trackRuntime.
        } action{Action::Request};

        il::runtime::RuntimeFeature feature{
            il::runtime::RuntimeFeature::Count}; ///< Runtime feature identifier.
    };

    /// @brief Transformation applied to an argument prior to emission.
    struct ArgTransform
    {
        /// @brief Specific adjustment performed on the argument value.
        enum class Kind
        {
            EnsureI64,  ///< Invoke @ref Lowerer::ensureI64.
            EnsureF64,  ///< Invoke @ref Lowerer::ensureF64.
            EnsureI32,  ///< Invoke @ref Lowerer::ensureI64 followed by a narrow to i32.
            CoerceI64,  ///< Invoke @ref Lowerer::coerceToI64.
            CoerceF64,  ///< Invoke @ref Lowerer::coerceToF64.
            CoerceBool, ///< Invoke @ref Lowerer::coerceToBool.
            AddConst,   ///< Emit an add with an integer constant.
        } kind{Kind::EnsureI64};

        std::int64_t immediate{0}; ///< Constant used by AddConst.
    };

    /// @brief Argument description for runtime emission.
    struct Argument
    {
        std::size_t index{0};                   ///< Index into BuiltinCallExpr::args.
        std::vector<ArgTransform> transforms{}; ///< Transformations to apply before use.

        struct DefaultValue
        {
            Lowerer::ExprType type{Lowerer::ExprType::I64}; ///< Expression type of the default.
            double f64{0.0};                                ///< Floating default payload.
            std::int64_t i64{0};                            ///< Integer default payload.
        };

        std::optional<DefaultValue> defaultValue{}; ///< Optional default when argument absent.
    };

    /// @brief Variant describing how to materialize the builtin call.
    struct Variant
    {
        /// @brief Selection predicate for the variant.
        enum class Condition
        {
            Always,        ///< Applies unconditionally.
            IfArgPresent,  ///< Applies when argument exists.
            IfArgMissing,  ///< Applies when argument is absent.
            IfArgTypeIs,   ///< Applies when argument type matches.
            IfArgTypeIsNot ///< Applies when argument type differs.
        } condition{Condition::Always};

        std::size_t conditionArg{0}; ///< Argument inspected by the condition.
        Lowerer::ExprType conditionType{
            Lowerer::ExprType::I64};             ///< Type operand for type-based predicates.
        std::optional<std::size_t> callLocArg{}; ///< Optional argument providing emission location.

        /// @brief Lowering strategy for the variant.
        enum class Kind
        {
            CallRuntime, ///< Call a runtime helper.
            EmitUnary,   ///< Emit a unary opcode.
            Custom,      ///< Reserved for bespoke handlers.
        } kind{Kind::CallRuntime};

        const char *runtime{nullptr}; ///< Runtime symbol name when calling helpers.
        il::core::Opcode opcode{
            il::core::Opcode::IAddOvf};    ///< Unary opcode when @ref kind == EmitUnary.
        std::vector<Argument> arguments{}; ///< Ordered argument specifications.
        std::vector<Feature> features{};   ///< Feature tracking applied for this variant.
    };

    ResultSpec result{};             ///< Result determination rule.
    std::vector<Variant> variants{}; ///< Ordered set of variants; first match is used.
};

/// @brief Metadata for a BASIC built-in function.
struct BuiltinInfo
{
    const char *name;                          ///< BASIC source spelling.
    SemanticAnalyzer::BuiltinAnalyzer analyze; ///< Optional semantic handler.
};

/// @brief Lookup builtin info by enum.
const BuiltinInfo &getBuiltinInfo(BuiltinCallExpr::Builtin b);

/// @brief Find builtin enum by BASIC name.
std::optional<BuiltinCallExpr::Builtin> lookupBuiltin(std::string_view name);

/// @brief Fetch the lowering rule for a builtin.
const BuiltinLoweringRule &getBuiltinLoweringRule(BuiltinCallExpr::Builtin b);

/// @brief Function signature used by builtin lowering handlers.
using BuiltinHandler = Lowerer::RVal (*)(lower::BuiltinLowerContext &);

/// @brief Register a lowering handler for the builtin spelled @p name.
void register_builtin(std::string_view name, BuiltinHandler fn);

/// @brief Locate the lowering handler associated with @p name.
BuiltinHandler find_builtin(std::string_view name);

} // namespace il::frontends::basic
