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
#include "frontends/basic/builtins/MathBuiltins.hpp"
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

constexpr std::size_t kBuiltinCount = static_cast<std::size_t>(B::Loc) + 1;

constexpr std::size_t idx(B b) noexcept
{
    return static_cast<std::size_t>(b);
}

static const std::array<LowerRule, kBuiltinCount> kBuiltinLoweringRules = [] {
    std::array<LowerRule, kBuiltinCount> rules{};

    rules[idx(B::Len)] = LowerRule{.result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
                                   .variants = {Variant{.condition = Condition::Always,
                                                       .kind = VariantKind::CallRuntime,
                                                       .runtime = "rt_len",
                                                       .arguments = {Argument{.index = 0}}}}};

    rules[idx(B::Mid)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::IfArgPresent,
                             .conditionArg = 2,
                             .callLocArg = 2,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_mid3",
                             .arguments = {Argument{.index = 0},
                                           Argument{.index = 1,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureI64},
                                                                   Transform{.kind = TransformKind::AddConst,
                                                                             .immediate = -1}}},
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
                                                                   Transform{.kind = TransformKind::AddConst,
                                                                             .immediate = -1}}}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Mid2}}}}};

    rules[idx(B::Left)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_left",
                             .arguments = {Argument{.index = 0},
                                           Argument{.index = 1,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureI64}}}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Left}}}}};

    rules[idx(B::Right)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_right",
                             .arguments = {Argument{.index = 0},
                                           Argument{.index = 1,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureI64}}}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Right}}}}};

    rules[idx(B::Str)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .callLocArg = 0,
                             .kind = VariantKind::Custom,
                             .arguments = {Argument{.index = 0}}}}};

    rules[idx(B::Val)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::F64},
        .variants = {Variant{.condition = Condition::Always,
                             .callLocArg = 0,
                             .kind = VariantKind::Custom,
                             .runtime = "rt_val_to_double",
                             .arguments = {Argument{.index = 0}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Val}}}}};

    rules[idx(B::Cint)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {Variant{.condition = Condition::Always,
                             .callLocArg = 0,
                             .kind = VariantKind::Custom,
                             .runtime = "rt_cint_from_double",
                             .arguments = {Argument{.index = 0,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::CintFromDouble}}}}};

    rules[idx(B::Clng)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {Variant{.condition = Condition::Always,
                             .callLocArg = 0,
                             .kind = VariantKind::Custom,
                             .runtime = "rt_clng_from_double",
                             .arguments = {Argument{.index = 0,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::ClngFromDouble}}}}};

    rules[idx(B::Csng)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::F64},
        .variants = {Variant{.condition = Condition::Always,
                             .callLocArg = 0,
                             .kind = VariantKind::Custom,
                             .runtime = "rt_csng_from_double",
                             .arguments = {Argument{.index = 0,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::CsngFromDouble}}}}};

    rules[idx(B::Cdbl)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::F64},
        .variants = {Variant{.condition = Condition::Always,
                             .callLocArg = 0,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_cdbl_from_any",
                             .arguments = {Argument{.index = 0,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureF64}}}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::CdblFromAny}}}}};

    builtins::registerMathBuiltinLoweringRules(rules);

    rules[idx(B::Instr)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {Variant{.condition = Condition::IfArgPresent,
                             .conditionArg = 2,
                             .callLocArg = 2,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_instr3",
                             .arguments = {Argument{.index = 0,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureI64},
                                                                   Transform{.kind = TransformKind::AddConst,
                                                                             .immediate = -1}}},
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
                                                  .feature = il::runtime::RuntimeFeature::Instr2}}}}};

    rules[idx(B::Ltrim)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_ltrim",
                             .arguments = {Argument{.index = 0}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Ltrim}}}}};

    rules[idx(B::Rtrim)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_rtrim",
                             .arguments = {Argument{.index = 0}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Rtrim}}}}};

    rules[idx(B::Trim)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_trim",
                             .arguments = {Argument{.index = 0}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Trim}}}}};

    rules[idx(B::Ucase)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_ucase",
                             .arguments = {Argument{.index = 0}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Ucase}}}}};

    rules[idx(B::Lcase)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_lcase",
                             .arguments = {Argument{.index = 0}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Lcase}}}}};

    rules[idx(B::Chr)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_chr",
                             .arguments = {Argument{.index = 0,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureI64}}}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Chr}}}}};

    rules[idx(B::Asc)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_asc",
                             .arguments = {Argument{.index = 0}},
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::Asc}}}}};

    rules[idx(B::InKey)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_inkey_str",
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::InKey}}}}};

    rules[idx(B::GetKey)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::Str},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_getkey_str",
                             .features = {Feature{.action = FeatureAction::Request,
                                                  .feature = il::runtime::RuntimeFeature::GetKey}}}}};

    rules[idx(B::Eof)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_eof_ch",
                             .arguments = {Argument{.index = 0,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureI32}}}}}}};

    rules[idx(B::Lof)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_lof_ch",
                             .arguments = {Argument{.index = 0,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureI32}}}}}}};

    rules[idx(B::Loc)] = LowerRule{
        .result = {.kind = ResultSpec::Kind::Fixed, .type = Lowerer::ExprType::I64},
        .variants = {Variant{.condition = Condition::Always,
                             .kind = VariantKind::CallRuntime,
                             .runtime = "rt_loc_ch",
                             .arguments = {Argument{.index = 0,
                                                    .transforms = {Transform{.kind = TransformKind::EnsureI32}}}}}}};

    return rules;
}();

