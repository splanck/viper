// File: src/frontends/basic/BuiltinRegistry.cpp
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
using Mask = BuiltinTypeMask;

static constexpr std::array<BuiltinArgSpec, 1> kStringArg = {{{Mask::String}}};
static constexpr std::array<BuiltinArgSpec, 1> kNumberArg = {{{Mask::Int | Mask::Float}}};
static constexpr std::array<BuiltinArgSpec, 1> kFloatArg = {{{Mask::Float}}};
static constexpr std::array<BuiltinArgSpec, 2> kStringNumberArgs = {{{Mask::String}, {Mask::Int | Mask::Float}}};
static constexpr std::array<BuiltinArgSpec, 2> kTwoNumberArgs = {{{Mask::Int | Mask::Float}, {Mask::Int | Mask::Float}}};
static constexpr std::array<BuiltinArgSpec, 2> kStringStringArgs = {{{Mask::String}, {Mask::String}}};
static constexpr std::array<BuiltinArgSpec, 3> kStringNumberNumberArgs = {
    {{Mask::String}, {Mask::Int | Mask::Float}, {Mask::Int | Mask::Float}}};
static constexpr std::array<BuiltinArgSpec, 3> kNumberStringStringArgs = {
    {{Mask::Int | Mask::Float}, {Mask::String}, {Mask::String}}};

static constexpr std::array<BuiltinSignature, 1> kStringSignatures = {{{kStringArg.data(), kStringArg.size()}}};
static constexpr std::array<BuiltinSignature, 1> kNumberSignatures = {{{kNumberArg.data(), kNumberArg.size()}}};
static constexpr std::array<BuiltinSignature, 1> kFloatSignatures = {{{kFloatArg.data(), kFloatArg.size()}}};
static constexpr std::array<BuiltinSignature, 1> kStringNumberSignatures = {
    {{kStringNumberArgs.data(), kStringNumberArgs.size()}}};
static constexpr std::array<BuiltinSignature, 1> kTwoNumberSignatures = {{{kTwoNumberArgs.data(), kTwoNumberArgs.size()}}};
static constexpr std::array<BuiltinSignature, 1> kStringStringSignatures = {
    {{kStringStringArgs.data(), kStringStringArgs.size()}}};
static constexpr std::array<BuiltinSignature, 1> kNumberStringStringSignatures = {
    {{kNumberStringStringArgs.data(), kNumberStringStringArgs.size()}}};
static constexpr std::array<BuiltinSignature, 2> kMidSignatures = {{
    {kStringNumberArgs.data(), kStringNumberArgs.size()},
    {kStringNumberNumberArgs.data(), kStringNumberNumberArgs.size()},
}};
static constexpr std::array<BuiltinSignature, 2> kInstrSignatures = {{
    {kStringStringArgs.data(), kStringStringArgs.size()},
    {kNumberStringStringArgs.data(), kNumberStringStringArgs.size()},
}};

