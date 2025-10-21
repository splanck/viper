//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/AstPrint_Stmt.cpp
// Purpose: Emit BASIC statements in a debug-friendly S-expression format.
// Key invariants: Printing never mutates the AST and honours implicit BASIC
//                 behaviours such as PRINT# channel handling.
// Ownership/Lifetime: Borrowed Printer and style instances; no persistent
//                     allocations occur.
// Links: docs/codemap.md, docs/basic-language.md
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

/// @brief Statement visitor that serialises BASIC statements for debugging.
///
/// @details The printer walks the AST using the shared @ref StmtVisitor
///          interface.  Each override prints a S-expression token sequence to
///          the configured @ref Printer stream, delegating nested constructs to
///          `print_stmt` helpers or the expression printer where appropriate.
struct AstPrinter::StmtPrinter final : StmtVisitor
{
    /// @brief Construct a printer that writes into @p printer using @p style.
    ///
    /// @param printer Destination object receiving formatted tokens.
    /// @param style Mutable print style shared with expression printers.
    StmtPrinter(Printer &printer, PrintStyle &style) : ctx{printer, style, *this} {}

    /// @brief Print a single statement by dispatching to the visitor override.
    ///
    /// @param stmt Statement node to serialise.
    void print(const Stmt &stmt)
    {
        stmt.accept(*this);
    }

    /// @brief Print an expression using the shared expression printer entry.
    ///
    /// @param expr Expression to serialise.
    void printExpr(const Expr &expr)
    {
        AstPrinter::printExpr(expr, ctx.printer, ctx.style);
    }

    /// @brief Catch unhandled statement node types during development builds.
    ///
    /// @tparam NodeT Statement type that lacked a dedicated override.
    /// @param node Unused parameter; only its type participates in static_assert.
    template <typename NodeT> void visit([[maybe_unused]] const NodeT &)
    {
        static_assert(sizeof(NodeT) == 0, "Unhandled statement node in AstPrinter");
    }

    /// @brief Render a numeric label statement.
    ///
    /// @param stmt Label node (unused because labels carry no payload).
    void visit(const LabelStmt &) override
    {
        ctx.stream() << "(LABEL)";
    }

    /// @brief Render a PRINT statement using helper formatting.
    ///
    /// @param stmt Statement describing the PRINT operands.
    void visit(const PrintStmt &stmt) override
    {
        print_stmt::printPrint(stmt, ctx);
    }

    /// @brief Render a PRINT# channel statement.
    ///
    /// @param stmt Statement containing the channel and item list.
    void visit(const PrintChStmt &stmt) override
    {
        print_stmt::printPrintChannel(stmt, ctx);
    }

    /// @brief Render a CALL statement with optional call expression.
    ///
    /// @param stmt Statement referencing the procedure invocation.
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

    /// @brief Render the CLS statement as a bare token.
    ///
    /// @param stmt CLS statement (payload unused).
    void visit(const ClsStmt &) override
    {
        ctx.stream() << "(CLS)";
    }

    /// @brief Render a COLOR statement showing optional foreground/background.
    ///
    /// @param stmt Statement referencing the target palette indices.
    void visit(const ColorStmt &stmt) override
    {
        ctx.stream() << "(COLOR ";
        ctx.printOptionalExpr(stmt.fg.get());
        ctx.stream() << ' ';
        ctx.printOptionalExpr(stmt.bg.get());
        ctx.stream() << ')';
    }

    /// @brief Render a LOCATE statement with optional coordinates.
    ///
    /// @param stmt Statement describing the cursor positioning request.
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

    /// @brief Render a LET assignment statement via helper utilities.
    ///
    /// @param stmt Assignment data, including target and expression.
    void visit(const LetStmt &stmt) override
    {
        print_stmt::printLet(stmt, ctx);
    }

    /// @brief Render a DIM declaration.
    ///
    /// @param stmt Declaration describing scalar or array binding.
    void visit(const DimStmt &stmt) override
    {
        print_stmt::printDim(stmt, ctx);
    }

