// File: src/frontends/basic/Intrinsics.hpp
// Purpose: Declare registry of BASIC intrinsic functions.
// Key invariants: Table entries are immutable and cover all supported intrinsics.
// Ownership/Lifetime: Intrinsic descriptors are static; callers must not free.
// Links: docs/codemap.md

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
