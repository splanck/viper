// File: src/il/core/Type.h
// Purpose: Declares IL type representation.
// Key invariants: Kind field determines payload.
// Ownership/Lifetime: Types are lightweight values.
// Links: docs/il-spec.md
#pragma once
#include <string>

namespace il::core {

/// @brief Simple type wrapper for IL primitive types.
struct Type {
  enum class Kind { Void, I1, I64, F64, Ptr, Str };
  Kind kind;
  constexpr explicit Type(Kind k = Kind::Void) : kind(k) {}
  std::string toString() const;
};

inline std::string kindToString(Type::Kind k) {
  switch (k) {
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

inline std::string Type::toString() const { return kindToString(kind); }

} // namespace il::core
