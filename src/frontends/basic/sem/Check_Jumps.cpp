//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Semantic checks for jump-oriented constructs (GOTO, GOSUB, ON ERROR,
///        RESUME, RETURN).
/// @details Ensures label references resolve, manages error-handler state, and
///          enforces RETURN usage constraints.
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/Check_Common.hpp"

#include <string>

namespace il::frontends::basic::sem
{

namespace
{
void emitUnknownLabel(ControlCheckContext &context,
                      int label,
                      const il::support::SourceLoc &loc,
                      uint32_t width)
{
    std::string msg = "unknown line ";
    msg += std::to_string(label);
    context.diagnostics().emit(il::support::Severity::Error,
                               "B1003",
                               loc,
                               width,
                               std::move(msg));
}
}

void analyzeGoto(SemanticAnalyzer &analyzer, const GotoStmt &stmt)
{
    ControlCheckContext context(analyzer);
    context.insertLabelReference(stmt.target);
    if (!context.hasKnownLabel(stmt.target))
        emitUnknownLabel(context, stmt.target, stmt.loc, 4);
}

void analyzeGosub(SemanticAnalyzer &analyzer, const GosubStmt &stmt)
{
    ControlCheckContext context(analyzer);
    context.insertLabelReference(stmt.targetLine);
    if (!context.hasKnownLabel(stmt.targetLine))
        emitUnknownLabel(context, stmt.targetLine, stmt.loc, 5);
}

void analyzeOnErrorGoto(SemanticAnalyzer &analyzer, const OnErrorGoto &stmt)
{
    ControlCheckContext context(analyzer);
    if (stmt.toZero)
    {
        context.clearErrorHandler();
        return;
    }

    context.insertLabelReference(stmt.target);
    if (!context.hasKnownLabel(stmt.target))
        emitUnknownLabel(context, stmt.target, stmt.loc, 4);

    context.installErrorHandler(stmt.target);
}

void analyzeResume(SemanticAnalyzer &analyzer, const Resume &stmt)
{
    ControlCheckContext context(analyzer);
    if (!context.hasActiveErrorHandler())
    {
        std::string msg = "RESUME requires an active error handler";
        context.diagnostics().emit(il::support::Severity::Error,
                                   "B1012",
                                   stmt.loc,
                                   6,
                                   std::move(msg));
    }

    if (stmt.mode != Resume::Mode::Label)
        return;

    context.insertLabelReference(stmt.target);
    if (!context.hasKnownLabel(stmt.target))
        emitUnknownLabel(context, stmt.target, stmt.loc, 4);
}

void analyzeReturn(SemanticAnalyzer &analyzer, ReturnStmt &stmt)
{
    ControlCheckContext context(analyzer);
    if (!context.hasActiveProcScope())
    {
        if (stmt.value)
        {
            std::string msg = "RETURN with value not allowed at top level";
            context.diagnostics().emit(il::support::Severity::Error,
                                       "B1008",
                                       stmt.loc,
                                       6,
                                       std::move(msg));
        }
        else
        {
            stmt.isGosubReturn = true;
        }
    }

    if (context.hasActiveErrorHandler())
        context.clearErrorHandler();
}

} // namespace il::frontends::basic::sem
