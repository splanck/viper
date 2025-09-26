// File: src/frontends/basic/BuiltinRegistry.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements registry of BASIC built-ins for semantic analysis and
//          lowering dispatch.
// Key invariants: Registry entries correspond 1:1 with BuiltinCallExpr::Builtin
//                 enum order.
// Ownership/Lifetime: Static data only.
// Links: docs/codemap.md

#include "frontends/basic/BuiltinRegistry.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include <array>
#include <unordered_map>

namespace il::frontends::basic
{
namespace
{
using B = BuiltinCallExpr::Builtin;
using LowerRule = BuiltinLoweringRule;
using ResultSpec = LowerRule::ResultSpec;
using Variant = LowerRule::Variant;
using Condition = Variant::Condition;
using VariantKind = Variant::Kind;
using Argument = LowerRule::Argument;
using Transform = LowerRule::ArgTransform;
using TransformKind = LowerRule::ArgTransform::Kind;
using Feature = LowerRule::Feature;
using FeatureAction = LowerRule::Feature::Action;

static const std::array<LowerRule, 23> kBuiltinLoweringRules = {{
    { // LEN
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_len",
                    .arguments = {Argument{.index = 0}}},
        },
    },
    { // MID$
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {
            Variant{.condition = Condition::IfArgPresent,
                    .conditionArg = 2,
                    .callLocArg = 2,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_mid3",
                    .arguments = {Argument{.index = 0},
                                  Argument{.index = 1,
                                           .transforms = {Transform{.kind = TransformKind::EnsureI64},
                                                          Transform{.kind = TransformKind::AddConst, .immediate = -1}}},
                                  Argument{.index = 2,
                                           .transforms = {Transform{.kind = TransformKind::EnsureI64}}}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Mid3}}},
            Variant{.condition = Condition::IfArgMissing,
                    .conditionArg = 2,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_mid2",
                    .arguments = {Argument{.index = 0},
                                  Argument{.index = 1,
                                           .transforms = {Transform{.kind = TransformKind::EnsureI64},
                                                          Transform{.kind = TransformKind::AddConst, .immediate = -1}}}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Mid2}}},
        },
    },
    { // LEFT$
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_left",
                    .arguments = {Argument{.index = 0},
                                  Argument{.index = 1,
                                           .transforms = {Transform{.kind = TransformKind::EnsureI64}}}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Left}}},
        },
    },
    { // RIGHT$
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_right",
                    .arguments = {Argument{.index = 0},
                                  Argument{.index = 1,
                                           .transforms = {Transform{.kind = TransformKind::EnsureI64}}}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Right}}},
        },
    },
    { // STR$
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {
            Variant{.condition = Condition::IfArgTypeIs,
                    .conditionArg = 0,
                    .conditionType = Lowerer::ExprType::F64,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_f64_to_str",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::F64ToStr}}},
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_int_to_str",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureI64}}}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::IntToStr}}},
        },
    },
    { // VAL
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_to_int",
                    .arguments = {Argument{.index = 0}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::ToInt}}},
        },
    },
    { // INT
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::EmitUnary,
                    .opcode = il::core::Opcode::CastFpToSiRteChk,
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureF64}}}}},
        },
    },
    { // SQR
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::F64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_sqrt",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                    .features = {Feature{.action = FeatureAction::Track,
                                         .feature = il::runtime::RuntimeFeature::Sqrt}}},
        },
    },
    { // ABS
        .result = {.kind = ResultSpec::Kind::FromArg, .type = Lowerer::ExprType::I64, .argIndex = 0},
        .variants = {
            Variant{.condition = Condition::IfArgTypeIs,
                    .conditionArg = 0,
                    .conditionType = Lowerer::ExprType::F64,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_abs_f64",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                    .features = {Feature{.action = FeatureAction::Track,
                                         .feature = il::runtime::RuntimeFeature::AbsF64}}},
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_abs_i64",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureI64}}}},
                    .features = {Feature{.action = FeatureAction::Track,
                                         .feature = il::runtime::RuntimeFeature::AbsI64}}},
        },
    },
    { // FLOOR
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::F64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_floor",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                    .features = {Feature{.action = FeatureAction::Track,
                                         .feature = il::runtime::RuntimeFeature::Floor}}},
        },
    },
    { // CEIL
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::F64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_ceil",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                    .features = {Feature{.action = FeatureAction::Track,
                                         .feature = il::runtime::RuntimeFeature::Ceil}}},
        },
    },
    { // SIN
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::F64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_sin",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                    .features = {Feature{.action = FeatureAction::Track,
                                         .feature = il::runtime::RuntimeFeature::Sin}}},
        },
    },
    { // COS
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::F64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_cos",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                    .features = {Feature{.action = FeatureAction::Track,
                                         .feature = il::runtime::RuntimeFeature::Cos}}},
        },
    },
    { // POW
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::F64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_pow_f64_chkdom",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureF64}}},
                                  Argument{.index = 1,
                                           .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                    .features = {Feature{.action = FeatureAction::Track,
                                         .feature = il::runtime::RuntimeFeature::Pow}}},
        },
    },
    { // RND
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::F64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_rnd",
                    .features = {Feature{.action = FeatureAction::Track,
                                         .feature = il::runtime::RuntimeFeature::Rnd}}},
        },
    },
    { // INSTR
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {
            Variant{.condition = Condition::IfArgPresent,
                    .conditionArg = 2,
                    .callLocArg = 2,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_instr3",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureI64},
                                                          Transform{.kind = TransformKind::AddConst, .immediate = -1}}},
                                  Argument{.index = 1},
                                  Argument{.index = 2}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Instr3}}},
            Variant{.condition = Condition::IfArgMissing,
                    .conditionArg = 2,
                    .callLocArg = 1,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_instr2",
                    .arguments = {Argument{.index = 0}, Argument{.index = 1}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Instr2}}},
        },
    },
    { // LTRIM$
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_ltrim",
                    .arguments = {Argument{.index = 0}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Ltrim}}},
        },
    },
    { // RTRIM$
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_rtrim",
                    .arguments = {Argument{.index = 0}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Rtrim}}},
        },
    },
    { // TRIM$
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_trim",
                    .arguments = {Argument{.index = 0}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Trim}}},
        },
    },
    { // UCASE$
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_ucase",
                    .arguments = {Argument{.index = 0}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Ucase}}},
        },
    },
    { // LCASE$
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_lcase",
                    .arguments = {Argument{.index = 0}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Lcase}}},
        },
    },
    { // CHR$
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_chr",
                    .arguments = {Argument{.index = 0,
                                           .transforms = {Transform{.kind = TransformKind::EnsureI64}}}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Chr}}},
        },
    },
    { // ASC
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {
            Variant{.condition = Condition::Always,
                    .kind = VariantKind::CallRuntime,
                    .runtime = "rt_asc",
                    .arguments = {Argument{.index = 0}},
                    .features = {Feature{.action = FeatureAction::Request,
                                         .feature = il::runtime::RuntimeFeature::Asc}}},
        },
    },
}};

