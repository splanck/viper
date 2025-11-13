//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file defines the BasicType enumeration, which represents the BASIC
// language's type system at the frontend level.
//
// BASIC Type System:
// The BASIC language uses a simple type system with five fundamental types:
// - Integer: 16-bit signed integers (suffix: %)
// - Long: 32-bit signed integers (suffix: &)
// - Single: 32-bit floating-point (suffix: !)
// - Double: 64-bit floating-point (suffix: #)
// - String: Variable-length character sequences (suffix: $)
//
// Additionally, the Void type represents procedures (SUB) that do not return
// a value, while Unknown is used during type inference and error recovery.
//
// Type Suffixes:
// BASIC allows variables and functions to declare their type via a suffix:
//   counter% = 42         ' Integer variable
//   distance# = 3.14159   ' Double variable
//   name$ = "Alice"       ' String variable
//
// If no suffix is provided and no explicit DIM statement exists, BASIC defaults
// to Single (32-bit float) for numeric variables.
//
// Integration:
// - Used by: Parser for type annotation during AST construction
// - Used by: SemanticAnalyzer for type checking and validation
// - Used by: Lowerer for mapping BASIC types to IL primitive types
// - Mapped to IL types: Int→i32, Long→i64, Single→f32, Double→f64, String→string
//
// Design Notes:
// - This is a frontend-specific type representation; the lowerer translates
//   BasicType to IL primitive types
// - The enumeration is kept minimal to match BASIC's simple type system
// - Header-only implementation for efficiency
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
namespace il::frontends::basic
{

/// @brief Enumerates the BASIC-level types that can annotate function returns.
enum class BasicType
{
    Unknown,
    Int,
    Float,
    String,
    Void
};

/// @brief Converts a BasicType to its lowercase BASIC surface spelling.
/// @param t The BASIC type to convert.
/// @return Null-terminated string literal naming the type.
inline const char *toString(BasicType t)
{
    switch (t)
    {
        case BasicType::Unknown:
            return "unknown";
        case BasicType::Int:
            return "int";
        case BasicType::Float:
            return "float";
        case BasicType::String:
            return "string";
        case BasicType::Void:
            return "void";
    }
    return "?";
}

} // namespace il::frontends::basic

namespace il::frontends::basic
{

/// @brief Access control for declarations (default Public).
/// @notes Applies to CLASS/TYPE fields and class members.
enum class Access : std::uint8_t
{
    Public = 0,
    Private = 1,
};

} // namespace il::frontends::basic
