// src/frontends/basic/BasicTypes.hpp
#pragma once

#include <cstdint>

/// @brief Shared BASIC front end value category enumeration.
/// @invariant Enum values correspond to parser and lowerer expectations for BASIC
///            function return annotations.
/// @ownership Header-only utilities; no dynamic ownership.
/// @notes See docs/roadmap.md for BASIC front end plan.
namespace il::frontends::basic
{

/// @brief Enumerates the BASIC-level types that can annotate function returns.
enum class BasicType
{
    Unknown,
    Int,
    Float,
    String,
    Void
};

/// @brief Converts a BasicType to its lowercase BASIC surface spelling.
/// @param t The BASIC type to convert.
/// @return Null-terminated string literal naming the type.
inline const char *toString(BasicType t)
{
    switch (t)
    {
        case BasicType::Unknown:
            return "unknown";
        case BasicType::Int:
            return "int";
        case BasicType::Float:
            return "float";
        case BasicType::String:
            return "string";
        case BasicType::Void:
            return "void";
    }
    return "?";
}

} // namespace il::frontends::basic

namespace il::frontends::basic
{

/// @brief Access control for declarations (default Public).
/// @notes Applies to CLASS/TYPE fields and class members.
enum class Access : std::uint8_t
{
    Public = 0,
    Private = 1,
};

} // namespace il::frontends::basic
