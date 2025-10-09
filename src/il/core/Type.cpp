//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the minimal helpers that accompany the lightweight IL type model.
// The translation unit primarily provides human-readable rendering utilities so
// diagnostics and text serialisers can surface type information without pulling
// in additional headers.  Keeping these helpers out-of-line preserves a stable
// location for future extensions without bloating the header-only API surface.
//
//===----------------------------------------------------------------------===//

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
