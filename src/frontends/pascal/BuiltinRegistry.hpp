//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/BuiltinRegistry.hpp
// Purpose: Declares the Pascal builtin function registry.
// Key invariants: Maps Pascal names to runtime symbols and signatures.
// Ownership/Lifetime: Static data; no dynamic allocation.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/pascal/SemanticAnalyzer.hpp"
#include <optional>
#include <string_view>
#include <vector>

namespace il::frontends::pascal
{

//===----------------------------------------------------------------------===//
// Builtin Categories
//===----------------------------------------------------------------------===//

/// @brief Categories of Pascal builtins for organization and lowering.
enum class BuiltinCategory
{
    Builtin,        ///< Core Pascal builtins (Write, ReadLn, Length, etc.)
    ViperStrings,   ///< Viper.Strings unit functions
    ViperMath,      ///< Viper.Math unit functions
    ViperTerminal,  ///< Viper.Terminal unit functions (console control)
    ViperIO,        ///< Viper.IO unit functions (file I/O)
    ViperDateTime,  ///< Viper.DateTime unit functions
    ViperEnvironment, ///< Viper.Environment unit functions
};

//===----------------------------------------------------------------------===//
// Builtin Identifiers
//===----------------------------------------------------------------------===//

/// @brief Enumeration of all Pascal builtin functions.
enum class PascalBuiltin
{
    // I/O
    Write,
    WriteLn,
    Read,
    ReadLn,
    ReadInteger, ///< Read line and parse as Integer
    ReadReal,    ///< Read line and parse as Real

    // String functions
    Length,
    SetLength,
    IntToStr,
    RealToStr,   ///< Spec name (alias for FloatToStr)
    FloatToStr,  ///< Extension (same as RealToStr)
    StrToInt,
    StrToReal,   ///< Spec name (alias for StrToFloat)
    StrToFloat,  ///< Extension (same as StrToReal)
    Copy,
    Pos,
    Concat,
    Trim,

    // Ordinal functions
    Ord,
    Chr,
    Pred,
    Succ,
    Inc,
    Dec,
    Low,
    High,

    // Math functions
    Abs,
    Sqr,
    Sqrt,
    Sin,
    Cos,
    Tan,
    ArcTan,
    Exp,
    Ln,
    Trunc,
    Round,
    Floor,
    Ceil,
    Random,
    Randomize,

    // Type conversion
    Integer, // Integer(x) cast
    Real,    // Real(x) cast

    // Array
    SetLengthArr,

    // Viper.Strings unit
    Upper,
    Lower,
    Left,
    Right,
    Mid,
    ChrStr, ///< Chr in Viper.Strings (integer to 1-byte string)
    AscStr, ///< Asc in Viper.Strings (first byte to integer)

    // Viper.Math unit
    Pow,   ///< Spec name for power function
    Power, ///< Extension (alias for Pow)
    Atan,  ///< Spec name for arc tangent
    Sign,
    Min,
    Max,

    // Viper.Terminal unit (console/terminal control)
    ClrScr,       ///< Clear screen
    GotoXY,       ///< Position cursor (col, row)
    TextColor,    ///< Set foreground color
    TextBackground, ///< Set background color
    KeyPressed,   ///< Check if key available (non-blocking)
    ReadKey,      ///< Read single key (blocking)
    InKey,        ///< Read single key (non-blocking, returns empty if none)
    Delay,        ///< Pause execution (milliseconds)
    Sleep,        ///< Alias for Delay
    HideCursor,   ///< Hide terminal cursor
    ShowCursor,   ///< Show terminal cursor

    // Viper.IO unit (file I/O)
    FileExists,    ///< Check if file exists
    ReadAllText,   ///< Read entire file as string
    WriteAllText,  ///< Write string to file
    DeleteFile,    ///< Delete a file

    // Viper.Strings unit (additional string functions)
    TrimStart,     ///< Remove leading whitespace
    TrimEnd,       ///< Remove trailing whitespace
    IndexOf,       ///< Find substring position
    Substring,     ///< Extract substring

    // Viper.DateTime unit
    Now,           ///< Current timestamp (seconds since epoch)
    NowMs,         ///< Current timestamp (milliseconds since epoch)
    Year,          ///< Extract year from timestamp
    Month,         ///< Extract month from timestamp
    Day,           ///< Extract day from timestamp
    Hour,          ///< Extract hour from timestamp
    Minute,        ///< Extract minute from timestamp
    Second,        ///< Extract second from timestamp
    DayOfWeek,     ///< Extract day of week from timestamp
    FormatDateTime, ///< Format timestamp as string
    CreateDateTime, ///< Create timestamp from components

    // Viper.Environment unit
    ParamCount,    ///< Number of command-line arguments
    ParamStr,      ///< Get command-line argument by index
    GetCommandLine, ///< Get full command line

