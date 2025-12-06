//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/lower/detail/ControlLoweringHelper.cpp
//
// Summary:
//   Implements ControlLoweringHelper which coordinates control flow statement
//   lowering. This helper delegates to the Lowerer's control flow methods and
//   existing ControlStatementLowerer while providing a unified interface.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/detail/LowererDetail.hpp"

namespace il::frontends::basic::lower::detail
{

ControlLoweringHelper::ControlLoweringHelper(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

void ControlLoweringHelper::lowerIf(const IfStmt &stmt)
{
    lowerer_.lowerIf(stmt);
}

void ControlLoweringHelper::lowerWhile(const WhileStmt &stmt)
{
    lowerer_.lowerWhile(stmt);
}

void ControlLoweringHelper::lowerDo(const DoStmt &stmt)
{
    lowerer_.lowerDo(stmt);
}

void ControlLoweringHelper::lowerFor(const ForStmt &stmt)
{
    lowerer_.lowerFor(stmt);
}

void ControlLoweringHelper::lowerForEach(const ForEachStmt &stmt)
{
    lowerer_.lowerForEach(stmt);
}

void ControlLoweringHelper::lowerSelectCase(const SelectCaseStmt &stmt)
{
    lowerer_.lowerSelectCase(stmt);
}

void ControlLoweringHelper::lowerNext(const NextStmt &stmt)
{
    lowerer_.lowerNext(stmt);
}

void ControlLoweringHelper::lowerExit(const ExitStmt &stmt)
{
    lowerer_.lowerExit(stmt);
}

void ControlLoweringHelper::lowerGoto(const GotoStmt &stmt)
{
    lowerer_.lowerGoto(stmt);
}

void ControlLoweringHelper::lowerGosub(const GosubStmt &stmt)
{
    lowerer_.lowerGosub(stmt);
}

void ControlLoweringHelper::lowerGosubReturn(const ReturnStmt &stmt)
{
    lowerer_.lowerGosubReturn(stmt);
}

void ControlLoweringHelper::lowerOnErrorGoto(const OnErrorGoto &stmt)
{
    lowerer_.lowerOnErrorGoto(stmt);
}

void ControlLoweringHelper::lowerResume(const Resume &stmt)
{
    lowerer_.lowerResume(stmt);
}

void ControlLoweringHelper::lowerEnd(const EndStmt &stmt)
{
    lowerer_.lowerEnd(stmt);
}

void ControlLoweringHelper::lowerTryCatch(const TryCatchStmt &stmt)
{
    lowerer_.lowerTryCatch(stmt);
}

} // namespace il::frontends::basic::lower::detail
