//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements statement printing for the BASIC AST printer.  The visitor in this
// file mirrors the surface BASIC syntax closely enough for debugging while
// remaining explicit about implicit behaviour (for example PRINT# channel
// handling).  Expression printing is defined in AstPrint_Expr.cpp.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AstPrinter.hpp"

#include "frontends/basic/print/Print_Stmt_Common.hpp"

namespace il::frontends::basic
{

namespace
{
using print_stmt::Context;
} // namespace

struct AstPrinter::StmtPrinter final : StmtVisitor
{
    StmtPrinter(Printer &printer, PrintStyle &style) : ctx{printer, style, *this} {}

    void print(const Stmt &stmt)
    {
        stmt.accept(*this);
    }

    void printExpr(const Expr &expr)
    {
        AstPrinter::printExpr(expr, ctx.printer, ctx.style);
    }

    template <typename NodeT> void visit([[maybe_unused]] const NodeT &)
    {
        static_assert(sizeof(NodeT) == 0, "Unhandled statement node in AstPrinter");
    }

    void visit(const LabelStmt &) override
    {
        ctx.stream() << "(LABEL)";
    }

    void visit(const PrintStmt &stmt) override
    {
        print_stmt::printPrint(stmt, ctx);
    }

    void visit(const PrintChStmt &stmt) override
    {
        print_stmt::printPrintChannel(stmt, ctx);
    }

    void visit(const CallStmt &stmt) override
    {
        ctx.stream() << "(CALL";
        if (stmt.call)
        {
            ctx.stream() << ' ';
            ctx.printExpr(*stmt.call);
        }
        ctx.stream() << ')';
    }

    void visit(const ClsStmt &) override
    {
        ctx.stream() << "(CLS)";
    }

    void visit(const ColorStmt &stmt) override
    {
        ctx.stream() << "(COLOR ";
        ctx.printOptionalExpr(stmt.fg.get());
        ctx.stream() << ' ';
        ctx.printOptionalExpr(stmt.bg.get());
        ctx.stream() << ')';
    }

    void visit(const LocateStmt &stmt) override
    {
        ctx.stream() << "(LOCATE ";
        ctx.printOptionalExpr(stmt.row.get());
        if (stmt.col)
        {
            ctx.stream() << ' ';
            ctx.printExpr(*stmt.col);
        }
        ctx.stream() << ')';
    }

    void visit(const LetStmt &stmt) override
    {
        print_stmt::printLet(stmt, ctx);
    }

    void visit(const DimStmt &stmt) override
    {
        print_stmt::printDim(stmt, ctx);
    }

    void visit(const ReDimStmt &stmt) override
    {
        print_stmt::printReDim(stmt, ctx);
    }

    void visit(const RandomizeStmt &stmt) override
    {
        ctx.stream() << "(RANDOMIZE ";
        ctx.printExpr(*stmt.seed);
        ctx.stream() << ')';
    }

    void visit(const IfStmt &stmt) override
    {
        print_stmt::printIf(stmt, ctx);
    }

    void visit(const SelectCaseStmt &stmt) override
    {
        print_stmt::printSelectCase(stmt, ctx);
    }

    void visit(const WhileStmt &stmt) override
    {
        print_stmt::printWhile(stmt, ctx);
    }

    void visit(const DoStmt &stmt) override
    {
        print_stmt::printDo(stmt, ctx);
    }

    void visit(const ForStmt &stmt) override
    {
        print_stmt::printFor(stmt, ctx);
    }

    void visit(const NextStmt &stmt) override
    {
        print_stmt::printNext(stmt, ctx);
    }

    void visit(const ExitStmt &stmt) override
    {
        print_stmt::printExit(stmt, ctx);
    }

    void visit(const GotoStmt &stmt) override
    {
        print_stmt::printGoto(stmt, ctx);
    }

    void visit(const GosubStmt &stmt) override
    {
        print_stmt::printGosub(stmt, ctx);
    }

    void visit(const OpenStmt &stmt) override
    {
        print_stmt::printOpen(stmt, ctx);
    }

    void visit(const CloseStmt &stmt) override
    {
        print_stmt::printClose(stmt, ctx);
    }

    void visit(const SeekStmt &stmt) override
    {
        print_stmt::printSeek(stmt, ctx);
    }

    void visit(const OnErrorGoto &stmt) override
    {
        print_stmt::printOnErrorGoto(stmt, ctx);
    }

    void visit(const Resume &stmt) override
    {
        print_stmt::printResume(stmt, ctx);
    }

    void visit(const EndStmt &) override
    {
        ctx.stream() << "(END)";
    }

    void visit(const InputStmt &stmt) override
    {
        print_stmt::printInput(stmt, ctx);
    }

    void visit(const InputChStmt &stmt) override
    {
        print_stmt::printInputChannel(stmt, ctx);
    }

    void visit(const LineInputChStmt &stmt) override
    {
        print_stmt::printLineInputChannel(stmt, ctx);
    }

    void visit(const ReturnStmt &stmt) override
    {
        print_stmt::printReturn(stmt, ctx);
    }

    void visit(const FunctionDecl &stmt) override
    {
        print_stmt::printFunction(stmt, ctx);
    }

    void visit(const SubDecl &stmt) override
    {
        print_stmt::printSub(stmt, ctx);
    }

    void visit(const StmtList &stmt) override
    {
        ctx.stream() << "(SEQ";
        for (const auto &subStmt : stmt.stmts)
        {
            ctx.stream() << ' ';
            ctx.printStmt(*subStmt);
        }
        ctx.stream() << ')';
    }

    void visit(const DeleteStmt &stmt) override
    {
        print_stmt::printDelete(stmt, ctx);
    }

    void visit(const ConstructorDecl &stmt) override
    {
        print_stmt::printConstructor(stmt, ctx);
    }

    void visit(const DestructorDecl &stmt) override
    {
        print_stmt::printDestructor(stmt, ctx);
    }

    void visit(const MethodDecl &stmt) override
    {
        print_stmt::printMethod(stmt, ctx);
    }

    void visit(const ClassDecl &stmt) override
    {
        print_stmt::printClass(stmt, ctx);
    }

    void visit(const TypeDecl &stmt) override
    {
        print_stmt::printType(stmt, ctx);
    }

    Context ctx;
};

void AstPrinter::printStmt(const Stmt &stmt, Printer &printer, PrintStyle &style)
{
    StmtPrinter stmtPrinter{printer, style};
    stmtPrinter.print(stmt);
}

} // namespace il::frontends::basic

namespace il::frontends::basic::print_stmt
{

void Context::printExpr(const Expr &expr) const
{
    dispatcher.printExpr(expr);
}

void Context::printOptionalExpr(const Expr *expr) const
{
    if (expr)
    {
        printExpr(*expr);
    }
    else
    {
        style.writeNull();
    }
}

void Context::printStmt(const Stmt &stmt) const
{
    dispatcher.print(stmt);
}

void Context::printNumberedBody(const std::vector<std::unique_ptr<Stmt>> &body) const
{
    style.openBody();
    bool first = true;
    for (const auto &bodyStmt : body)
    {
        style.separate(first);
        style.writeLineNumber(bodyStmt->line);
        dispatcher.print(*bodyStmt);
    }
    style.closeBody();
}

} // namespace il::frontends::basic::print_stmt
