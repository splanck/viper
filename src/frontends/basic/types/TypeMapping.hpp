//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the mapIlToBasic() utility function, which translates
// IL core types to their corresponding BASIC frontend scalar types. This
// mapping is used when importing external function signatures (e.g., from
// runtime library declarations) into the BASIC frontend's type system.
//
// Supported mappings include i64 -> Long, f64 -> Double, i1 -> Boolean,
// ptr -> String (for string pointer parameters), and void -> Void.
// Unsupported or unrecognized IL types return std::nullopt.
//
// Key invariants:
//   - The mapping is one-directional (IL -> BASIC only).
//   - Returns std::nullopt for aggregate or vector types not representable
//     in the BASIC scalar type system.
//   - The returned type is a BASIC AST Type value, not an IL type.
//
// Ownership: Stateless function; no heap allocation or external state.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"
#include "il/core/Type.hpp"
#include <optional>

namespace il::frontends::basic::types
{

/// @brief Map an IL core type to the corresponding BASIC AST scalar type.
/// @details Translates IL types (i64, f64, i1, ptr, void) into their BASIC
///          equivalents (Long, Double, Boolean, String, Void). Returns nullopt
///          for IL types that have no direct BASIC representation, such as
///          aggregate or vector types.
/// @param ilType The IL core type to translate.
/// @return The corresponding BASIC type, or std::nullopt if unsupported.
std::optional<Type> mapIlToBasic(const il::core::Type &ilType);

} // namespace il::frontends::basic::types