static const std::array<BuiltinInfo, kBuiltinCount> kBuiltins = [] {
    std::array<BuiltinInfo, kBuiltinCount> infos{};

    infos[idx(B::Len)] = {"LEN", nullptr};
    infos[idx(B::Mid)] = {"MID$", nullptr};
    infos[idx(B::Left)] = {"LEFT$", nullptr};
    infos[idx(B::Right)] = {"RIGHT$", nullptr};
    infos[idx(B::Str)] = {"STR$", nullptr};
    infos[idx(B::Val)] = {"VAL", nullptr};
    infos[idx(B::Cint)] = {"CINT", nullptr};
    infos[idx(B::Clng)] = {"CLNG", nullptr};
    infos[idx(B::Csng)] = {"CSNG", nullptr};
    infos[idx(B::Cdbl)] = {"CDBL", nullptr};

    builtins::registerMathBuiltinInfos(infos);

    infos[idx(B::Instr)] = {"INSTR", &SemanticAnalyzer::analyzeInstr};
    infos[idx(B::Ltrim)] = {"LTRIM$", nullptr};
    infos[idx(B::Rtrim)] = {"RTRIM$", nullptr};
    infos[idx(B::Trim)] = {"TRIM$", nullptr};
    infos[idx(B::Ucase)] = {"UCASE$", nullptr};
    infos[idx(B::Lcase)] = {"LCASE$", nullptr};
    infos[idx(B::Chr)] = {"CHR$", nullptr};
    infos[idx(B::Asc)] = {"ASC", nullptr};
    infos[idx(B::InKey)] = {"INKEY$", nullptr};
    infos[idx(B::GetKey)] = {"GETKEY$", nullptr};
    infos[idx(B::Eof)] = {"EOF", nullptr};
    infos[idx(B::Lof)] = {"LOF", nullptr};
    infos[idx(B::Loc)] = {"LOC", nullptr};

    return infos;
}();

