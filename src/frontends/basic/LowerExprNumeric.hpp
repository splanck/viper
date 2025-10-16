// File: src/frontends/basic/LowerExprNumeric.hpp
// Purpose: Declares numeric expression lowering helpers for the BASIC Lowerer.
// Key invariants: Helpers reuse Lowerer conversions to maintain consistent type
//                 coercions across arithmetic operations.
// Ownership/Lifetime: Borrow the Lowerer reference for on-demand lowering.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

struct NumericExprLowering
{
    explicit NumericExprLowering(Lowerer &lowerer) noexcept;

    [[nodiscard]] Lowerer::RVal lowerDivOrMod(const BinaryExpr &expr);

    [[nodiscard]] Lowerer::RVal lowerPowBinary(const BinaryExpr &expr,
                                               Lowerer::RVal lhs,
                                               Lowerer::RVal rhs);

    [[nodiscard]] Lowerer::RVal lowerStringBinary(const BinaryExpr &expr,
                                                  Lowerer::RVal lhs,
                                                  Lowerer::RVal rhs);

    [[nodiscard]] Lowerer::RVal lowerNumericBinary(const BinaryExpr &expr,
                                                   Lowerer::RVal lhs,
                                                   Lowerer::RVal rhs);

  private:
    Lowerer *lowerer_{nullptr};
};

[[nodiscard]] Lowerer::RVal lowerDivOrMod(Lowerer &lowerer, const BinaryExpr &expr);

[[nodiscard]] Lowerer::RVal lowerPowBinary(Lowerer &lowerer,
                                           const BinaryExpr &expr,
                                           Lowerer::RVal lhs,
                                           Lowerer::RVal rhs);

[[nodiscard]] Lowerer::RVal lowerStringBinary(Lowerer &lowerer,
                                              const BinaryExpr &expr,
                                              Lowerer::RVal lhs,
                                              Lowerer::RVal rhs);

[[nodiscard]] Lowerer::RVal lowerNumericBinary(Lowerer &lowerer,
                                               const BinaryExpr &expr,
                                               Lowerer::RVal lhs,
                                               Lowerer::RVal rhs);

} // namespace il::frontends::basic
