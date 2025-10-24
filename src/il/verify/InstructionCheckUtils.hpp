// File: src/il/verify/InstructionCheckUtils.hpp
// Purpose: Declares reusable helpers shared across instruction verification routines.
// Key invariants: Utility functions operate on fundamental IL metadata enums and kinds.
// Ownership/Lifetime: Stateless helpers that do not manage resources.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/OpcodeInfo.hpp"
#include "il/core/Type.hpp"

#include <optional>

namespace il::verify::detail
{

/// @brief Checks whether an integer literal fits within the specified kind.
/// @param value Literal value to test.
/// @param kind Target integer kind describing the representable range.
/// @return True when @p value can be represented without loss.
bool fitsInIntegerKind(long long value, il::core::Type::Kind kind);

/// @brief Maps a type category to a concrete type kind when available.
/// @param category Opcode metadata category to translate.
/// @return Optional kind describing the category, or std::nullopt when dynamic.
std::optional<il::core::Type::Kind> kindFromCategory(il::core::TypeCategory category);

} // namespace il::verify::detail
