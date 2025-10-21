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

/// @brief Render an IF/ELSEIF/ELSE control-flow statement.
///
/// @details Serialises the condition and bodies using the provided context so
///          indentation and expression formatting honour the caller's style.
///          Each branch is emitted in the canonical `(IF ... THEN ...)` form the
///          textual dumper expects.
///
/// @param stmt IF statement node to print.
/// @param ctx  Printer context supplying output stream and recursion helpers.
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

/// @brief Render a SELECT CASE statement including each arm and optional else.
///
/// @details Emits the selector expression (or a null token when absent),
///          serialises each labelled arm using `Context::printNumberedBody`, and
///          prints the final ELSE body when provided.  Labels are written as a
///          flat list so downstream tests can pattern-match easily.
///
/// @param stmt SELECT CASE AST node to print.
/// @param ctx  Printer context used for recursive printing.
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

/// @brief Render a WHILE loop and its body.
///
/// @details Prints the loop condition followed by the numbered body to preserve
///          source line associations.  The representation follows the
///          `(WHILE <cond> ...)` pattern used across the BASIC printer.
///
/// @param stmt WHILE statement node.
/// @param ctx  Printer context used for nested output.
void printWhile(const WhileStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(WHILE ";
    ctx.printExpr(*stmt.cond);
    ctx.printNumberedBody(stmt.body);
}

/// @brief Render a DO loop including test position and condition kind.
///
/// @details Encodes whether the test occurs at the start or end of the loop,
///          prints the condition kind (`NONE`, `WHILE`, or `UNTIL`), and emits
///          the optional condition expression before the loop body.  The helper
///          keeps the textual format stable for golden tests.
///
/// @param stmt DO statement node describing the loop.
/// @param ctx  Printer context for recursive emission.
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

/// @brief Render a FOR loop with optional STEP expression.
///
/// @details Prints the loop variable, start and end expressions, and includes a
///          STEP clause when present.  The loop body is emitted using the
///          numbered-body helper to maintain indentation and statement numbers.
///
/// @param stmt FOR loop node.
/// @param ctx  Printer context used for nested statements.
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

/// @brief Render a NEXT statement that advances a FOR loop variable.
///
/// @details Outputs the canonical `(NEXT <var>)` form recognised by the
///          round-trip tests.  The helper does not include additional whitespace
///          or formatting logic beyond the statement variable.
///
/// @param stmt NEXT statement node.
/// @param ctx  Printer context providing the output stream.
void printNext(const NextStmt &stmt, Context &ctx)
{
    ctx.stream() << "(NEXT " << stmt.var << ')';
}

/// @brief Render an EXIT statement targeting a specific loop kind.
///
/// @details Encodes the target loop as `FOR`, `WHILE`, or `DO` to mirror the
///          surface syntax.  No trailing whitespace is emitted so the printer's
///          output remains deterministic.
///
/// @param stmt EXIT statement node.
/// @param ctx  Printer context providing access to the output stream.
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
