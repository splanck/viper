// File: src/frontends/basic/BuiltinRegistry.hpp
// Purpose: Central registry of BASIC built-in functions mapping names to
//          semantic and lowering hooks.
// Key invariants: Table order matches BuiltinCallExpr::Builtin enum.
// Ownership/Lifetime: Static compile-time data only; no dynamic allocation.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/Lowerer.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace il::frontends::basic
{
/// @brief Bitmask describing allowable BASIC value categories for builtin arguments.
enum class BuiltinTypeMask : std::uint8_t
{
    None = 0,
    Int = 1u << 0,
    Float = 1u << 1,
    String = 1u << 2,
    Bool = 1u << 3,
};

constexpr BuiltinTypeMask operator|(BuiltinTypeMask lhs, BuiltinTypeMask rhs)
{
    return static_cast<BuiltinTypeMask>(static_cast<std::uint8_t>(lhs) |
                                        static_cast<std::uint8_t>(rhs));
}

constexpr BuiltinTypeMask operator&(BuiltinTypeMask lhs, BuiltinTypeMask rhs)
{
    return static_cast<BuiltinTypeMask>(static_cast<std::uint8_t>(lhs) &
                                        static_cast<std::uint8_t>(rhs));
}

constexpr bool any(BuiltinTypeMask mask)
{
    return mask != BuiltinTypeMask::None;
}

/// @brief Per-argument type constraints for a builtin signature.
struct BuiltinArgSpec
{
    BuiltinTypeMask allowed;
};

/// @brief Single callable signature for a builtin (fixed arity/argument types).
struct BuiltinSignature
{
    const BuiltinArgSpec *args;
    std::size_t argCount;
};

/// @brief Result inference strategy for a builtin call.
enum class BuiltinResultKind
{
    Int,
    Float,
    String,
    Bool,
    NumericLikeFirstArg,
    Unknown,
};

/// @brief Metadata for a BASIC built-in function.
struct BuiltinInfo
{
    const char *name;    ///< BASIC source spelling.
    std::size_t minArgs; ///< Minimum accepted arguments.
    std::size_t maxArgs; ///< Maximum accepted arguments.
    const BuiltinSignature *signatures; ///< Supported signatures for type checking.
    std::size_t signatureCount;         ///< Number of signatures.
    BuiltinResultKind result;           ///< Result inference rule.

    using LowerFn = typename Lowerer::RVal (Lowerer::*)(const BuiltinCallExpr &);
    LowerFn lower; ///< Lowering hook.

    using ScanFn = typename Lowerer::ExprType (Lowerer::*)(const BuiltinCallExpr &);
    ScanFn scan; ///< Pre-lowering scan hook.
};

/// @brief Lookup builtin info by enum.
const BuiltinInfo &getBuiltinInfo(BuiltinCallExpr::Builtin b);

/// @brief Find builtin enum by BASIC name.
std::optional<BuiltinCallExpr::Builtin> lookupBuiltin(std::string_view name);

} // namespace il::frontends::basic
