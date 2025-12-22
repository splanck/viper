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

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/detail/LowererDetail.hpp"

namespace il::frontends::basic::lower::detail
{

RuntimeLoweringHelper::RuntimeLoweringHelper(Lowerer::DetailAccess access) noexcept
    : access_(access)
{
}

void RuntimeLoweringHelper::lowerLet(const LetStmt &stmt)
{
    access_.lowerLet(stmt);
}

void RuntimeLoweringHelper::lowerConst(const ConstStmt &stmt)
{
    access_.lowerConst(stmt);
}

void RuntimeLoweringHelper::lowerStatic(const StaticStmt &stmt)
{
    access_.lowerStatic(stmt);
}

void RuntimeLoweringHelper::lowerDim(const DimStmt &stmt)
{
    access_.lowerDim(stmt);
}

void RuntimeLoweringHelper::lowerReDim(const ReDimStmt &stmt)
{
    access_.lowerReDim(stmt);
}

void RuntimeLoweringHelper::lowerRandomize(const RandomizeStmt &stmt)
{
    access_.lowerRandomize(stmt);
}

void RuntimeLoweringHelper::lowerSwap(const SwapStmt &stmt)
{
    access_.lowerSwap(stmt);
}

} // namespace il::frontends::basic::lower::detail
