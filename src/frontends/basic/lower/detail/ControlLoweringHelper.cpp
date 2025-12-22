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

ControlLoweringHelper::ControlLoweringHelper(Lowerer::DetailAccess access) noexcept
    : access_(access)
{
}

void ControlLoweringHelper::lowerIf(const IfStmt &stmt)
{
    access_.lowerIf(stmt);
}

void ControlLoweringHelper::lowerWhile(const WhileStmt &stmt)
{
    access_.lowerWhile(stmt);
}

void ControlLoweringHelper::lowerDo(const DoStmt &stmt)
{
    access_.lowerDo(stmt);
}

void ControlLoweringHelper::lowerFor(const ForStmt &stmt)
{
    access_.lowerFor(stmt);
}

void ControlLoweringHelper::lowerForEach(const ForEachStmt &stmt)
{
    access_.lowerForEach(stmt);
}

void ControlLoweringHelper::lowerSelectCase(const SelectCaseStmt &stmt)
{
    access_.lowerSelectCase(stmt);
}

void ControlLoweringHelper::lowerNext(const NextStmt &stmt)
{
    access_.lowerNext(stmt);
}

void ControlLoweringHelper::lowerExit(const ExitStmt &stmt)
{
    access_.lowerExit(stmt);
}

void ControlLoweringHelper::lowerGoto(const GotoStmt &stmt)
{
    access_.lowerGoto(stmt);
}

void ControlLoweringHelper::lowerGosub(const GosubStmt &stmt)
{
    access_.lowerGosub(stmt);
}

void ControlLoweringHelper::lowerGosubReturn(const ReturnStmt &stmt)
{
    access_.lowerGosubReturn(stmt);
}

void ControlLoweringHelper::lowerOnErrorGoto(const OnErrorGoto &stmt)
{
    access_.lowerOnErrorGoto(stmt);
}

void ControlLoweringHelper::lowerResume(const Resume &stmt)
{
    access_.lowerResume(stmt);
}

void ControlLoweringHelper::lowerEnd(const EndStmt &stmt)
{
    access_.lowerEnd(stmt);
}

void ControlLoweringHelper::lowerTryCatch(const TryCatchStmt &stmt)
{
    access_.lowerTryCatch(stmt);
}

} // namespace il::frontends::basic::lower::detail
