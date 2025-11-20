// File: src/frontends/basic/types/TypeMapping.cpp
// Purpose: Map IL core types to BASIC frontend scalar types for proc signatures.
// Key invariants: Only scalar types are supported (i64, f64, str, i1). Others are skipped.

#include "frontends/basic/types/TypeMapping.hpp"

namespace il::frontends::basic::types
{

std::optional<Type> mapIlToBasic(const il::core::Type &ilType)
{
    using K = il::core::Type::Kind;
    switch (ilType.kind)
    {
        case K::I64:
            return Type::I64;
        case K::F64:
            return Type::F64;
        case K::Str:
            return Type::Str;
        case K::I1:
            return Type::Bool;
        case K::Void:
            return std::nullopt; // Indicates SUB/void
        default:
            return std::nullopt; // Unsupported (i16/i32/ptr/error/resumetok)
    }
}

} // namespace il::frontends::basic::types
