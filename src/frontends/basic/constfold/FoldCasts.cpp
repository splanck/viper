//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// This source file is part of the Viper project.
//
// File: src/frontends/basic/constfold/FoldCasts.cpp
// Purpose: House the cast-oriented entry point for the BASIC constant folder,
//          providing a single location to enable literal cast folding once the
//          frontend exposes suitable semantics.
// Key invariants: Returns `std::nullopt` for every invocation until cast rules
//                 are defined, ensuring dispatchers can call the hook safely.
// Ownership/Lifetime: No allocations; simply forwards state back to callers.
// Links: docs/codemap.md, docs/il-guide.md#basic-frontend-constant-folding
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides stubs for cast folding.
/// @details BASIC currently implements casts via runtime builtins that the
///          constant folder cannot evaluate. The stub keeps the dispatcher
///          interface stable while documenting the intended expansion point.

#include "frontends/basic/constfold/Dispatch.hpp"

namespace il::frontends::basic::constfold
{

/// @brief Placeholder hook for cast constant folding.
/// @details Returns @c std::nullopt for all invocations, signalling that cast
///          expressions must be evaluated at runtime until literal folding rules
///          are implemented.
/// @param op Cast-related opcode (unused).
/// @param lhs Left-hand operand supplied by the dispatcher (unused).
/// @param rhs Right-hand operand supplied by the dispatcher (unused).
/// @return Always @c std::nullopt.
std::optional<Constant> fold_cast(AST::BinaryExpr::Op op, const Constant &lhs, const Constant &rhs)
{
    (void)op;
    (void)lhs;
    (void)rhs;
    return std::nullopt;
}

} // namespace il::frontends::basic::constfold