static const std::array<BuiltinScanRule, kBuiltinCount> kBuiltinScanRules = [] {
    std::array<BuiltinScanRule, kBuiltinCount> rules{};

    rules[idx(B::Len)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                     Lowerer::ExprType::I64,
                                                                     0},
                                         BuiltinScanRule::ArgTraversal::Explicit,
                                         {0},
                                         {}};

    rules[idx(B::Mid)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
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
                                                                   Lowerer::ExprType::I64}}};

    rules[idx(B::Left)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                      Lowerer::ExprType::Str,
                                                                      0},
                                          BuiltinScanRule::ArgTraversal::Explicit,
                                          {0, 1},
                                          {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                    BuiltinScanRule::Feature::Condition::Always,
                                                                    il::runtime::RuntimeFeature::Left,
                                                                    0,
                                                                    Lowerer::ExprType::I64}}};

    rules[idx(B::Right)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                       Lowerer::ExprType::Str,
                                                                       0},
                                           BuiltinScanRule::ArgTraversal::Explicit,
                                           {0, 1},
                                           {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                     BuiltinScanRule::Feature::Condition::Always,
                                                                     il::runtime::RuntimeFeature::Right,
                                                                     0,
                                                                     Lowerer::ExprType::I64}}};

    rules[idx(B::Str)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                     Lowerer::ExprType::Str,
                                                                     0},
                                         BuiltinScanRule::ArgTraversal::Explicit,
                                         {0},
                                         {}};

    rules[idx(B::Val)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                     Lowerer::ExprType::F64,
                                                                     0},
                                         BuiltinScanRule::ArgTraversal::Explicit,
                                         {0},
                                         {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                   BuiltinScanRule::Feature::Condition::Always,
                                                                   il::runtime::RuntimeFeature::Val,
                                                                   0,
                                                                   Lowerer::ExprType::I64}}};

    rules[idx(B::Cint)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                      Lowerer::ExprType::I64,
                                                                      0},
                                          BuiltinScanRule::ArgTraversal::Explicit,
                                          {0},
                                          {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                    BuiltinScanRule::Feature::Condition::Always,
                                                                    il::runtime::RuntimeFeature::CintFromDouble,
                                                                    0,
                                                                    Lowerer::ExprType::I64}}};

    rules[idx(B::Clng)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                      Lowerer::ExprType::I64,
                                                                      0},
                                          BuiltinScanRule::ArgTraversal::Explicit,
                                          {0},
                                          {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                    BuiltinScanRule::Feature::Condition::Always,
                                                                    il::runtime::RuntimeFeature::ClngFromDouble,
                                                                    0,
                                                                    Lowerer::ExprType::I64}}};

    rules[idx(B::Csng)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                      Lowerer::ExprType::F64,
                                                                      0},
                                          BuiltinScanRule::ArgTraversal::Explicit,
                                          {0},
                                          {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                    BuiltinScanRule::Feature::Condition::Always,
                                                                    il::runtime::RuntimeFeature::CsngFromDouble,
                                                                    0,
                                                                    Lowerer::ExprType::I64}}};

    rules[idx(B::Cdbl)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                      Lowerer::ExprType::F64,
                                                                      0},
                                          BuiltinScanRule::ArgTraversal::Explicit,
                                          {0},
                                          {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                    BuiltinScanRule::Feature::Condition::Always,
                                                                    il::runtime::RuntimeFeature::CdblFromAny,
                                                                    0,
                                                                    Lowerer::ExprType::I64}}};

    builtins::registerMathBuiltinScanRules(rules);

    rules[idx(B::Instr)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
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
                                                                     Lowerer::ExprType::I64}}};

    rules[idx(B::Ltrim)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                       Lowerer::ExprType::Str,
                                                                       0},
                                           BuiltinScanRule::ArgTraversal::Explicit,
                                           {0},
                                           {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                     BuiltinScanRule::Feature::Condition::Always,
                                                                     il::runtime::RuntimeFeature::Ltrim,
                                                                     0,
                                                                     Lowerer::ExprType::I64}}};

    rules[idx(B::Rtrim)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                       Lowerer::ExprType::Str,
                                                                       0},
                                           BuiltinScanRule::ArgTraversal::Explicit,
                                           {0},
                                           {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                     BuiltinScanRule::Feature::Condition::Always,
                                                                     il::runtime::RuntimeFeature::Rtrim,
                                                                     0,
                                                                     Lowerer::ExprType::I64}}};

    rules[idx(B::Trim)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                      Lowerer::ExprType::Str,
                                                                      0},
                                          BuiltinScanRule::ArgTraversal::Explicit,
                                          {0},
                                          {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                    BuiltinScanRule::Feature::Condition::Always,
                                                                    il::runtime::RuntimeFeature::Trim,
                                                                    0,
                                                                    Lowerer::ExprType::I64}}};

    rules[idx(B::Ucase)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                       Lowerer::ExprType::Str,
                                                                       0},
                                           BuiltinScanRule::ArgTraversal::Explicit,
                                           {0},
                                           {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                     BuiltinScanRule::Feature::Condition::Always,
                                                                     il::runtime::RuntimeFeature::Ucase,
                                                                     0,
                                                                     Lowerer::ExprType::I64}}};

    rules[idx(B::Lcase)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                       Lowerer::ExprType::Str,
                                                                       0},
                                           BuiltinScanRule::ArgTraversal::Explicit,
                                           {0},
                                           {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                     BuiltinScanRule::Feature::Condition::Always,
                                                                     il::runtime::RuntimeFeature::Lcase,
                                                                     0,
                                                                     Lowerer::ExprType::I64}}};

    rules[idx(B::Chr)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                     Lowerer::ExprType::Str,
                                                                     0},
                                         BuiltinScanRule::ArgTraversal::Explicit,
                                         {0},
                                         {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                   BuiltinScanRule::Feature::Condition::Always,
                                                                   il::runtime::RuntimeFeature::Chr,
                                                                   0,
                                                                   Lowerer::ExprType::I64}}};

    rules[idx(B::Asc)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                     Lowerer::ExprType::I64,
                                                                     0},
                                         BuiltinScanRule::ArgTraversal::Explicit,
                                         {0},
                                         {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                   BuiltinScanRule::Feature::Condition::Always,
                                                                   il::runtime::RuntimeFeature::Asc,
                                                                   0,
                                                                   Lowerer::ExprType::I64}}};

    rules[idx(B::InKey)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                       Lowerer::ExprType::Str,
                                                                       0},
                                           BuiltinScanRule::ArgTraversal::Explicit,
                                           {},
                                           {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                     BuiltinScanRule::Feature::Condition::Always,
                                                                     il::runtime::RuntimeFeature::InKey}}};

    rules[idx(B::GetKey)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                        Lowerer::ExprType::Str,
                                                                        0},
                                            BuiltinScanRule::ArgTraversal::Explicit,
                                            {},
                                            {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                                                                      BuiltinScanRule::Feature::Condition::Always,
                                                                      il::runtime::RuntimeFeature::GetKey}}};

    rules[idx(B::Eof)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                     Lowerer::ExprType::I64,
                                                                     0},
                                         BuiltinScanRule::ArgTraversal::Explicit,
                                         {0},
                                         {}};

    rules[idx(B::Lof)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                     Lowerer::ExprType::I64,
                                                                     0},
                                         BuiltinScanRule::ArgTraversal::Explicit,
                                         {0},
                                         {}};

    rules[idx(B::Loc)] = BuiltinScanRule{BuiltinScanRule::ResultSpec{BuiltinScanRule::ResultSpec::Kind::Fixed,
                                                                     Lowerer::ExprType::I64,
                                                                     0},
                                         BuiltinScanRule::ArgTraversal::Explicit,
                                         {0},
                                         {}};

    return rules;
}();

