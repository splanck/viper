//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file LowererBinaryOperatorLowerer.hpp
/// @brief Helper for lowering non-assignment binary operators.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/Lowerer.hpp"

namespace il::frontends::zia {

/// @brief Lowers binary operators after assignment and short-circuit handling.
///
/// @details Keeps arithmetic, comparison, string concatenation, and bitwise
///          operator selection out of the main binary expression entry file.
class BinaryOperatorLowerer final {
  public:
    explicit BinaryOperatorLowerer(Lowerer &lowerer) : lowerer_(lowerer) {}

    LowerResult lowerBinary(BinaryExpr *expr);

  private:
    using Type = il::core::Type;
    using Value = il::core::Value;
    using Opcode = il::core::Opcode;

    Lowerer &lowerer_;
};

} // namespace il::frontends::zia
