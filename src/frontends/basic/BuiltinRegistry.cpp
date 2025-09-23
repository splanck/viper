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
#include <array>
#include <unordered_map>

namespace il::frontends::basic
{
namespace
{
using B = BuiltinCallExpr::Builtin;
using Semantics = BuiltinSemantics;
using Signature = BuiltinSemantics::Signature;
using ResultSpec = BuiltinSemantics::ResultSpec;
using ValueKind = BuiltinValueKind;
using TypeSet = BuiltinTypeSet;

constexpr TypeSet Numeric = TypeSet::Int | TypeSet::Float;

// Declarative semantic table indexed by BuiltinCallExpr::Builtin enumerators. The
// order must remain in lockstep with the enum definition so the metadata can be
// indexed without translation.
static const std::array<Semantics, 23> kBuiltinSemantics = {{
    {1, 1, {Signature{{TypeSet::String}}}, ResultSpec::fixed(ValueKind::Int)},
    {2,
     3,
     {Signature{{TypeSet::String, Numeric}},
      Signature{{TypeSet::String, Numeric, Numeric}}},
     ResultSpec::fixed(ValueKind::String)},
    {2, 2, {Signature{{TypeSet::String, Numeric}}}, ResultSpec::fixed(ValueKind::String)},
    {2, 2, {Signature{{TypeSet::String, Numeric}}}, ResultSpec::fixed(ValueKind::String)},
    {1, 1, {Signature{{Numeric}}}, ResultSpec::fixed(ValueKind::String)},
    {1, 1, {Signature{{TypeSet::String}}}, ResultSpec::fixed(ValueKind::Int)},
    {1, 1, {Signature{{TypeSet::Float}}}, ResultSpec::fixed(ValueKind::Int)},
    {1, 1, {Signature{{Numeric}}}, ResultSpec::fixed(ValueKind::Float)},
    {1, 1, {Signature{{Numeric}}}, ResultSpec::fromArg(0, ValueKind::Int)},
    {1, 1, {Signature{{Numeric}}}, ResultSpec::fixed(ValueKind::Float)},
    {1, 1, {Signature{{Numeric}}}, ResultSpec::fixed(ValueKind::Float)},
    {1, 1, {Signature{{Numeric}}}, ResultSpec::fixed(ValueKind::Float)},
    {1, 1, {Signature{{Numeric}}}, ResultSpec::fixed(ValueKind::Float)},
    {2, 2, {Signature{{Numeric, Numeric}}}, ResultSpec::fixed(ValueKind::Float)},
    {0, 0, {Signature{}}, ResultSpec::fixed(ValueKind::Float)},
    {2,
     3,
     {Signature{{TypeSet::String, TypeSet::String}},
      Signature{{Numeric, TypeSet::String, TypeSet::String}}},
     ResultSpec::fixed(ValueKind::Int)},
    {1, 1, {Signature{{TypeSet::String}}}, ResultSpec::fixed(ValueKind::String)},
    {1, 1, {Signature{{TypeSet::String}}}, ResultSpec::fixed(ValueKind::String)},
    {1, 1, {Signature{{TypeSet::String}}}, ResultSpec::fixed(ValueKind::String)},
    {1, 1, {Signature{{TypeSet::String}}}, ResultSpec::fixed(ValueKind::String)},
    {1, 1, {Signature{{TypeSet::String}}}, ResultSpec::fixed(ValueKind::String)},
    {1, 1, {Signature{{Numeric}}}, ResultSpec::fixed(ValueKind::String)},
    {1, 1, {Signature{{TypeSet::String}}}, ResultSpec::fixed(ValueKind::Int)},
}};

// Dense metadata table indexing lowering hooks and semantic descriptors.
static const std::array<BuiltinInfo, 23> kBuiltins = {{
    {"LEN", &kBuiltinSemantics[0], &Lowerer::lowerLen},
    {"MID$", &kBuiltinSemantics[1], &Lowerer::lowerMid},
    {"LEFT$", &kBuiltinSemantics[2], &Lowerer::lowerLeft},
    {"RIGHT$", &kBuiltinSemantics[3], &Lowerer::lowerRight},
    {"STR$", &kBuiltinSemantics[4], &Lowerer::lowerStr},
    {"VAL", &kBuiltinSemantics[5], &Lowerer::lowerVal},
    {"INT", &kBuiltinSemantics[6], &Lowerer::lowerInt},
    {"SQR", &kBuiltinSemantics[7], &Lowerer::lowerSqr},
    {"ABS", &kBuiltinSemantics[8], &Lowerer::lowerAbs},
    {"FLOOR", &kBuiltinSemantics[9], &Lowerer::lowerFloor},
    {"CEIL", &kBuiltinSemantics[10], &Lowerer::lowerCeil},
    {"SIN", &kBuiltinSemantics[11], &Lowerer::lowerSin},
    {"COS", &kBuiltinSemantics[12], &Lowerer::lowerCos},
    {"POW", &kBuiltinSemantics[13], &Lowerer::lowerPow},
    {"RND", &kBuiltinSemantics[14], &Lowerer::lowerRnd},
    {"INSTR", &kBuiltinSemantics[15], &Lowerer::lowerInstr},
    {"LTRIM$", &kBuiltinSemantics[16], &Lowerer::lowerLtrim},
    {"RTRIM$", &kBuiltinSemantics[17], &Lowerer::lowerRtrim},
    {"TRIM$", &kBuiltinSemantics[18], &Lowerer::lowerTrim},
    {"UCASE$", &kBuiltinSemantics[19], &Lowerer::lowerUcase},
    {"LCASE$", &kBuiltinSemantics[20], &Lowerer::lowerLcase},
    {"CHR$", &kBuiltinSemantics[21], &Lowerer::lowerChr},
    {"ASC", &kBuiltinSemantics[22], &Lowerer::lowerAsc},
}};

static const std::array<BuiltinScanRule, 23> kBuiltinScanRules = {{
    {BuiltinScanRule::ArgTraversal::Explicit, {0}, {}},
    {BuiltinScanRule::ArgTraversal::All,
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
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0, 1},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Left,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0, 1},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Right,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
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
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::ToInt,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit, {0}, {}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Sqrt,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
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
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Floor,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Ceil,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Sin,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Cos,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::All,
     {},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Pow,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Track,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Rnd,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::All,
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
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Ltrim,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Rtrim,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Trim,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Ucase,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Lcase,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
     {0},
     {BuiltinScanRule::Feature{BuiltinScanRule::Feature::Action::Request,
                               BuiltinScanRule::Feature::Condition::Always,
                               il::runtime::RuntimeFeature::Chr,
                               0,
                               Lowerer::ExprType::I64}}},
    {BuiltinScanRule::ArgTraversal::Explicit,
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