static const std::array<BuiltinInfo, 23> kBuiltins = {{
    {"LEN", 1, 1, kStringSignatures.data(), kStringSignatures.size(), BuiltinResultKind::Int,
     &Lowerer::lowerLen, &Lowerer::scanLen},
    {"MID$", 2, 3, kMidSignatures.data(), kMidSignatures.size(), BuiltinResultKind::String,
     &Lowerer::lowerMid, &Lowerer::scanMid},
    {"LEFT$", 2, 2, kStringNumberSignatures.data(), kStringNumberSignatures.size(), BuiltinResultKind::String,
     &Lowerer::lowerLeft, &Lowerer::scanLeft},
    {"RIGHT$", 2, 2, kStringNumberSignatures.data(), kStringNumberSignatures.size(), BuiltinResultKind::String,
     &Lowerer::lowerRight, &Lowerer::scanRight},
    {"STR$", 1, 1, kNumberSignatures.data(), kNumberSignatures.size(), BuiltinResultKind::String,
     &Lowerer::lowerStr, &Lowerer::scanStr},
    {"VAL", 1, 1, kStringSignatures.data(), kStringSignatures.size(), BuiltinResultKind::Int,
     &Lowerer::lowerVal, &Lowerer::scanVal},
    {"INT", 1, 1, kFloatSignatures.data(), kFloatSignatures.size(), BuiltinResultKind::Int,
     &Lowerer::lowerInt, &Lowerer::scanInt},
    {"SQR", 1, 1, kNumberSignatures.data(), kNumberSignatures.size(), BuiltinResultKind::Float,
     &Lowerer::lowerSqr, &Lowerer::scanSqr},
    {"ABS", 1, 1, kNumberSignatures.data(), kNumberSignatures.size(), BuiltinResultKind::NumericLikeFirstArg,
     &Lowerer::lowerAbs, &Lowerer::scanAbs},
    {"FLOOR", 1, 1, kNumberSignatures.data(), kNumberSignatures.size(), BuiltinResultKind::Float,
     &Lowerer::lowerFloor, &Lowerer::scanFloor},
    {"CEIL", 1, 1, kNumberSignatures.data(), kNumberSignatures.size(), BuiltinResultKind::Float,
     &Lowerer::lowerCeil, &Lowerer::scanCeil},
    {"SIN", 1, 1, kNumberSignatures.data(), kNumberSignatures.size(), BuiltinResultKind::Float,
     &Lowerer::lowerSin, &Lowerer::scanSin},
    {"COS", 1, 1, kNumberSignatures.data(), kNumberSignatures.size(), BuiltinResultKind::Float,
     &Lowerer::lowerCos, &Lowerer::scanCos},
    {"POW", 2, 2, kTwoNumberSignatures.data(), kTwoNumberSignatures.size(), BuiltinResultKind::Float,
     &Lowerer::lowerPow, &Lowerer::scanPow},
    {"RND", 0, 0, nullptr, 0, BuiltinResultKind::Float, &Lowerer::lowerRnd, &Lowerer::scanRnd},
    {"INSTR", 2, 3, kInstrSignatures.data(), kInstrSignatures.size(), BuiltinResultKind::Int,
     &Lowerer::lowerInstr, &Lowerer::scanInstr},
    {"LTRIM$", 1, 1, kStringSignatures.data(), kStringSignatures.size(), BuiltinResultKind::String,
     &Lowerer::lowerLtrim, &Lowerer::scanLtrim},
    {"RTRIM$", 1, 1, kStringSignatures.data(), kStringSignatures.size(), BuiltinResultKind::String,
     &Lowerer::lowerRtrim, &Lowerer::scanRtrim},
    {"TRIM$", 1, 1, kStringSignatures.data(), kStringSignatures.size(), BuiltinResultKind::String,
     &Lowerer::lowerTrim, &Lowerer::scanTrim},
    {"UCASE$", 1, 1, kStringSignatures.data(), kStringSignatures.size(), BuiltinResultKind::String,
     &Lowerer::lowerUcase, &Lowerer::scanUcase},
    {"LCASE$", 1, 1, kStringSignatures.data(), kStringSignatures.size(), BuiltinResultKind::String,
     &Lowerer::lowerLcase, &Lowerer::scanLcase},
    {"CHR$", 1, 1, kNumberSignatures.data(), kNumberSignatures.size(), BuiltinResultKind::String,
     &Lowerer::lowerChr, &Lowerer::scanChr},
    {"ASC", 1, 1, kStringSignatures.data(), kStringSignatures.size(), BuiltinResultKind::Int,
     &Lowerer::lowerAsc, &Lowerer::scanAsc},
}};

static const std::unordered_map<std::string_view, B> kByName = {
    {"LEN", B::Len},      {"MID$", B::Mid},     {"LEFT$", B::Left}, {"RIGHT$", B::Right},
    {"STR$", B::Str},     {"VAL", B::Val},      {"INT", B::Int},    {"SQR", B::Sqr},
    {"ABS", B::Abs},      {"FLOOR", B::Floor},  {"CEIL", B::Ceil},  {"SIN", B::Sin},
    {"COS", B::Cos},      {"POW", B::Pow},      {"RND", B::Rnd},    {"INSTR", B::Instr},
    {"LTRIM$", B::Ltrim}, {"RTRIM$", B::Rtrim}, {"TRIM$", B::Trim}, {"UCASE$", B::Ucase},
    {"LCASE$", B::Lcase}, {"CHR$", B::Chr},     {"ASC", B::Asc},
};
} // namespace

const BuiltinInfo &getBuiltinInfo(BuiltinCallExpr::Builtin b)
{
    return kBuiltins[static_cast<std::size_t>(b)];
}

std::optional<BuiltinCallExpr::Builtin> lookupBuiltin(std::string_view name)
{
    auto it = kByName.find(name);
    if (it == kByName.end())
        return std::nullopt;
    return it->second;
}

} // namespace il::frontends::basic
