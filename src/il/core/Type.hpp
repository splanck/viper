// File: src/il/core/Type.hpp
// Purpose: Declares IL type representation.
// Key invariants: Kind field determines payload.
// Ownership/Lifetime: Types are lightweight values.
// Links: docs/il-spec.md
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
        I64,
        F64,
        Ptr,
        Str
    };
    Kind kind; ///< Discriminator specifying the active kind

    /// @brief Construct a type of kind @p k.
    /// @param k Desired kind.
    constexpr explicit Type(Kind k = Kind::Void) : kind(k) {}

    /// @brief Convert type to string representation.
    /// @return Lowercase type mnemonic.
    std::string toString() const;
};

/// @brief Convert kind @p k to its mnemonic string.
/// @param k Kind to convert.
/// @return Lowercase mnemonic defined in the spec.
inline std::string kindToString(Type::Kind k)
{
    switch (k)
    {
        case Type::Kind::Void:
            return "void";
        case Type::Kind::I1:
            return "i1";
        case Type::Kind::I64:
            return "i64";
        case Type::Kind::F64:
            return "f64";
        case Type::Kind::Ptr:
            return "ptr";
        case Type::Kind::Str:
            return "str";
    }
    return "";
}

/// @brief Stringify this type.
/// @return Lowercase mnemonic of the kind.
inline std::string Type::toString() const
{
    return kindToString(kind);
}

} // namespace il::core
