// File: src/frontends/basic/builtins/MathBuiltins.cpp
// Purpose: Implements registration helpers for BASIC math builtins, supplying
//          metadata consumed by semantic analysis and lowering.
// Key invariants: Registration populates tables using enum ordinals without
//                 reallocating storage.
// Ownership/Lifetime: Tables are owned by the registry; this unit only mutates
//                     spans passed by the caller.
// Links: docs/codemap.md

#include "frontends/basic/builtins/MathBuiltins.hpp"

#include <cstddef>

namespace il::frontends::basic::builtins
{
namespace
{
using B = BuiltinCallExpr::Builtin;

constexpr std::size_t idx(B b) noexcept
{
    return static_cast<std::size_t>(b);
}

} // namespace

void registerMathBuiltinInfos(std::span<BuiltinInfo> infos)
{
    infos[idx(B::Int)] = {"INT", nullptr};
    infos[idx(B::Fix)] = {"FIX", nullptr};
    infos[idx(B::Round)] = {"ROUND", nullptr};
    infos[idx(B::Sqr)] = {"SQR", nullptr};
    infos[idx(B::Abs)] = {"ABS", &SemanticAnalyzer::analyzeAbs};
    infos[idx(B::Floor)] = {"FLOOR", nullptr};
    infos[idx(B::Ceil)] = {"CEIL", nullptr};
    infos[idx(B::Sin)] = {"SIN", nullptr};
    infos[idx(B::Cos)] = {"COS", nullptr};
    infos[idx(B::Pow)] = {"POW", nullptr};
    infos[idx(B::Rnd)] = {"RND", nullptr};
}

void registerMathBuiltinScanRules(std::span<BuiltinScanRule> rules)
{
    using ResultSpec = BuiltinScanRule::ResultSpec;
    using Feature = BuiltinScanRule::Feature;

    rules[idx(B::Int)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        BuiltinScanRule::ArgTraversal::Explicit,
        {0},
        {Feature{Feature::Action::Track,
                 Feature::Condition::Always,
                 il::runtime::RuntimeFeature::IntFloor,
                 0,
                 Lowerer::ExprType::I64}},
    };

    rules[idx(B::Fix)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        BuiltinScanRule::ArgTraversal::Explicit,
        {0},
        {Feature{Feature::Action::Track,
                 Feature::Condition::Always,
                 il::runtime::RuntimeFeature::FixTrunc,
                 0,
                 Lowerer::ExprType::I64}},
    };

    rules[idx(B::Round)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        BuiltinScanRule::ArgTraversal::Explicit,
        {0, 1},
        {Feature{Feature::Action::Track,
                 Feature::Condition::Always,
                 il::runtime::RuntimeFeature::RoundEven,
                 0,
                 Lowerer::ExprType::I64}},
    };

    rules[idx(B::Sqr)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        BuiltinScanRule::ArgTraversal::Explicit,
        {0},
        {Feature{Feature::Action::Track,
                 Feature::Condition::Always,
                 il::runtime::RuntimeFeature::Sqrt,
                 0,
                 Lowerer::ExprType::I64}},
    };

    rules[idx(B::Abs)] = {
        ResultSpec{ResultSpec::Kind::FromArg, Lowerer::ExprType::I64, 0},
        BuiltinScanRule::ArgTraversal::Explicit,
        {0},
        {Feature{Feature::Action::Track,
                 Feature::Condition::IfArgTypeIs,
                 il::runtime::RuntimeFeature::AbsF64,
                 0,
                 Lowerer::ExprType::F64},
         Feature{Feature::Action::Track,
                 Feature::Condition::IfArgTypeIsNot,
                 il::runtime::RuntimeFeature::AbsI64,
                 0,
                 Lowerer::ExprType::F64}},
    };

    rules[idx(B::Floor)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        BuiltinScanRule::ArgTraversal::Explicit,
        {0},
        {Feature{Feature::Action::Track,
                 Feature::Condition::Always,
                 il::runtime::RuntimeFeature::Floor,
                 0,
                 Lowerer::ExprType::I64}},
    };

    rules[idx(B::Ceil)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        BuiltinScanRule::ArgTraversal::Explicit,
        {0},
        {Feature{Feature::Action::Track,
                 Feature::Condition::Always,
                 il::runtime::RuntimeFeature::Ceil,
                 0,
                 Lowerer::ExprType::I64}},
    };

    rules[idx(B::Sin)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        BuiltinScanRule::ArgTraversal::Explicit,
        {0},
        {Feature{Feature::Action::Track,
                 Feature::Condition::Always,
                 il::runtime::RuntimeFeature::Sin,
                 0,
                 Lowerer::ExprType::I64}},
    };

    rules[idx(B::Cos)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        BuiltinScanRule::ArgTraversal::Explicit,
        {0},
        {Feature{Feature::Action::Track,
                 Feature::Condition::Always,
                 il::runtime::RuntimeFeature::Cos,
                 0,
                 Lowerer::ExprType::I64}},
    };

    rules[idx(B::Pow)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        BuiltinScanRule::ArgTraversal::All,
        {},
        {Feature{Feature::Action::Track,
                 Feature::Condition::Always,
                 il::runtime::RuntimeFeature::Pow,
                 0,
                 Lowerer::ExprType::I64}},
    };

    rules[idx(B::Rnd)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        BuiltinScanRule::ArgTraversal::Explicit,
        {},
        {Feature{Feature::Action::Track,
                 Feature::Condition::Always,
                 il::runtime::RuntimeFeature::Rnd,
                 0,
                 Lowerer::ExprType::I64}},
    };
}

void registerMathBuiltinLoweringRules(std::span<BuiltinLoweringRule> rules)
{
    using LowerRule = BuiltinLoweringRule;
    using ResultSpec = LowerRule::ResultSpec;
    using Variant = LowerRule::Variant;
    using Condition = Variant::Condition;
    using VariantKind = Variant::Kind;
    using Argument = LowerRule::Argument;
    using Transform = LowerRule::ArgTransform;
    using Feature = LowerRule::Feature;

    rules[idx(B::Int)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        {Variant{.condition = Condition::Always,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_int_floor",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::IntFloor}}}},
    };

    rules[idx(B::Fix)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        {Variant{.condition = Condition::Always,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_fix_trunc",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::FixTrunc}}}},
    };

    rules[idx(B::Round)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        {Variant{.condition = Condition::IfArgPresent,
                 .conditionArg = 1,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_round_even",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}},
                                Argument{.index = 1,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureI32}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::RoundEven}}},
         Variant{.condition = Condition::IfArgMissing,
                 .conditionArg = 1,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_round_even",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}},
                                Argument{.index = 1,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureI32}},
                                        .defaultValue = Argument::DefaultValue{.type = Lowerer::ExprType::I64,
                                                                               .f64 = 0.0,
                                                                               .i64 = 0}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::RoundEven}}}},
    };

