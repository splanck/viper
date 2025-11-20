// File: src/frontends/basic/types/TypeMapping.hpp
// Purpose: Map IL core types to BASIC frontend scalar types for proc signatures.
// Ownership/Lifetime: Header-only decl; implementation in .cpp.
#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"
#include "il/core/Type.hpp"
#include <optional>

namespace il::frontends::basic::types
{

/// @brief Map an IL core type to a BASIC AST scalar type.
/// @return Mapped type when supported; std::nullopt for unsupported kinds.
std::optional<Type> mapIlToBasic(const il::core::Type &ilType);

} // namespace il::frontends::basic::types
