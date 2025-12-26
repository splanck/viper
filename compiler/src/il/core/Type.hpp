//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the IL type representation system used throughout the
// Viper compiler infrastructure. The Type struct provides a lightweight,
// value-based wrapper around an enumerated type kind that represents the
// primitive types supported by Viper IL.
//
// Viper IL supports 10 primitive types: void, i1, i16, i32, i64, f64, ptr,
// str, error, and resumetok. Each type is represented by a Type::Kind enum
// discriminator. Types are designed to be copied by value with minimal
// overhead, making them suitable for frequent use during IR construction,
// analysis, and transformation passes.
//
// Key Design Decisions:
// - Value semantics: Types are lightweight POD structs suitable for copying
// - No parametric types: Pointer and string types are opaque (no element types)
// - Enumerated kinds: Simple discriminated union without additional payload
// - String conversion: Every type can be serialized to its IL spec mnemonic
//
// The type system is intentionally minimal to keep the IL layer simple and
// focused. Higher-level type systems in frontend languages are mapped to
// these primitives during lowering.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace il::core
{

/// @brief Simple type wrapper for IL primitive types.
struct Type
{
    /// @brief Enumerates primitive IL types.
    enum class Kind
    {
        Void,
        I1,
        I16,
        I32,
        I64,
        F64,
        Ptr,
        Str,
        Error,
        ResumeTok
    };
    Kind kind; ///< Discriminator specifying the active kind

    /// @brief Construct a type of kind @p k.
    /// @param k Desired kind.
    explicit Type(Kind k = Kind::Void);

    /// @brief Convert type to string representation.
    /// @return Lowercase type mnemonic.
    std::string toString() const;
};

/// @brief Convert kind @p k to its mnemonic string.
/// @param k Kind to convert.
/// @return Lowercase mnemonic defined in the spec.
std::string kindToString(Type::Kind k);

} // namespace il::core
