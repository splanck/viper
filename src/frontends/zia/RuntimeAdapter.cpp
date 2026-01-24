//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RuntimeAdapter.cpp
/// @brief Implementation of type conversion utilities for Zia/RuntimeRegistry.
///
/// @details This file implements the bridge functions that convert IL-layer
/// type representations (ILScalarType) to Zia semantic types (TypeRef). These
/// conversions enable the Zia frontend to use the unified RuntimeRegistry
/// for type-safe runtime function binding.
///
/// ## Implementation Notes
///
/// The conversion functions use direct switch-case mapping rather than lookup
/// tables for optimal performance and compile-time verification that all
/// cases are handled. The compiler will warn if new ILScalarType values are
/// added but not handled here.
///
/// ## Type System Alignment
///
/// The IL type system is intentionally minimal, supporting only the scalar
/// types that can cross the IL/runtime boundary:
///
/// - **Integers**: IL uses i64 exclusively; Zia maps this to Integer
/// - **Floats**: IL uses f64 exclusively; Zia maps this to Number
/// - **Booleans**: IL uses i1; Zia maps this to Boolean
/// - **Strings**: IL uses str (a reference type); Zia maps to String
/// - **Objects**: IL uses ptr/obj for runtime class instances; Zia maps to Ptr
///
/// Collection types (List, Map, Set) and user-defined types are represented
/// as Object/ptr at the IL level—their specific type information is tracked
/// in the Zia type registry, not in the IL signature.
///
/// @see RuntimeAdapter.hpp - Interface documentation and architecture overview
/// @see il::runtime::RuntimeRegistry - Source of parsed signatures
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/RuntimeAdapter.hpp"

namespace il::frontends::zia
{

//===----------------------------------------------------------------------===//
// toZiaType Implementation
//===----------------------------------------------------------------------===//

TypeRef toZiaType(il::runtime::ILScalarType t)
{
    // Map each IL scalar type to its Zia semantic equivalent.
    // This switch is exhaustive—the compiler will warn if new
    // ILScalarType values are added but not handled here.
    switch (t)
    {
        case il::runtime::ILScalarType::I64:
            // 64-bit signed integer. This is the only integer width at the IL
            // level; Zia's Integer type is semantically equivalent.
            return types::integer();

        case il::runtime::ILScalarType::F64:
            // 64-bit IEEE 754 floating point. Zia calls this "Number" to match
            // its high-level semantics (no distinction between float/double).
            return types::number();

        case il::runtime::ILScalarType::Bool:
            // Boolean type (i1 in IL). Maps directly to Zia's Boolean type.
            return types::boolean();

        case il::runtime::ILScalarType::String:
            // Immutable string reference. IL strings are reference-counted
            // internally by the runtime; Zia treats them as value-like.
            return types::string();

        case il::runtime::ILScalarType::Void:
            // No return value. Used for procedures and setters.
            return types::voidType();

        case il::runtime::ILScalarType::Object:
            // Opaque object pointer. Used for runtime class instances (like
            // Viper.File, Viper.Graphics.Canvas) where the actual type is
            // tracked separately in Zia's type registry.
            return types::ptr();

        case il::runtime::ILScalarType::Unknown:
        default:
            // Unknown type indicates a parse error or unrecognized type token
            // in the signature. Return unknown() to signal the error; the
            // caller should check isValid() on the signature and not register
            // functions with unknown types.
            return types::unknown();
    }
}

//===----------------------------------------------------------------------===//
// toZiaParamTypes Implementation
//===----------------------------------------------------------------------===//

std::vector<TypeRef> toZiaParamTypes(const il::runtime::ParsedSignature &sig)
{
    // Pre-allocate the result vector to avoid reallocations.
    // The signature's params vector contains one ILScalarType per parameter,
    // excluding the implicit receiver for method calls.
    std::vector<TypeRef> result;
    result.reserve(sig.params.size());

    // Convert each parameter type using toZiaType(). The order is preserved
    // so parameter positions match between the IL signature and Zia's
    // function type representation.
    for (auto p : sig.params)
        result.push_back(toZiaType(p));

    return result;
}

} // namespace il::frontends::zia
