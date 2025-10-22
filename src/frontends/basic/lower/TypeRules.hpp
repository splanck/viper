// File: src/frontends/basic/lower/TypeRules.hpp
// Purpose: Declares helper rules for numeric classification during lowering.
// Key invariants: Mirrors BASIC promotion semantics for expressions and calls.
// Ownership/Lifetime: Stateless utilities; no retained resources.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/TypeRules.hpp"

#include <optional>

namespace il::frontends::basic
{

class Lowerer;
struct BinaryExpr;
struct BuiltinCallExpr;
struct CallExpr;

} // namespace il::frontends::basic

namespace il::frontends::basic::lower
{

/// @brief Determine the numeric result for a BASIC binary operator.
TypeRules::NumericType classifyBinaryNumericResult(const BinaryExpr &bin,
                                                   TypeRules::NumericType lhs,
                                                   TypeRules::NumericType rhs) noexcept;

/// @brief Determine the numeric result for a builtin call expression.
TypeRules::NumericType classifyBuiltinCall(const BuiltinCallExpr &call,
                                           std::optional<TypeRules::NumericType> firstArgType);

/// @brief Determine the numeric result for a user-defined procedure call.
TypeRules::NumericType classifyProcedureCall(const Lowerer &lowerer, const CallExpr &call);

} // namespace il::frontends::basic::lower