    rules[idx(B::Sqr)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        {Variant{.condition = Condition::Always,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_sqrt",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::Sqrt}}}},
    };

    rules[idx(B::Abs)] = {
        ResultSpec{ResultSpec::Kind::FromArg, Lowerer::ExprType::I64, 0},
        {Variant{.condition = Condition::IfArgTypeIs,
                 .conditionArg = 0,
                 .conditionType = Lowerer::ExprType::F64,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_abs_f64",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::AbsF64}}},
         Variant{.condition = Condition::Always,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_abs_i64",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureI64}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::AbsI64}}}},
    };

    rules[idx(B::Floor)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        {Variant{.condition = Condition::Always,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_floor",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::Floor}}}},
    };

    rules[idx(B::Ceil)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        {Variant{.condition = Condition::Always,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_ceil",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::Ceil}}}},
    };

    rules[idx(B::Sin)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        {Variant{.condition = Condition::Always,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_sin",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::Sin}}}},
    };

    rules[idx(B::Cos)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        {Variant{.condition = Condition::Always,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_cos",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::Cos}}}},
    };

    rules[idx(B::Pow)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        {Variant{.condition = Condition::Always,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_pow_f64_chkdom",
                 .arguments = {Argument{.index = 0,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}},
                                Argument{.index = 1,
                                        .transforms = {Transform{.kind = Transform::Kind::EnsureF64}}}},
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::Pow}}}},
    };

    rules[idx(B::Rnd)] = {
        ResultSpec{ResultSpec::Kind::Fixed, Lowerer::ExprType::F64, 0},
        {Variant{.condition = Condition::Always,
                 .kind = VariantKind::CallRuntime,
                 .runtime = "rt_rnd",
                 .features = {Feature{.action = Feature::Action::Track,
                                      .feature = il::runtime::RuntimeFeature::Rnd}}}},
    };
}

} // namespace il::frontends::basic::builtins
