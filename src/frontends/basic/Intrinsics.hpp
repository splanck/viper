//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the registry of BASIC intrinsic functions, providing
// metadata for validation and code generation.
//
// BASIC Intrinsics:
// BASIC provides a set of intrinsic (built-in) functions that are part of
// the language specification rather than library functions. These include:
// - Mathematical: ABS, SIN, COS, TAN, ATN, EXP, LOG, SQR, INT, RND
// - String: LEN, ASC, CHR$, LEFT$, RIGHT$, MID$, INSTR, STR$, VAL
// - Type conversion: CINT, CLNG, CSNG, CDBL, HEX$, OCT$
// - System: EOF, LOF, TIMER, INPUT$
//
// Intrinsic Registry:
// Each intrinsic is described by static metadata:
// - Name: Function identifier (e.g., "SIN", "LEFT$")
// - Parameter types: Expected argument types
// - Return type: Result type of the function
// - Arity: Fixed or variable argument count
//
// This registry provides type information for:
// - Semantic validation during analysis
// - IL generation during lowering
// - Error message generation for invalid calls
//
// Type System:
// Intrinsics use BASIC's type system:
// - Int: Integer values (64-bit in this implementation)
// - Float: Floating-point values (64-bit double precision)
// - String: Variable-length character sequences
//
// Integration:
// - Used by: Parser to recognize intrinsic function calls
// - Used by: SemanticAnalyzer to validate intrinsic arguments
// - Used by: Lowerer to generate appropriate IL (inline or runtime call)
//
// Design Notes:
// - Intrinsic descriptors are immutable static data
// - Table covers all supported BASIC intrinsics
// - Callers must not free or modify descriptor entries
// - Separate from BuiltinRegistry which handles extension functions
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <ostream>
#include <string_view>

namespace il::frontends::basic::intrinsics
{

/// @brief Type of parameter or return value for a BASIC intrinsic.
enum class Type
{
    Int,    ///< 64-bit integer.
    Float,  ///< 64-bit floating point.
    String, ///< BASIC string.
    Numeric ///< Either Int or Float.
};

/// @brief Parameter descriptor.
struct Param
{
    Type type;     ///< Parameter type.
    bool optional; ///< True if the parameter is optional.
};

/// @brief Intrinsic function descriptor.
struct Intrinsic
{
    std::string_view name;  ///< BASIC name including $ suffix.
    Type returnType;        ///< Return type.
    const Param *params;    ///< Pointer to ordered parameter descriptors.
    std::size_t paramCount; ///< Number of parameters in @ref params.
};

/// @brief Lookup intrinsic by BASIC name.
/// @param name Name such as "LEFT$".
/// @return Descriptor or nullptr if unknown.
const Intrinsic *lookup(std::string_view name);

/// @brief Dump all intrinsic names separated by spaces to @p os.
/// @param os Output stream receiving names.
void dumpNames(std::ostream &os);

} // namespace il::frontends::basic::intrinsics
