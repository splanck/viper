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
#include <cstdint>
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

/// @brief Describes broad type categories produced by a builtin.
enum class TypeMask : std::uint8_t
{
    None = 0,
    I64 = 1U << 0U,
    F64 = 1U << 1U,
    Str = 1U << 2U,
};

constexpr TypeMask operator|(TypeMask lhs, TypeMask rhs) noexcept
{
    return static_cast<TypeMask>(static_cast<std::uint8_t>(lhs) |
                                 static_cast<std::uint8_t>(rhs));
}

struct Arity
{
    std::uint8_t minArgs;
    std::uint8_t maxArgs;
};

struct BuiltinDescriptor
{
    const char *name;
    B builtin;
    Arity arity;
    TypeMask typeMask;
    SemanticAnalyzer::BuiltinAnalyzer analyze;
};

/// @brief Declarative builtin table used for name/enum lookups.
/// Names remain uppercase because lookupBuiltin() performs exact, case-sensitive
/// matches on normalized BASIC keywords. The arity bounds mirror
/// SemanticAnalyzer::builtinSignature() so diagnostics continue to enforce the
/// same argument requirements, and the type masks communicate the possible
/// result categories to inference helpers.
constexpr std::array<BuiltinDescriptor, kBuiltinCount> kBuiltinDescriptors{{
    {"LEN", B::Len, {1, 1}, TypeMask::I64, nullptr},
    {"MID$", B::Mid, {2, 3}, TypeMask::Str, nullptr},
    {"LEFT$", B::Left, {2, 2}, TypeMask::Str, nullptr},
    {"RIGHT$", B::Right, {2, 2}, TypeMask::Str, nullptr},
    {"STR$", B::Str, {1, 1}, TypeMask::Str, nullptr},
    {"VAL", B::Val, {1, 1}, TypeMask::F64, nullptr},
    {"CINT", B::Cint, {1, 1}, TypeMask::I64, nullptr},
    {"CLNG", B::Clng, {1, 1}, TypeMask::I64, nullptr},
    {"CSNG", B::Csng, {1, 1}, TypeMask::F64, nullptr},
    {"CDBL", B::Cdbl, {1, 1}, TypeMask::F64, nullptr},
    {"INT", B::Int, {1, 1}, TypeMask::F64, nullptr},
    {"FIX", B::Fix, {1, 1}, TypeMask::F64, nullptr},
    {"ROUND", B::Round, {1, 2}, TypeMask::F64, nullptr},
    {"SQR", B::Sqr, {1, 1}, TypeMask::F64, nullptr},
    {"ABS", B::Abs, {1, 1}, TypeMask::I64 | TypeMask::F64,
     &SemanticAnalyzer::analyzeAbs},
    {"FLOOR", B::Floor, {1, 1}, TypeMask::F64, nullptr},
    {"CEIL", B::Ceil, {1, 1}, TypeMask::F64, nullptr},
    {"SIN", B::Sin, {1, 1}, TypeMask::F64, nullptr},
    {"COS", B::Cos, {1, 1}, TypeMask::F64, nullptr},
    {"POW", B::Pow, {2, 2}, TypeMask::F64, nullptr},
    {"RND", B::Rnd, {0, 0}, TypeMask::F64, nullptr},
    {"INSTR", B::Instr, {2, 3}, TypeMask::I64,
     &SemanticAnalyzer::analyzeInstr},
    {"LTRIM$", B::Ltrim, {1, 1}, TypeMask::Str, nullptr},
    {"RTRIM$", B::Rtrim, {1, 1}, TypeMask::Str, nullptr},
    {"TRIM$", B::Trim, {1, 1}, TypeMask::Str, nullptr},
    {"UCASE$", B::Ucase, {1, 1}, TypeMask::Str, nullptr},
    {"LCASE$", B::Lcase, {1, 1}, TypeMask::Str, nullptr},
    {"CHR$", B::Chr, {1, 1}, TypeMask::Str, nullptr},
    {"ASC", B::Asc, {1, 1}, TypeMask::I64, nullptr},
    {"INKEY$", B::InKey, {0, 0}, TypeMask::Str, nullptr},
    {"GETKEY$", B::GetKey, {0, 0}, TypeMask::Str, nullptr},
    {"EOF", B::Eof, {1, 1}, TypeMask::I64, nullptr},
    {"LOF", B::Lof, {1, 1}, TypeMask::I64, nullptr},
    {"LOC", B::Loc, {1, 1}, TypeMask::I64, nullptr},
}};

constexpr std::array<BuiltinInfo, kBuiltinCount> makeBuiltinInfos()
{
    std::array<BuiltinInfo, kBuiltinCount> infos{};
    for (const auto &desc : kBuiltinDescriptors)
        infos[idx(desc.builtin)] = BuiltinInfo{desc.name, desc.analyze};
    return infos;
}

constexpr auto kBuiltins = makeBuiltinInfos();

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

/// @brief Access the lazily-initialized name-to-enum map.
/// @details Entries are stored with canonical uppercase spellings so callers
/// should normalize BASIC identifiers before lookup.
const std::unordered_map<std::string_view, B> &builtinNameIndex()
{
    static const auto index = [] {
        std::unordered_map<std::string_view, B> map;
        map.reserve(kBuiltinDescriptors.size());
        for (const auto &desc : kBuiltinDescriptors)
            map.emplace(desc.name, desc.builtin);
        return map;
    }();
    return index;
}
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
    const auto &index = builtinNameIndex();
    auto it = index.find(name);
    if (it == index.end())
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