    /// @brief Render a REDIM statement for resizing arrays.
    ///
    /// @param stmt Resizing directive paired with bounds.
    void visit(const ReDimStmt &stmt) override
    {
        print_stmt::printReDim(stmt, ctx);
    }

    /// @brief Render a RANDOMIZE call including the seed expression.
    ///
    /// @param stmt Randomize statement carrying an optional seed.
    void visit(const RandomizeStmt &stmt) override
    {
        ctx.stream() << "(RANDOMIZE ";
        ctx.printExpr(*stmt.seed);
        ctx.stream() << ')';
    }

    /// @brief Render an IF/THEN[/ELSE] construct.
    ///
    /// @param stmt High-level IF statement node.
    void visit(const IfStmt &stmt) override
    {
        print_stmt::printIf(stmt, ctx);
    }

    /// @brief Render a SELECT CASE construct and its arms.
    ///
    /// @param stmt Select Case statement definition.
    void visit(const SelectCaseStmt &stmt) override
    {
        print_stmt::printSelectCase(stmt, ctx);
    }

    /// @brief Render a WHILE loop.
    ///
    /// @param stmt While statement with condition and body.
    void visit(const WhileStmt &stmt) override
    {
        print_stmt::printWhile(stmt, ctx);
    }

    /// @brief Render a DO loop with exit conditions.
    ///
    /// @param stmt DO statement capturing loop semantics.
    void visit(const DoStmt &stmt) override
    {
        print_stmt::printDo(stmt, ctx);
    }

    /// @brief Render a FOR loop including iterator metadata.
    ///
    /// @param stmt FOR statement with start, end, and step.
    void visit(const ForStmt &stmt) override
    {
        print_stmt::printFor(stmt, ctx);
    }

    /// @brief Render the NEXT statement referencing loop variables.
    ///
    /// @param stmt NEXT statement containing iterators.
    void visit(const NextStmt &stmt) override
    {
        print_stmt::printNext(stmt, ctx);
    }

    /// @brief Render EXIT statements (FOR, DO, etc.).
    ///
    /// @param stmt EXIT statement including target loop kind.
    void visit(const ExitStmt &stmt) override
    {
        print_stmt::printExit(stmt, ctx);
    }

    /// @brief Render a GOTO jump to a label.
    ///
    /// @param stmt Statement referencing the destination label.
    void visit(const GotoStmt &stmt) override
    {
        print_stmt::printGoto(stmt, ctx);
    }

    /// @brief Render a GOSUB invocation.
    ///
    /// @param stmt Statement referencing the subroutine label.
    void visit(const GosubStmt &stmt) override
    {
        print_stmt::printGosub(stmt, ctx);
    }

    /// @brief Render an OPEN statement configuring file channels.
    ///
    /// @param stmt Statement detailing the path, channel, and mode.
    void visit(const OpenStmt &stmt) override
    {
        print_stmt::printOpen(stmt, ctx);
    }

    /// @brief Render a CLOSE statement closing a channel.
    ///
    /// @param stmt Statement listing the channel to close.
    void visit(const CloseStmt &stmt) override
    {
        print_stmt::printClose(stmt, ctx);
    }

    /// @brief Render a SEEK statement for file positioning.
    ///
    /// @param stmt Statement describing the channel and offset.
    void visit(const SeekStmt &stmt) override
    {
        print_stmt::printSeek(stmt, ctx);
    }

    /// @brief Render an ON ERROR GOTO handler installation.
    ///
    /// @param stmt Statement referencing the error target label.
    void visit(const OnErrorGoto &stmt) override
    {
        print_stmt::printOnErrorGoto(stmt, ctx);
    }

    /// @brief Render a RESUME statement for error recovery.
    ///
    /// @param stmt Resume form (NEXT/label) and associated data.
    void visit(const Resume &stmt) override
    {
        print_stmt::printResume(stmt, ctx);
    }

    /// @brief Render the END statement that terminates execution.
    ///
    /// @param stmt END statement (payload unused).
    void visit(const EndStmt &) override
    {
        ctx.stream() << "(END)";
    }

