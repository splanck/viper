//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/constfold/ConstantUtils.hpp
// Purpose: Provide shared helpers for constructing constant-folding results so
//          folding domains agree on literal encodings.
// Key invariants: Helpers always populate the @ref Constant wrapper with
//                 coherent kind tags and payload fields, ensuring downstream
//                 folders can rely on a consistent representation regardless of
//                 the originating domain.
// Ownership/Lifetime: Return values are plain aggregates without ownership
//                     semantics; callers receive them by value.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Shared helpers for constructing constant-folding literals.
/// @details Centralises constant creation utilities to keep folding domains
///          consistent when materialising literal results. The helpers ensure
///          that the @ref Constant wrapper is fully initialised so dispatchers
///          and materialisers observe the same representation.

#pragma once

#include "frontends/basic/constfold/Dispatch.hpp"

namespace il::frontends::basic::constfold
{

/// @brief Construct a boolean constant with coherent numeric metadata.
/// @param value Boolean payload to encode.
/// @return Constant describing @p value as a boolean literal.
inline Constant make_bool_constant(bool value) noexcept
{
    Constant constant;
    constant.kind = LiteralKind::Bool;
    constant.boolValue = value;
    constant.numeric = NumericValue{false, value ? 1.0 : 0.0, value ? 1 : 0};
    return constant;
}

} // namespace il::frontends::basic::constfold
