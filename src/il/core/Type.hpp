// File: src/il/core/Type.hpp
// Purpose: Declares IL type representation.
// Key invariants: Kind field determines payload.
// Ownership/Lifetime: Types are lightweight values.
// Links: docs/il-guide.md#reference
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
        F32,
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
