// File: src/il/core/Type.cpp
// Purpose: Implements helpers for IL type system.
// Key invariants: None.
// Ownership/Lifetime: Types are value objects.
// Links: docs/il-spec.md

#include "il/core/Type.hpp"

#include <string>

namespace il::core
{

std::string kindToString(Type::Kind k)
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

std::string Type::toString() const
{
    return kindToString(kind);
}

} // namespace il::core
