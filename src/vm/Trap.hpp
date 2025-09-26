// File: src/vm/Trap.hpp
// Purpose: Defines trap classification for VM diagnostics.
// Key invariants: Enum values map directly to trap categories used in diagnostics.
// Ownership/Lifetime: Not applicable.
// Links: docs/il-guide.md#reference
#pragma once

#include <string_view>

namespace il::vm
{

/// @brief Categorises runtime traps for diagnostic reporting.
enum class TrapKind
{
    DivideByZero, ///< Integer division or remainder by zero.
    Overflow,     ///< Arithmetic or conversion overflow.
    InvalidCast,  ///< Invalid cast or conversion semantics.
    DomainError,  ///< Semantic domain violation or user trap.
};

/// @brief Convert trap kind to canonical diagnostic string.
/// @param kind Enumerated trap kind.
/// @return Stable string view naming the trap category.
constexpr std::string_view toString(TrapKind kind)
{
    switch (kind)
    {
        case TrapKind::DivideByZero:
            return "DivideByZero";
        case TrapKind::Overflow:
            return "Overflow";
        case TrapKind::InvalidCast:
            return "InvalidCast";
        case TrapKind::DomainError:
            return "DomainError";
    }
    return "DomainError";
}

} // namespace il::vm