// Dense metadata table indexed by BuiltinCallExpr::Builtin enumerators. The
// order must remain in lockstep with the enum definition so that we can index
// directly without translation.
static const std::array<BuiltinInfo, 23> kBuiltins = {{
    {"LEN", 1, 1, &SemanticAnalyzer::analyzeLen},
    {"MID$", 2, 3, &SemanticAnalyzer::analyzeMid},
    {"LEFT$", 2, 2, &SemanticAnalyzer::analyzeLeft},
    {"RIGHT$", 2, 2, &SemanticAnalyzer::analyzeRight},
    {"STR$", 1, 1, &SemanticAnalyzer::analyzeStr},
    {"VAL", 1, 1, &SemanticAnalyzer::analyzeVal},
    {"INT", 1, 1, &SemanticAnalyzer::analyzeInt},
    {"SQR", 1, 1, &SemanticAnalyzer::analyzeSqr},
    {"ABS", 1, 1, &SemanticAnalyzer::analyzeAbs},
    {"FLOOR", 1, 1, &SemanticAnalyzer::analyzeFloor},
    {"CEIL", 1, 1, &SemanticAnalyzer::analyzeCeil},
    {"SIN", 1, 1, &SemanticAnalyzer::analyzeSin},
    {"COS", 1, 1, &SemanticAnalyzer::analyzeCos},
    {"POW", 2, 2, &SemanticAnalyzer::analyzePow},
    {"RND", 0, 0, &SemanticAnalyzer::analyzeRnd},
    {"INSTR", 2, 3, &SemanticAnalyzer::analyzeInstr},
    {"LTRIM$", 1, 1, &SemanticAnalyzer::analyzeLtrim},
    {"RTRIM$", 1, 1, &SemanticAnalyzer::analyzeRtrim},
    {"TRIM$", 1, 1, &SemanticAnalyzer::analyzeTrim},
    {"UCASE$", 1, 1, &SemanticAnalyzer::analyzeUcase},
    {"LCASE$", 1, 1, &SemanticAnalyzer::analyzeLcase},
    {"CHR$", 1, 1, &SemanticAnalyzer::analyzeChr},
    {"ASC", 1, 1, &SemanticAnalyzer::analyzeAsc},
}};

