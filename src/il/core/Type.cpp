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

#include <array>
#include <string_view>

namespace il::core {

/// @brief Construct a Type wrapper from the provided kind enumerator.
///
/// The constructor simply stores the enumeration value.  It exists so that the
/// class can be forward declared without exposing the underlying representation
/// to callers.
///
/// @param k Enumeration tag describing the type category.
Type::Type(Kind k) : kind(k) {}

/// @brief Static lookup table for type kind string representations.
/// @details Uses O(1) array indexing instead of switch statement branching.
///          Index order must match Type::Kind enum values exactly.
static constexpr std::array<std::string_view, 10> kTypeKindNames = {{
    "void",      // Kind::Void = 0
    "i1",        // Kind::I1 = 1
    "i16",       // Kind::I16 = 2
    "i32",       // Kind::I32 = 3
    "i64",       // Kind::I64 = 4
    "f64",       // Kind::F64 = 5
    "ptr",       // Kind::Ptr = 6
    "str",       // Kind::Str = 7
    "error",     // Kind::Error = 8
    "resume_tok" // Kind::ResumeTok = 9
}};

/// @brief Render an IL type enumerator to its canonical textual spelling.
///
/// Used by diagnostics and the text serialiser to produce stable, lower-case
/// mnemonics that match the IL specification.  Unknown enumerators fall back to
/// the empty string so callers can provide bespoke error messaging.
///
/// @details Uses O(1) array lookup instead of switch statement for guaranteed
///          constant-time performance regardless of enum value distribution.
///
/// @param k Enumeration tag to translate.
/// @return Canonical string name for the provided type tag.
std::string kindToString(Type::Kind k) {
    const auto index = static_cast<std::size_t>(k);
    if (index < kTypeKindNames.size())
        return std::string(kTypeKindNames[index]);
    return "";
}

/// @brief Return the byte width used to represent values of @p kind in memory.
/// @details The size table matches the IL verifier and optimizer storage model:
///          integer widths use their natural byte sizes, opaque pointer-like
///          handles use one machine word in IL memory, and structured error
///          records occupy three words.  Void intentionally has no storage size.
/// @param kind Type kind to classify.
/// @return Number of bytes used by the type, or empty for void/invalid kinds.
std::optional<unsigned> storageSizeBytes(Type::Kind kind) {
    switch (kind) {
        case Type::Kind::I1:
            return 1;
        case Type::Kind::I16:
            return 2;
        case Type::Kind::I32:
            return 4;
        case Type::Kind::I64:
        case Type::Kind::F64:
        case Type::Kind::Ptr:
        case Type::Kind::Str:
        case Type::Kind::ResumeTok:
            return 8;
        case Type::Kind::Error:
            return 24;
        case Type::Kind::Void:
            return std::nullopt;
    }
    return std::nullopt;
}

/// @brief Return the byte width used to represent values of @p type in memory.
/// @param type Type wrapper whose kind should be classified.
/// @return Number of bytes used by the type, or empty for void/invalid kinds.
std::optional<unsigned> storageSizeBytes(Type type) {
    return storageSizeBytes(type.kind);
}

/// @brief Produce the canonical spelling for this type instance.
///
/// Delegates to @ref kindToString so that all textual rendering flows through a
/// single implementation.
///
/// @return Canonical string representation of the stored type tag.
std::string Type::toString() const {
    return kindToString(kind);
}

} // namespace il::core
