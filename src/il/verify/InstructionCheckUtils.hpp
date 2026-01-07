//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares low-level utility functions used throughout instruction
// verification. These helpers provide common operations on IL type metadata and
// literal value validation that are needed by multiple verification strategies.
//
// Instruction verification requires checking that literal operands fit within
// their declared types and that opcode metadata correctly describes instruction
// semantics. Rather than duplicating these checks across verification strategies,
// this file centralizes them as reusable building blocks.
//
// Key Responsibilities:
// - Validate integer literals fit within their declared integer kind
// - Map opcode type categories to concrete type kinds when possible
// - Provide type metadata queries used across verification strategies
//
// Design Notes:
// All functions in this file are stateless pure functions operating on IL type
// metadata. They form a utility layer below the main verification logic, providing
// primitive operations used to implement higher-level verification rules. The
// functions are in the il::verify::detail namespace to indicate they are internal
// helpers not part of the public verification API.
//
//===----------------------------------------------------------------------===//

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
