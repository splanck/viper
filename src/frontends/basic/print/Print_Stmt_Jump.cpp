//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/print/Print_Stmt_Jump.cpp
// Purpose: Emit BASIC jump and error-handling statements for the AST printer.
// Whitespace invariants: Output preserves original spacing contracts, only
//   writing spaces when required by the BASIC syntax tokens themselves.
// Ownership/Lifetime: Context and statement nodes remain owned by the caller.
// Notes: Helpers rely on Context to delegate nested printing.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/print/Print_Stmt_Common.hpp"

namespace il::frontends::basic::print_stmt
{

void printGoto(const GotoStmt &stmt, Context &ctx)
{
    ctx.stream() << "(GOTO " << stmt.target << ')';
}

void printGosub(const GosubStmt &stmt, Context &ctx)
{
    ctx.stream() << "(GOSUB " << stmt.targetLine << ')';
}

void printReturn(const ReturnStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(RETURN";
    if (stmt.isGosubReturn)
    {
        os << " GOSUB";
    }
    if (stmt.value)
    {
        os << ' ';
        ctx.printExpr(*stmt.value);
    }
    os << ')';
}

void printOnErrorGoto(const OnErrorGoto &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(ON-ERROR GOTO ";
    if (stmt.toZero)
    {
        os << '0';
    }
    else
    {
        os << stmt.target;
    }
    os << ')';
}

void printResume(const Resume &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(RESUME";
    switch (stmt.mode)
    {
        case Resume::Mode::Same:
            break;
        case Resume::Mode::Next:
            os << " NEXT";
            break;
        case Resume::Mode::Label:
            os << ' ' << stmt.target;
            break;
    }
    os << ')';
}

} // namespace il::frontends::basic::print_stmt
