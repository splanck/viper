//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/lower/detail/RuntimeLoweringHelper.cpp
//
// Summary:
//   Implements RuntimeLoweringHelper which coordinates runtime statement
//   lowering. This helper delegates to the Lowerer's runtime methods and
//   existing RuntimeStatementLowerer while providing a unified interface.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/lower/detail/LowererDetail.hpp"
#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic::lower::detail
{

RuntimeLoweringHelper::RuntimeLoweringHelper(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

void RuntimeLoweringHelper::lowerLet(const LetStmt &stmt)
{
    lowerer_.lowerLet(stmt);
}

void RuntimeLoweringHelper::lowerConst(const ConstStmt &stmt)
{
    lowerer_.lowerConst(stmt);
}

void RuntimeLoweringHelper::lowerStatic(const StaticStmt &stmt)
{
    lowerer_.lowerStatic(stmt);
}

void RuntimeLoweringHelper::lowerDim(const DimStmt &stmt)
{
    lowerer_.lowerDim(stmt);
}

void RuntimeLoweringHelper::lowerReDim(const ReDimStmt &stmt)
{
    lowerer_.lowerReDim(stmt);
}

void RuntimeLoweringHelper::lowerRandomize(const RandomizeStmt &stmt)
{
    lowerer_.lowerRandomize(stmt);
}

void RuntimeLoweringHelper::lowerSwap(const SwapStmt &stmt)
{
    lowerer_.lowerSwap(stmt);
}

} // namespace il::frontends::basic::lower::detail
