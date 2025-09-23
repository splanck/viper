// File: src/frontends/basic/BuiltinRegistry.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements registry of BASIC built-ins for semantic analysis and
//          lowering dispatch.
// Key invariants: Registry entries correspond 1:1 with BuiltinCallExpr::Builtin
//                 enum order.
// Ownership/Lifetime: Static data only.
// Links: docs/class-catalog.md

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

// Dense metadata table indexed by BuiltinCallExpr::Builtin enumerators. The
// order must remain in lockstep with the enum definition so that we can index
// directly without translation.
static const std::array<BuiltinInfo, 23> kBuiltins = {{
    {"LEN", 1, 1, &SemanticAnalyzer::analyzeLen, &Lowerer::lowerLen},
    {"MID$", 2, 3, &SemanticAnalyzer::analyzeMid, &Lowerer::lowerMid},
    {"LEFT$", 2, 2, &SemanticAnalyzer::analyzeLeft, &Lowerer::lowerLeft},
    {"RIGHT$", 2, 2, &SemanticAnalyzer::analyzeRight, &Lowerer::lowerRight},
    {"STR$", 1, 1, &SemanticAnalyzer::analyzeStr, &Lowerer::lowerStr},
    {"VAL", 1, 1, &SemanticAnalyzer::analyzeVal, &Lowerer::lowerVal},
    {"INT", 1, 1, &SemanticAnalyzer::analyzeInt, &Lowerer::lowerInt},
    {"SQR", 1, 1, &SemanticAnalyzer::analyzeSqr, &Lowerer::lowerSqr},
    {"ABS", 1, 1, &SemanticAnalyzer::analyzeAbs, &Lowerer::lowerAbs},
    {"FLOOR", 1, 1, &SemanticAnalyzer::analyzeFloor, &Lowerer::lowerFloor},
    {"CEIL", 1, 1, &SemanticAnalyzer::analyzeCeil, &Lowerer::lowerCeil},
    {"SIN", 1, 1, &SemanticAnalyzer::analyzeSin, &Lowerer::lowerSin},
    {"COS", 1, 1, &SemanticAnalyzer::analyzeCos, &Lowerer::lowerCos},
    {"POW", 2, 2, &SemanticAnalyzer::analyzePow, &Lowerer::lowerPow},
    {"RND", 0, 0, &SemanticAnalyzer::analyzeRnd, &Lowerer::lowerRnd},
    {"INSTR", 2, 3, &SemanticAnalyzer::analyzeInstr, &Lowerer::lowerInstr},
    {"LTRIM$", 1, 1, &SemanticAnalyzer::analyzeLtrim, &Lowerer::lowerLtrim},
    {"RTRIM$", 1, 1, &SemanticAnalyzer::analyzeRtrim, &Lowerer::lowerRtrim},
    {"TRIM$", 1, 1, &SemanticAnalyzer::analyzeTrim, &Lowerer::lowerTrim},
    {"UCASE$", 1, 1, &SemanticAnalyzer::analyzeUcase, &Lowerer::lowerUcase},
    {"LCASE$", 1, 1, &SemanticAnalyzer::analyzeLcase, &Lowerer::lowerLcase},
    {"CHR$", 1, 1, &SemanticAnalyzer::analyzeChr, &Lowerer::lowerChr},
    {"ASC", 1, 1, &SemanticAnalyzer::analyzeAsc, &Lowerer::lowerAsc},
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
/// @return Reference to the metadata describing semantic, lowering, and scan
///         hooks.
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

} // namespace il::frontends::basic
