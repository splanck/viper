//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/print/Print_Stmt_IO.cpp
// Purpose: Emit BASIC I/O statements for the AST printer.
// Whitespace invariants: Helpers mirror legacy spacing, ensuring prefixes,
//   separators, and channel markers appear exactly as before.
// Ownership/Lifetime: Context owns no state; statements are caller-owned.
// Notes: Channel formatting relies on PrintStyle conventions.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/print/Print_Stmt_Common.hpp"

namespace il::frontends::basic::print_stmt
{

void printPrint(const PrintStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(PRINT";
    for (const auto &item : stmt.items)
    {
        os << ' ';
        switch (item.kind)
        {
            case PrintItem::Kind::Expr:
                ctx.printExpr(*item.expr);
                break;
            case PrintItem::Kind::Comma:
                os << ',';
                break;
            case PrintItem::Kind::Semicolon:
                os << ';';
                break;
        }
    }
    os << ')';
}

void printPrintChannel(const PrintChStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    if (stmt.mode == PrintChStmt::Mode::Write)
    {
        os << "(WRITE#";
    }
    else
    {
        os << "(PRINT#";
    }
    ctx.style.writeChannelPrefix();
    ctx.printOptionalExpr(stmt.channelExpr.get());
    ctx.style.writeArgsPrefix();
    bool first = true;
    for (const auto &arg : stmt.args)
    {
        ctx.style.separate(first);
        if (arg)
        {
            ctx.printExpr(*arg);
        }
        else
        {
            ctx.style.writeNull();
        }
    }
    ctx.style.writeArgsSuffix();
    if (!stmt.trailingNewline)
    {
        ctx.style.writeNoNewlineTag();
    }
    os << ')';
}

void printOpen(const OpenStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(OPEN mode=" << openModeToString(stmt.mode) << '('
       << static_cast<int>(stmt.mode) << ") path=";
    ctx.printOptionalExpr(stmt.pathExpr.get());
    ctx.style.writeChannelPrefix();
    ctx.printOptionalExpr(stmt.channelExpr.get());
    os << ')';
}

void printClose(const CloseStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(CLOSE";
    ctx.style.writeChannelPrefix();
    ctx.printOptionalExpr(stmt.channelExpr.get());
    os << ')';
}

void printSeek(const SeekStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(SEEK";
    ctx.style.writeChannelPrefix();
    ctx.printOptionalExpr(stmt.channelExpr.get());
    os << " pos=";
    ctx.printOptionalExpr(stmt.positionExpr.get());
    os << ')';
}

void printInput(const InputStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(INPUT";
    bool firstItem = true;
    auto writeItemPrefix = [&] {
        if (firstItem)
        {
            os << ' ';
            firstItem = false;
        }
        else
        {
            os << ", ";
        }
    };
    if (stmt.prompt)
    {
        writeItemPrefix();
        ctx.printExpr(*stmt.prompt);
    }
    for (const auto &name : stmt.vars)
    {
        writeItemPrefix();
        os << name;
    }
    os << ')';
}

void printInputChannel(const InputChStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(INPUT#";
    ctx.style.writeChannelPrefix();
    os << stmt.channel;
    os << " target=" << stmt.target.name << ')';
}

void printLineInputChannel(const LineInputChStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(LINE-INPUT#";
    ctx.style.writeChannelPrefix();
    ctx.printOptionalExpr(stmt.channelExpr.get());
    os << " target=";
    if (stmt.targetVar)
    {
        ctx.printExpr(*stmt.targetVar);
    }
    else
    {
        ctx.style.writeNull();
    }
    os << ')';
}

} // namespace il::frontends::basic::print_stmt
