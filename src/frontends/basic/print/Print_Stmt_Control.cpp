//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/print/Print_Stmt_Control.cpp
// Purpose: Emit BASIC control-flow statements for the AST printer.
// Whitespace invariants: Keywords are separated by single spaces and bodies are
//   serialized via Context::printNumberedBody to preserve indentation and
//   spacing guarantees.
// Ownership/Lifetime: Context and statement nodes are owned by the caller.
// Notes: Helpers rely on Context to handle recursive printing.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/print/Print_Stmt_Common.hpp"

namespace il::frontends::basic::print_stmt
{

void printIf(const IfStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(IF ";
    ctx.printExpr(*stmt.cond);
    os << " THEN ";
    ctx.printStmt(*stmt.then_branch);
    for (const auto &elseif : stmt.elseifs)
    {
        os << " ELSEIF ";
        ctx.printExpr(*elseif.cond);
        os << " THEN ";
        ctx.printStmt(*elseif.then_branch);
    }
    if (stmt.else_branch)
    {
        os << " ELSE ";
        ctx.printStmt(*stmt.else_branch);
    }
    os << ')';
}

void printSelectCase(const SelectCaseStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(SELECT CASE ";
    if (stmt.selector)
    {
        ctx.printExpr(*stmt.selector);
    }
    else
    {
        ctx.style.writeNull();
    }
    for (const auto &arm : stmt.arms)
    {
        os << " (CASE";
        for (auto label : arm.labels)
        {
            os << ' ' << label;
        }
        os << ')';
        ctx.printNumberedBody(arm.body);
    }
    if (!stmt.elseBody.empty())
    {
        os << " (CASE ELSE)";
        ctx.printNumberedBody(stmt.elseBody);
    }
    os << ')';
}

void printWhile(const WhileStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(WHILE ";
    ctx.printExpr(*stmt.cond);
    ctx.printNumberedBody(stmt.body);
}

void printDo(const DoStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(DO " << (stmt.testPos == DoStmt::TestPos::Pre ? "pre" : "post") << ' ';
    switch (stmt.condKind)
    {
        case DoStmt::CondKind::None:
            os << "NONE";
            break;
        case DoStmt::CondKind::While:
            os << "WHILE";
            break;
        case DoStmt::CondKind::Until:
            os << "UNTIL";
            break;
    }
    if (stmt.condKind != DoStmt::CondKind::None && stmt.cond)
    {
        os << ' ';
        ctx.printExpr(*stmt.cond);
    }
    ctx.printNumberedBody(stmt.body);
}

void printFor(const ForStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(FOR " << stmt.var << " = ";
    ctx.printExpr(*stmt.start);
    os << " TO ";
    ctx.printExpr(*stmt.end);
    if (stmt.step)
    {
        os << " STEP ";
        ctx.printExpr(*stmt.step);
    }
    ctx.printNumberedBody(stmt.body);
}

void printNext(const NextStmt &stmt, Context &ctx)
{
    ctx.stream() << "(NEXT " << stmt.var << ')';
}

void printExit(const ExitStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(EXIT ";
    switch (stmt.kind)
    {
        case ExitStmt::LoopKind::For:
            os << "FOR";
            break;
        case ExitStmt::LoopKind::While:
            os << "WHILE";
            break;
        case ExitStmt::LoopKind::Do:
            os << "DO";
            break;
    }
    os << ')';
}

} // namespace il::frontends::basic::print_stmt