static const std::array<BuiltinScanRule, 23> kBuiltinScanRules = {{
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::I64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::Str,
                                 0},
     BuiltinScanRule::ArgTraversal::All,
     {},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::IfArgPresent,
                               il::runtime::RuntimeFeature::Mid3,
                               2,
                               Lowerer::ExprType::I64},
      BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::IfArgMissing,
                               il::runtime::RuntimeFeature::Mid2,
                               2,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::Str,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0, 1},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Left,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::Str,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0, 1},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Right,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::Str,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::IntToStr,
                               0,
                               Lowerer::ExprType::I64},
      BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::F64ToStr,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::I64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::ToInt,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::I64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::F64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Sqrt,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::FromArg,
                                 Lowerer::ExprType::I64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::IfArgTypeIs,
                               il::runtime::RuntimeFeature::AbsF64,
                               0,
                               Lowerer::ExprType::F64},
      BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::IfArgTypeIsNot,
                               il::runtime::RuntimeFeature::AbsI64,
                               0,
                               Lowerer::ExprType::F64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::F64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Floor,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::F64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Ceil,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::F64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Sin,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::F64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Cos,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::F64,
                                 0},
     BuiltinScanRule::ArgTraversal::All,
     {},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Pow,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::F64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Rnd,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::I64,
                                 0},
     BuiltinScanRule::ArgTraversal::All,
     {},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::IfArgPresent,
                               il::runtime::RuntimeFeature::Instr3,
                               2,
                               Lowerer::ExprType::I64},
      BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::IfArgMissing,
                               il::runtime::RuntimeFeature::Instr2,
                               2,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::Str,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Ltrim,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::Str,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Rtrim,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::Str,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Trim,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::Str,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Ucase,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::Str,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Lcase,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::Str,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Chr,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                 Lowerer::ExprType::I64,
                                 0},
     BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Asc,
                               0,
                               Lowerer::ExprType::I64}}},
}};

// Maps canonical BASIC source spellings to enum entries. Names must already be
// normalized to uppercase with any suffix characters (e.g., $) preserved.
static const std::unordered_map<std::string_view, B> kByName = {
    {"LEN", B::Len},      {"MID$", B::Mid},     {"LEFT$", B::Left}, {"RIGHT$", B::Right},
    {"STR$", B::Str},     {"VAL", B::Val},      {"INT", B::Int},    {"SQR", B::Sqr},
    {"ABS", B::Abs},      {"FLOOR", B::Floor},  {"CEIL", B::Ceil},  {"SIN", B::Sin},
    {"COS", B::Cos},      {"POW", B::Pow},      {"RND", B::Rnd},    {"INSTR", B::Instr},
    {"LTRIM$", B::Ltrim}, {"RTRIM$", B::Rtrim}, {"TRIM$", B::Trim}, {"UCASE$", B::Ucase},
    {"LCASE$", B::Lcase}, {"CHR$", B::Chr},     {"ASC", B::Asc},
};
} // namespace

/// @brief Fetch metadata for a BASIC built-in represented by its enum value.
/// @param b Builtin enumerator to inspect.
/// @return Reference to the metadata describing the semantic analysis hook.
/// @pre @p b must be a valid BuiltinCallExpr::Builtin enumerator; passing an
///      out-of-range value results in undefined behavior due to direct array
///      indexing.
const BuiltinInfo &getBuiltinInfo(BuiltinCallExpr::Builtin b)
{
    return kBuiltins[static_cast<std::size_t>(b)];
}

/// @brief Resolve a BASIC built-in enum from its source spelling.
/// @param name Canonical BASIC keyword spelling to look up. The string is
///        matched exactly; callers must provide the normalized uppercase form
///        including any suffix markers.
/// @return Built-in enumerator on success; std::nullopt when the name is not
///         registered.
std::optional<BuiltinCallExpr::Builtin> lookupBuiltin(std::string_view name)
{
    auto it = kByName.find(name);
    if (it == kByName.end())
        return std::nullopt;
    return it->second;
}

/// @brief Access the declarative scan rule for a BASIC builtin.
/// @param b Builtin enumerator whose rule is requested.
/// @return Reference to the rule describing argument traversal and runtime features.
const BuiltinScanRule &getBuiltinScanRule(BuiltinCallExpr::Builtin b)
{
    return kBuiltinScanRules[static_cast<std::size_t>(b)];
}

/// @brief Access the lowering rule for a BASIC builtin.
/// @param b Builtin enumerator identifying the builtin.
/// @return Reference to the declarative lowering description.
const BuiltinLoweringRule &getBuiltinLoweringRule(BuiltinCallExpr::Builtin b)
{
    return kBuiltinLoweringRules[static_cast<std::size_t>(b)];
}

} // namespace il::frontends::basic
