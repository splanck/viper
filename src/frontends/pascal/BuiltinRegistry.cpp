//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/BuiltinRegistry.cpp
// Purpose: Implements the Pascal builtin function registry.
// Key invariants: Static tables map names to descriptors; case-insensitive lookup.
// Ownership/Lifetime: Static data initialized at startup.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/BuiltinRegistry.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

namespace il::frontends::pascal
{

namespace
{

/// @brief Convert string to lowercase for case-insensitive comparison.
std::string toLower(std::string_view s)
{
    std::string result;
    result.reserve(s.size());
    for (char c : s)
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return result;
}

/// @brief Number of builtins.
constexpr size_t kBuiltinCount = static_cast<size_t>(PascalBuiltin::Count);

/// @brief Build the builtin descriptor table.
std::array<BuiltinDescriptor, kBuiltinCount> makeDescriptors()
{
    std::array<BuiltinDescriptor, kBuiltinCount> desc{};
    using B = PascalBuiltin;
    using C = BuiltinCategory;
    using R = ResultKind;
    using A = ArgTypeMask;
    using T = PasTypeKind;

    // Helper to set descriptor
    auto set = [&](B id, const char *name, C cat, uint8_t minA, uint8_t maxA, bool var, R res,
                   std::vector<RuntimeVariant> rt = {}, std::vector<BuiltinArgSpec> args = {}) {
        auto idx = static_cast<size_t>(id);
        desc[idx].name = name;
        desc[idx].id = id;
        desc[idx].category = cat;
        desc[idx].minArgs = minA;
        desc[idx].maxArgs = maxA;
        desc[idx].variadic = var;
        desc[idx].result = res;
        desc[idx].runtimeVariants = std::move(rt);
        desc[idx].args = std::move(args);
    };

    //=========================================================================
    // Core I/O Builtins
    //=========================================================================

    set(B::Write, "Write", C::Builtin, 0, 255, true, R::Void,
        {{.symbol = "rt_print_str", .argType = T::String},
         {.symbol = "rt_print_i64", .argType = T::Integer},
         {.symbol = "rt_print_f64", .argType = T::Real},
         {.symbol = "rt_print_i64", .argType = T::Boolean}});

    set(B::WriteLn, "WriteLn", C::Builtin, 0, 255, true, R::Void,
        {{.symbol = "rt_println_str", .argType = T::String},
         {.symbol = "rt_print_str", .argType = T::String}, // Write then newline
         {.symbol = "rt_print_i64", .argType = T::Integer},
         {.symbol = "rt_print_f64", .argType = T::Real},
         {.symbol = "rt_print_i64", .argType = T::Boolean},
         {.symbol = "rt_println_empty", .argType = T::Unknown}}); // No args = just newline

    set(B::Read, "Read", C::Builtin, 1, 255, true, R::Void,
        {{.symbol = "rt_input_line", .argType = T::String}},
        {{.allowed = A::Any, .isVar = true}});

    set(B::ReadLn, "ReadLn", C::Builtin, 0, 255, true, R::String,
        {{.symbol = "rt_input_line", .argType = T::String}});

    // ReadInteger: reads a line and parses as Integer (raises on error)
    set(B::ReadInteger, "ReadInteger", C::Builtin, 0, 0, false, R::Integer,
        {{.symbol = "rt_read_integer"}});

    // ReadReal: reads a line and parses as Real (raises on error)
    set(B::ReadReal, "ReadReal", C::Builtin, 0, 0, false, R::Real,
        {{.symbol = "rt_read_real"}});

    //=========================================================================
    // String Functions
    //=========================================================================

    set(B::Length, "Length", C::Builtin, 1, 1, false, R::Integer,
        {{.symbol = "rt_len", .argType = T::String},
         {.symbol = "rt_arr_len", .argType = T::Array}},
        {{.allowed = A::String | A::Array}});

    set(B::SetLength, "SetLength", C::Builtin, 2, 2, false, R::Void,
        {{.symbol = "rt_str_setlength", .argType = T::String},
         {.symbol = "rt_arr_setlength", .argType = T::Array}},
        {{.allowed = A::String | A::Array, .isVar = true}, {.allowed = A::Integer}});

    set(B::IntToStr, "IntToStr", C::Builtin, 1, 1, false, R::String,
        {{.symbol = "rt_int_to_str"}}, {{.allowed = A::Integer}});

    // RealToStr: spec name for Real to String conversion
    set(B::RealToStr, "RealToStr", C::Builtin, 1, 1, false, R::String,
        {{.symbol = "rt_f64_to_str"}}, {{.allowed = A::Real}});

    // FloatToStr: extension alias (same as RealToStr)
    set(B::FloatToStr, "FloatToStr", C::Builtin, 1, 1, false, R::String,
        {{.symbol = "rt_f64_to_str"}}, {{.allowed = A::Real}});

    // StrToInt: raises exception on parse error
    set(B::StrToInt, "StrToInt", C::Builtin, 1, 1, false, R::Integer,
        {{.symbol = "rt_str_to_int"}}, {{.allowed = A::String}});

    // StrToReal: spec name for String to Real conversion (raises on error)
    set(B::StrToReal, "StrToReal", C::Builtin, 1, 1, false, R::Real,
        {{.symbol = "rt_str_to_real"}}, {{.allowed = A::String}});

    // StrToFloat: extension alias (same as StrToReal)
    set(B::StrToFloat, "StrToFloat", C::Builtin, 1, 1, false, R::Real,
        {{.symbol = "rt_str_to_real"}}, {{.allowed = A::String}});

    set(B::Copy, "Copy", C::Builtin, 2, 3, false, R::String,
        {{.symbol = "rt_substr"}},
        {{.allowed = A::String}, {.allowed = A::Integer}, {.allowed = A::Integer, .optional = true}});

    set(B::Pos, "Pos", C::Builtin, 2, 2, false, R::Integer, {{.symbol = "rt_instr"}},
        {{.allowed = A::String}, {.allowed = A::String}});

    set(B::Concat, "Concat", C::Builtin, 1, 255, true, R::String, {{.symbol = "rt_concat"}});

    set(B::Trim, "Trim", C::Builtin, 1, 1, false, R::String, {{.symbol = "rt_trim"}},
        {{.allowed = A::String}});

    //=========================================================================
    // Ordinal Functions
    //=========================================================================

    set(B::Ord, "Ord", C::Builtin, 1, 1, false, R::Integer,
        {}, // Inline lowering
        {{.allowed = A::Ordinal}});

    set(B::Chr, "Chr", C::Builtin, 1, 1, false, R::String, {{.symbol = "rt_chr"}},
        {{.allowed = A::Integer}});

    set(B::Pred, "Pred", C::Builtin, 1, 1, false, R::FromArg,
        {}, // Inline: subtract 1
        {{.allowed = A::Ordinal}});
    desc[static_cast<size_t>(B::Pred)].resultArgIndex = 0;

    set(B::Succ, "Succ", C::Builtin, 1, 1, false, R::FromArg,
        {}, // Inline: add 1
        {{.allowed = A::Ordinal}});
    desc[static_cast<size_t>(B::Succ)].resultArgIndex = 0;

    set(B::Inc, "Inc", C::Builtin, 1, 2, false, R::Void,
        {}, // Inline: add and store
        {{.allowed = A::Ordinal, .isVar = true}, {.allowed = A::Integer, .optional = true}});

    set(B::Dec, "Dec", C::Builtin, 1, 2, false, R::Void,
        {}, // Inline: subtract and store
        {{.allowed = A::Ordinal, .isVar = true}, {.allowed = A::Integer, .optional = true}});

    set(B::Low, "Low", C::Builtin, 1, 1, false, R::Integer,
        {}, // Inline: compile-time constant
        {{.allowed = A::Any}});

    set(B::High, "High", C::Builtin, 1, 1, false, R::Integer,
        {{.symbol = "rt_arr_high", .argType = T::Array}}, {{.allowed = A::Any}});

    //=========================================================================
    // Math Functions
    //=========================================================================

    set(B::Abs, "Abs", C::Builtin, 1, 1, false, R::FromArg,
        {{.symbol = "rt_abs_i64", .argType = T::Integer},
         {.symbol = "rt_abs_f64", .argType = T::Real}},
        {{.allowed = A::Numeric}});
    desc[static_cast<size_t>(B::Abs)].resultArgIndex = 0;

    set(B::Sqr, "Sqr", C::Builtin, 1, 1, false, R::FromArg,
        {}, // Inline: x * x
        {{.allowed = A::Numeric}});
    desc[static_cast<size_t>(B::Sqr)].resultArgIndex = 0;

    set(B::Sqrt, "Sqrt", C::Builtin, 1, 1, false, R::Real, {{.symbol = "rt_sqrt"}},
        {{.allowed = A::Numeric}});

    set(B::Sin, "Sin", C::Builtin, 1, 1, false, R::Real, {{.symbol = "rt_sin"}},
        {{.allowed = A::Numeric}});

    set(B::Cos, "Cos", C::Builtin, 1, 1, false, R::Real, {{.symbol = "rt_cos"}},
        {{.allowed = A::Numeric}});

    set(B::Tan, "Tan", C::Builtin, 1, 1, false, R::Real, {{.symbol = "rt_tan"}},
        {{.allowed = A::Numeric}});

    set(B::ArcTan, "ArcTan", C::Builtin, 1, 1, false, R::Real, {{.symbol = "rt_atan"}},
        {{.allowed = A::Numeric}});

    set(B::Exp, "Exp", C::Builtin, 1, 1, false, R::Real, {{.symbol = "rt_exp"}},
        {{.allowed = A::Numeric}});

    set(B::Ln, "Ln", C::Builtin, 1, 1, false, R::Real, {{.symbol = "rt_log"}},
        {{.allowed = A::Numeric}});

    set(B::Trunc, "Trunc", C::Builtin, 1, 1, false, R::Integer, {{.symbol = "rt_fix_trunc"}},
        {{.allowed = A::Numeric}});

    set(B::Round, "Round", C::Builtin, 1, 1, false, R::Integer, {{.symbol = "rt_round_even"}},
        {{.allowed = A::Numeric}});

    set(B::Floor, "Floor", C::Builtin, 1, 1, false, R::Integer, {{.symbol = "rt_floor"}},
        {{.allowed = A::Numeric}});

    set(B::Ceil, "Ceil", C::Builtin, 1, 1, false, R::Integer, {{.symbol = "rt_ceil"}},
        {{.allowed = A::Numeric}});

    set(B::Random, "Random", C::Builtin, 0, 1, false, R::Real,
        {{.symbol = "rt_rnd", .argType = T::Unknown},
         {.symbol = "rt_random_int", .argType = T::Integer}},
        {{.allowed = A::Integer, .optional = true}});

    set(B::Randomize, "Randomize", C::Builtin, 0, 1, false, R::Void,
        {{.symbol = "rt_randomize_i64", .argType = T::Integer}},
        {{.allowed = A::Integer, .optional = true}});

    //=========================================================================
    // Type Conversion (handled specially in lowering)
    //=========================================================================

    set(B::Integer, "Integer", C::Builtin, 1, 1, false, R::Integer, {},
        {{.allowed = A::Any}});

    set(B::Real, "Real", C::Builtin, 1, 1, false, R::Real, {}, {{.allowed = A::Any}});

    //=========================================================================
    // Array
    //=========================================================================

    set(B::SetLengthArr, "SetLength", C::Builtin, 2, 2, false, R::Void,
        {{.symbol = "rt_arr_setlength"}}, {{.allowed = A::Array, .isVar = true}, {.allowed = A::Integer}});

    //=========================================================================
    // Viper.Strings Unit
    //=========================================================================

    set(B::Upper, "Upper", C::ViperStrings, 1, 1, false, R::String, {{.symbol = "rt_ucase"}},
        {{.allowed = A::String}});

    set(B::Lower, "Lower", C::ViperStrings, 1, 1, false, R::String, {{.symbol = "rt_lcase"}},
        {{.allowed = A::String}});

    set(B::Left, "Left", C::ViperStrings, 2, 2, false, R::String, {{.symbol = "rt_left"}},
        {{.allowed = A::String}, {.allowed = A::Integer}});

    set(B::Right, "Right", C::ViperStrings, 2, 2, false, R::String, {{.symbol = "rt_right"}},
        {{.allowed = A::String}, {.allowed = A::Integer}});

    set(B::Mid, "Mid", C::ViperStrings, 2, 3, false, R::String,
        {{.symbol = "rt_mid2", .argType = T::Unknown}, {.symbol = "rt_mid3", .argType = T::Integer}},
        {{.allowed = A::String}, {.allowed = A::Integer}, {.allowed = A::Integer, .optional = true}});

    set(B::ChrStr, "Chr", C::ViperStrings, 1, 1, false, R::String, {{.symbol = "rt_chr"}},
        {{.allowed = A::Integer}});

    set(B::AscStr, "Asc", C::ViperStrings, 1, 1, false, R::Integer, {{.symbol = "rt_asc"}},
        {{.allowed = A::String}});

    //=========================================================================
    // Viper.Math Unit
    //=========================================================================

    // Pow: spec name for power function
    set(B::Pow, "Pow", C::ViperMath, 2, 2, false, R::Real, {{.symbol = "rt_pow"}},
        {{.allowed = A::Numeric}, {.allowed = A::Numeric}});

    // Power: extension alias (same as Pow)
    set(B::Power, "Power", C::ViperMath, 2, 2, false, R::Real, {{.symbol = "rt_pow"}},
        {{.allowed = A::Numeric}, {.allowed = A::Numeric}});

    // Atan: spec name for arc tangent (in unit, not core ArcTan)
    set(B::Atan, "Atan", C::ViperMath, 1, 1, false, R::Real, {{.symbol = "rt_atan"}},
        {{.allowed = A::Numeric}});

    set(B::Sign, "Sign", C::ViperMath, 1, 1, false, R::Integer,
        {{.symbol = "rt_sgn_i64", .argType = T::Integer},
         {.symbol = "rt_sgn_f64", .argType = T::Real}},
        {{.allowed = A::Numeric}});

    set(B::Min, "Min", C::ViperMath, 2, 2, false, R::FromArg,
        {{.symbol = "rt_min_i64", .argType = T::Integer},
         {.symbol = "rt_min_f64", .argType = T::Real}},
        {{.allowed = A::Numeric}, {.allowed = A::Numeric}});
    desc[static_cast<size_t>(B::Min)].resultArgIndex = 0;

    set(B::Max, "Max", C::ViperMath, 2, 2, false, R::FromArg,
        {{.symbol = "rt_max_i64", .argType = T::Integer},
         {.symbol = "rt_max_f64", .argType = T::Real}},
        {{.allowed = A::Numeric}, {.allowed = A::Numeric}});
    desc[static_cast<size_t>(B::Max)].resultArgIndex = 0;

    //=========================================================================
    // Viper.Terminal Unit - Console/Terminal Control
    // Note: Using i64 wrapper functions since Pascal Integer is i64
    //=========================================================================

    set(B::ClrScr, "ClrScr", C::ViperTerminal, 0, 0, false, R::Void,
        {{.symbol = "rt_term_cls"}});

    // GotoXY takes (col, row) in Pascal/Turbo Pascal style but our runtime
    // uses (row, col) - the lowerer will swap the arguments
    set(B::GotoXY, "GotoXY", C::ViperTerminal, 2, 2, false, R::Void,
        {{.symbol = "rt_term_locate"}},
        {{.allowed = A::Integer}, {.allowed = A::Integer}});

    // TextColor sets only foreground
    set(B::TextColor, "TextColor", C::ViperTerminal, 1, 1, false, R::Void,
        {{.symbol = "rt_term_textcolor"}},
        {{.allowed = A::Integer}});

    // TextBackground sets only background
    set(B::TextBackground, "TextBackground", C::ViperTerminal, 1, 1, false, R::Void,
        {{.symbol = "rt_term_textbg"}},
        {{.allowed = A::Integer}});

    // KeyPressed: returns boolean (1 if key available, 0 if not)
    // Uses i64 version for IL compatibility
    set(B::KeyPressed, "KeyPressed", C::ViperTerminal, 0, 0, false, R::Boolean,
        {{.symbol = "rt_keypressed_i64"}});

    set(B::ReadKey, "ReadKey", C::ViperTerminal, 0, 0, false, R::String,
        {{.symbol = "rt_getkey_str"}});

    set(B::InKey, "InKey", C::ViperTerminal, 0, 0, false, R::String,
        {{.symbol = "rt_inkey_str"}});

    set(B::Delay, "Delay", C::ViperTerminal, 1, 1, false, R::Void,
        {{.symbol = "rt_sleep_ms_i64"}},
        {{.allowed = A::Integer}});

    set(B::Sleep, "Sleep", C::ViperTerminal, 1, 1, false, R::Void,
        {{.symbol = "rt_sleep_ms_i64"}},
        {{.allowed = A::Integer}});

    set(B::HideCursor, "HideCursor", C::ViperTerminal, 0, 0, false, R::Void,
        {{.symbol = "rt_term_hide_cursor"}});

    set(B::ShowCursor, "ShowCursor", C::ViperTerminal, 0, 0, false, R::Void,
        {{.symbol = "rt_term_show_cursor"}});

    return desc;
}

/// @brief Static descriptor table.
const std::array<BuiltinDescriptor, kBuiltinCount> &descriptors()
{
    static const auto desc = makeDescriptors();
    return desc;
}

/// @brief Build the name lookup index (case-insensitive).
const std::unordered_map<std::string, PascalBuiltin> &nameIndex()
{
    static const auto index = []() {
        std::unordered_map<std::string, PascalBuiltin> map;
        for (const auto &desc : descriptors())
        {
            if (desc.name)
            {
                map[toLower(desc.name)] = desc.id;
            }
        }
        return map;
    }();
    return index;
}

} // namespace

//===----------------------------------------------------------------------===//
// Public Interface
//===----------------------------------------------------------------------===//

std::optional<PascalBuiltin> lookupBuiltin(std::string_view name)
{
    std::string key = toLower(name);
    const auto &index = nameIndex();
    auto it = index.find(key);
    if (it != index.end())
        return it->second;
    return std::nullopt;
}

const BuiltinDescriptor &getBuiltinDescriptor(PascalBuiltin id)
{
    return descriptors()[static_cast<size_t>(id)];
}

const char *getBuiltinRuntimeSymbol(PascalBuiltin id, PasTypeKind argType)
{
    const auto &desc = getBuiltinDescriptor(id);
    if (desc.runtimeVariants.empty())
        return nullptr;

    // If only one variant or no type-specific variant, return first
    if (desc.runtimeVariants.size() == 1)
        return desc.runtimeVariants[0].symbol;

    // Find matching variant by argument type
    for (const auto &var : desc.runtimeVariants)
    {
        if (var.argType == argType)
            return var.symbol;
    }

    // Fall back to first variant with Unknown type (default)
    for (const auto &var : desc.runtimeVariants)
    {
        if (var.argType == PasTypeKind::Unknown)
            return var.symbol;
    }

    // Last resort: return first variant
    return desc.runtimeVariants[0].symbol;
}

bool isBuiltinProcedure(PascalBuiltin id)
{
    return getBuiltinDescriptor(id).result == ResultKind::Void;
}

PasType getBuiltinResultType(PascalBuiltin id, PasTypeKind argType)
{
    const auto &desc = getBuiltinDescriptor(id);
    switch (desc.result)
    {
    case ResultKind::Void:
        return PasType::voidType();
    case ResultKind::Integer:
        return PasType::integer();
    case ResultKind::Real:
        return PasType::real();
    case ResultKind::String:
        return PasType::string();
    case ResultKind::Boolean:
        return PasType::boolean();
    case ResultKind::FromArg:
        // Return type matches argument type
        switch (argType)
        {
        case PasTypeKind::Integer:
            return PasType::integer();
        case PasTypeKind::Real:
            return PasType::real();
        case PasTypeKind::String:
            return PasType::string();
        case PasTypeKind::Boolean:
            return PasType::boolean();
        default:
            return PasType::unknown();
        }
    }
    return PasType::unknown();
}

std::vector<std::string> getRequiredExterns(const std::vector<PascalBuiltin> &usedBuiltins)
{
    std::vector<std::string> externs;
    std::unordered_map<std::string, bool> seen;

    for (auto id : usedBuiltins)
    {
        const auto &desc = getBuiltinDescriptor(id);
        for (const auto &var : desc.runtimeVariants)
        {
            if (var.symbol && !seen[var.symbol])
            {
                externs.push_back(var.symbol);
                seen[var.symbol] = true;
            }
        }
    }

    return externs;
}

bool isViperUnit(std::string_view unitName)
{
    std::string key = toLower(unitName);
    return key == "viper.strings" || key == "viperstrings" || key == "viper.math" ||
           key == "vipermath" || key == "viper.terminal" || key == "viperterminal" ||
           key == "crt";  // CRT is common Pascal terminal unit name
}

std::vector<PascalBuiltin> getUnitBuiltins(std::string_view unitName)
{
    std::string key = toLower(unitName);
    std::vector<PascalBuiltin> result;

    if (key == "viper.strings" || key == "viperstrings")
    {
        result = {PascalBuiltin::Upper, PascalBuiltin::Lower, PascalBuiltin::Left,
                  PascalBuiltin::Right, PascalBuiltin::Mid,   PascalBuiltin::ChrStr,
                  PascalBuiltin::AscStr};
    }
    else if (key == "viper.math" || key == "vipermath")
    {
        result = {PascalBuiltin::Power, PascalBuiltin::Sign, PascalBuiltin::Min,
                  PascalBuiltin::Max};
    }
    else if (key == "viper.terminal" || key == "viperterminal" || key == "crt")
    {
        result = {PascalBuiltin::ClrScr,    PascalBuiltin::GotoXY,
                  PascalBuiltin::TextColor, PascalBuiltin::TextBackground,
                  PascalBuiltin::KeyPressed, PascalBuiltin::ReadKey,
                  PascalBuiltin::InKey,     PascalBuiltin::Delay,
                  PascalBuiltin::Sleep,     PascalBuiltin::HideCursor,
                  PascalBuiltin::ShowCursor};
    }

    return result;
}

} // namespace il::frontends::pascal