// Maps canonical BASIC source spellings to enum entries. Names must already be
// normalized to uppercase with any suffix characters (e.g., $) preserved.
static const std::unordered_map<std::string_view, B> kByName = {
    {"LEN", B::Len},      {"MID$", B::Mid},     {"LEFT$", B::Left}, {"RIGHT$", B::Right},
    {"STR$", B::Str},     {"VAL", B::Val},      {"CINT", B::Cint},  {"CLNG", B::Clng},
    {"CSNG", B::Csng},    {"CDBL", B::Cdbl},    {"INT", B::Int},    {"FIX", B::Fix},
    {"ROUND", B::Round},  {"SQR", B::Sqr},
    {"ABS", B::Abs},      {"FLOOR", B::Floor},  {"CEIL", B::Ceil},  {"SIN", B::Sin},
    {"COS", B::Cos},      {"POW", B::Pow},      {"RND", B::Rnd},    {"INSTR", B::Instr},
    {"LTRIM$", B::Ltrim}, {"RTRIM$", B::Rtrim}, {"TRIM$", B::Trim}, {"UCASE$", B::Ucase},
    {"LCASE$", B::Lcase}, {"CHR$", B::Chr},     {"ASC", B::Asc},
    {"INKEY$", B::InKey}, {"GETKEY$", B::GetKey}, {"EOF", B::Eof},
    {"LOF", B::Lof},      {"LOC", B::Loc},
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
