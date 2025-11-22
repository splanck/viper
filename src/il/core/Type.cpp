//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/core/Type.cpp
// Purpose: Implement the minimal helpers for rendering and constructing IL type
//          descriptors without exposing representation details in headers.
// Links: docs/il-guide.md#types
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides stringification helpers for IL type descriptors.
/// @details Keeping the logic here allows other translation units to forward
///          declare il::core::Type without depending on implementation details
///          while still sharing the canonical textual rendering helpers.

#include "il/core/Type.hpp"

namespace il::core
{

/// @brief Construct a Type wrapper from the provided kind enumerator.
///
/// The constructor simply stores the enumeration value.  It exists so that the
/// class can be forward declared without exposing the underlying representation
/// to callers.
///
/// @param k Enumeration tag describing the type category.
Type::Type(Kind k) : kind(k) {}

/// @brief Render an IL type enumerator to its canonical textual spelling.
///
/// Used by diagnostics and the text serialiser to produce stable, lower-case
/// mnemonics that match the IL specification.  Unknown enumerators fall back to
/// the empty string so callers can provide bespoke error messaging.
///
/// @param k Enumeration tag to translate.
/// @return Canonical string name for the provided type tag.
std::string kindToString(Type::Kind k)
{
/// @brief Implements switch functionality.
/// @param k Parameter description needed.
/// @return Return value description needed.
    switch (k)
    {
        case Type::Kind::Void:
            return "void";
        case Type::Kind::I1:
            return "i1";
        case Type::Kind::I16:
            return "i16";
        case Type::Kind::I32:
            return "i32";
        case Type::Kind::I64:
            return "i64";
        case Type::Kind::F64:
            return "f64";
        case Type::Kind::Ptr:
            return "ptr";
        case Type::Kind::Str:
            return "str";
        case Type::Kind::Error:
            return "error";
        case Type::Kind::ResumeTok:
            return "resume_tok";
    }
    return "";
}

/// @brief Produce the canonical spelling for this type instance.
///
/// Delegates to @ref kindToString so that all textual rendering flows through a
/// single implementation.
///
/// @return Canonical string representation of the stored type tag.
std::string Type::toString() const
{
    return kindToString(kind);
}

} // namespace il::core
