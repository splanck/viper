//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/LowerExprLogical.hpp
// Purpose: Declares logical expression lowering helpers that operate on the
// Key invariants: Helper objects reuse Lowerer state and utilities when
// Ownership/Lifetime: Helpers borrow the Lowerer reference for the duration of
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

struct LogicalExprLowering
{
    explicit LogicalExprLowering(Lowerer &lowerer) noexcept;

    [[nodiscard]] Lowerer::RVal lower(const BinaryExpr &expr);

  private:
    Lowerer *lowerer_{nullptr};
};

[[nodiscard]] Lowerer::RVal lowerLogicalBinary(Lowerer &lowerer, const BinaryExpr &expr);

} // namespace il::frontends::basic