    /// @brief Render an INPUT statement reading from stdin.
    ///
    /// @param stmt Statement listing prompt and variables.
    void visit(const InputStmt &stmt) override
    {
        print_stmt::printInput(stmt, ctx);
    }

    /// @brief Render an INPUT# channel statement.
    ///
    /// @param stmt Statement containing channel and destination vars.
    void visit(const InputChStmt &stmt) override
    {
        print_stmt::printInputChannel(stmt, ctx);
    }

    /// @brief Render a LINE INPUT# channel statement.
    ///
    /// @param stmt Statement specifying the target variable and channel.
    void visit(const LineInputChStmt &stmt) override
    {
        print_stmt::printLineInputChannel(stmt, ctx);
    }

    /// @brief Render a RETURN statement (GOSUB return or function exit).
    ///
    /// @param stmt Return statement capturing mode and value.
    void visit(const ReturnStmt &stmt) override
    {
        print_stmt::printReturn(stmt, ctx);
    }

    /// @brief Render a FUNCTION declaration header and body summary.
    ///
    /// @param stmt Function declaration to serialise.
    void visit(const FunctionDecl &stmt) override
    {
        print_stmt::printFunction(stmt, ctx);
    }

    /// @brief Render a SUB declaration header and body summary.
    ///
    /// @param stmt Subroutine declaration to serialise.
    void visit(const SubDecl &stmt) override
    {
        print_stmt::printSub(stmt, ctx);
    }

    /// @brief Render a sequence statement by printing each member.
    ///
    /// @param stmt Statement list containing ordered child statements.
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

    /// @brief Render a DELETE statement targeting object fields.
    ///
    /// @param stmt Delete statement referencing the target expression.
    void visit(const DeleteStmt &stmt) override
    {
        print_stmt::printDelete(stmt, ctx);
    }

    /// @brief Render a CLASS constructor declaration summary.
    ///
    /// @param stmt Constructor declaration (printed for debugging only).
    void visit(const ConstructorDecl &stmt) override
    {
        print_stmt::printConstructor(stmt, ctx);
    }

    /// @brief Render a CLASS destructor declaration summary.
    ///
    /// @param stmt Destructor declaration for the AST printer.
    void visit(const DestructorDecl &stmt) override
    {
        print_stmt::printDestructor(stmt, ctx);
    }

    /// @brief Render a CLASS method declaration summary.
    ///
    /// @param stmt Method declaration captured by the AST printer.
    void visit(const MethodDecl &stmt) override
    {
        print_stmt::printMethod(stmt, ctx);
    }

    /// @brief Render a CLASS declaration including members.
    ///
    /// @param stmt Class declaration node to print.
    void visit(const ClassDecl &stmt) override
    {
        print_stmt::printClass(stmt, ctx);
    }

    /// @brief Render a TYPE declaration summarising fields.
    ///
    /// @param stmt User-defined type declaration to serialise.
    void visit(const TypeDecl &stmt) override
    {
        print_stmt::printType(stmt, ctx);
    }

    Context ctx;
};

/// @brief Entry point used by tooling to print a statement with style control.
///
/// @param stmt Statement to format.
/// @param printer Destination printer receiving the token stream.
/// @param style Print style configuring indentation and quoting rules.
void AstPrinter::printStmt(const Stmt &stmt, Printer &printer, PrintStyle &style)
{
    StmtPrinter stmtPrinter{printer, style};
    stmtPrinter.print(stmt);
}

} // namespace il::frontends::basic

namespace il::frontends::basic::print_stmt
{

/// @brief Print an expression using the AST printer dispatcher.
///
/// @param expr Expression node to serialise.
void Context::printExpr(const Expr &expr) const
{
    dispatcher.printExpr(expr);
}

/// @brief Print an optional expression, emitting null markers when absent.
///
/// @param expr Optional expression pointer supplied by the caller.
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

/// @brief Print a statement using the nested dispatcher instance.
///
/// @param stmt Statement to serialise.
void Context::printStmt(const Stmt &stmt) const
{
    dispatcher.print(stmt);
}

/// @brief Print a body of numbered statements such as SELECT arms.
///
/// @param body Ordered list of statements with BASIC line numbers.
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
