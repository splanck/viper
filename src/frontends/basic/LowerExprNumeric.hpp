//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/LowerExprNumeric.hpp
// Purpose: Declares numeric expression lowering helpers for the BASIC Lowerer.
// Key invariants: Helpers reuse Lowerer conversions to maintain consistent type
// Ownership/Lifetime: Borrow the Lowerer reference for on-demand lowering.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/Lowerer.hpp"

#include <optional>

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
    struct NumericOpConfig
    {
        bool isFloat{false};
        il::core::Type arithmeticType{};
        il::core::Type resultType{};
    };

    struct OpcodeSelection
    {
        il::core::Opcode opcode{il::core::Opcode::IAddOvf};
        il::core::Type resultType{};
        bool promoteBoolToI64{false};
    };

    [[nodiscard]] NumericOpConfig normalizeNumericOperands(const BinaryExpr &expr,
                                                           Lowerer::RVal &lhs,
                                                           Lowerer::RVal &rhs);

    [[nodiscard]] std::optional<Lowerer::RVal> applySpecialConstantPatterns(
        const BinaryExpr &expr,
        Lowerer::RVal &lhs,
        Lowerer::RVal &rhs,
        const NumericOpConfig &config);

    [[nodiscard]] OpcodeSelection selectNumericOpcode(BinaryExpr::Op op,
                                                      const NumericOpConfig &config);

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
