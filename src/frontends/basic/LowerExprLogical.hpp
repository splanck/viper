// File: src/frontends/basic/LowerExprLogical.hpp
// Purpose: Declares logical expression lowering helpers that operate on the
//          BASIC Lowerer implementation.
// Key invariants: Helper objects reuse Lowerer state and utilities when
//                 materializing logical values.
// Ownership/Lifetime: Helpers borrow the Lowerer reference for the duration of
//                      a single lowering operation.
// Links: docs/codemap.md
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