    // Count (must be last)
    Count
};

//===----------------------------------------------------------------------===//
// Argument Type Constraints
//===----------------------------------------------------------------------===//

/// @brief Bitmask for allowed argument types.
enum class ArgTypeMask : uint8_t
{
    None = 0,
    Integer = 1U << 0U,
    Real = 1U << 1U,
    String = 1U << 2U,
    Boolean = 1U << 3U,
    Array = 1U << 4U,
    Any = Integer | Real | String | Boolean | Array,
    Numeric = Integer | Real,
    Ordinal = Integer | Boolean,
};

/// @brief Bitwise OR for ArgTypeMask.
inline ArgTypeMask operator|(ArgTypeMask a, ArgTypeMask b)
{
    return static_cast<ArgTypeMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

/// @brief Bitwise AND for ArgTypeMask.
inline bool operator&(ArgTypeMask a, ArgTypeMask b)
{
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

//===----------------------------------------------------------------------===//
// Builtin Descriptor
//===----------------------------------------------------------------------===//

/// @brief Argument specification for a builtin.
struct BuiltinArgSpec
{
    ArgTypeMask allowed{ArgTypeMask::Any}; ///< Allowed types
    bool isVar{false};                     ///< Is var/out parameter
    bool optional{false};                  ///< Is optional argument
};

/// @brief Result type specification.
enum class ResultKind
{
    Void,    ///< No return value
    Integer, ///< Returns Integer
    Real,    ///< Returns Real
    String,  ///< Returns String
    Boolean, ///< Returns Boolean
    FromArg, ///< Same type as argument (index in resultArgIndex)
};

/// @brief Runtime call variant for lowering.
struct RuntimeVariant
{
    const char *symbol{nullptr}; ///< Runtime symbol name
    PasTypeKind argType{
        PasTypeKind::Unknown}; ///< Argument type for variant dispatch (Unknown = any)
};

/// @brief Complete descriptor for a Pascal builtin.
struct BuiltinDescriptor
{
    const char *name{nullptr};                   ///< Pascal source name
    PascalBuiltin id{PascalBuiltin::Count};      ///< Builtin identifier
    BuiltinCategory category{BuiltinCategory::Builtin}; ///< Category
    uint8_t minArgs{0};                          ///< Minimum arguments
    uint8_t maxArgs{0};                          ///< Maximum arguments
    bool variadic{false};                        ///< True for variadic (Write, WriteLn)
    ResultKind result{ResultKind::Void};         ///< Result type kind
    uint8_t resultArgIndex{0};                   ///< Argument index for FromArg result

    /// @brief Runtime symbol(s) for lowering.
    /// For type-dispatched builtins (Abs, etc.), multiple variants may exist.
    std::vector<RuntimeVariant> runtimeVariants;

    /// @brief Argument specifications (up to maxArgs).
    std::vector<BuiltinArgSpec> args;
};

//===----------------------------------------------------------------------===//
// Registry Interface
//===----------------------------------------------------------------------===//

/// @brief Look up a builtin by Pascal name (case-insensitive).
/// @param name The Pascal function name.
/// @return Builtin enum if found, nullopt otherwise.
std::optional<PascalBuiltin> lookupBuiltin(std::string_view name);

/// @brief Get the descriptor for a builtin.
/// @param id The builtin identifier.
/// @return Reference to the descriptor.
const BuiltinDescriptor &getBuiltinDescriptor(PascalBuiltin id);

/// @brief Get the runtime symbol for a builtin call.
/// @param id The builtin identifier.
/// @param argType The type of the first argument (for type-dispatched builtins).
/// @return Runtime symbol name, or nullptr if not lowered to runtime call.
const char *getBuiltinRuntimeSymbol(PascalBuiltin id, PasTypeKind argType = PasTypeKind::Unknown);

/// @brief Check if a builtin is a procedure (returns void).
/// @param id The builtin identifier.
/// @return True if the builtin is a procedure.
bool isBuiltinProcedure(PascalBuiltin id);

/// @brief Get the result PasType for a builtin.
/// @param id The builtin identifier.
/// @param argType The type of the relevant argument (for FromArg results).
/// @return The result type.
PasType getBuiltinResultType(PascalBuiltin id, PasTypeKind argType = PasTypeKind::Unknown);

/// @brief Get the list of extern declarations needed for a set of used builtins.
/// @param usedBuiltins Set of builtins used in the program.
/// @return List of runtime symbols that need extern declarations.
std::vector<std::string> getRequiredExterns(const std::vector<PascalBuiltin> &usedBuiltins);

//===----------------------------------------------------------------------===//
// Builtin Registration for Units
//===----------------------------------------------------------------------===//

/// @brief Check if a unit name is a known Viper standard unit.
/// @param unitName The unit name to check.
/// @return True if it's a Viper standard unit.
bool isViperUnit(std::string_view unitName);

/// @brief Get builtins provided by a Viper unit.
/// @param unitName The unit name.
/// @return List of builtins from that unit.
std::vector<PascalBuiltin> getUnitBuiltins(std::string_view unitName);

} // namespace il::frontends::pascal
