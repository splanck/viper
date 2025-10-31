//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC AST expression printer.  Each visitor method renders a
// distinct expression node into a stable, human-readable form that mirrors the
// surface syntax while remaining precise for debugging and golden tests.  The
// companion AstPrint_Stmt.cpp handles statement formatting.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include <array>
#include <sstream>

/// @file
/// @brief Implements the expression visitor used by the BASIC AST printer.
/// @details The visitor translates strongly typed AST nodes into formatted text
///          written to the printer stream supplied by @ref AstPrinter.  Each
///          visit method mirrors the surface syntax while remaining explicit
///          enough for diagnostics and golden tests.

namespace il::frontends::basic
{

namespace
{
} // namespace

/// @brief Visitor that renders expression nodes to the printer's stream.
struct AstPrinter::ExprPrinter final : ExprVisitor
{
    /// @brief Construct the visitor with a destination printer.
    /// @details Stores a reference to the shared printer so every visit method
    ///          can stream text directly.  The unused @p style argument keeps the
    ///          constructor signature aligned with the statement visitor and
    ///          preserves a hook for future formatting policies.
    ExprPrinter(Printer &printer, [[maybe_unused]] PrintStyle &style) : printer(printer) {}

    /// @brief Dispatch expression printing through the visitor interface.
    /// @details Delegates to @ref Expr::accept so the dynamic node type selects
    ///          the appropriate visit overload.
    void print(const Expr &expr)
    {
        expr.accept(*this);
    }

    /// @brief Fallback for unhandled expression types; triggers a build-time failure.
    /// @details The static assertion fires during compilation if a new AST node
    ///          lacks a visit overload, ensuring dumps stay in sync with the
    ///          language.
    template <typename NodeT> void visit([[maybe_unused]] const NodeT &)
    {
        static_assert(sizeof(NodeT) == 0, "Unhandled expression node in AstPrinter");
    }

    /// @brief Print an integer literal expression.
    /// @details Emits the literal digits exactly as stored so diagnostics mirror
    ///          the user's source text.
    void visit(const IntExpr &expr) override
    {
        printer.os << expr.value;
    }

    /// @brief Print a floating-point literal expression preserving precision.
    /// @details Streams through @c std::ostringstream to leverage standard
    ///          formatting rules, including scientific notation when needed while
    ///          remaining locale agnostic.
    void visit(const FloatExpr &expr) override
    {
        std::ostringstream os;
        os << expr.value;
        printer.os << os.str();
    }

    /// @brief Print a string literal with surrounding quotes.
    /// @details Writes the lexeme verbatim between double quotes.  Escapes are
    ///          already normalised by the parser.
    void visit(const StringExpr &expr) override
    {
        printer.os << '"' << expr.value << '"';
    }

    /// @brief Print a boolean literal as TRUE/FALSE tokens.
    /// @details BASIC traditionally surfaces uppercase TRUE/FALSE so the dump
    ///          follows suit.
    void visit(const BoolExpr &expr) override
    {
        printer.os << (expr.value ? "TRUE" : "FALSE");
    }

    /// @brief Print a variable reference by name.
    /// @details Emits the identifier without decoration so chained expressions
    ///          remain compact.
    void visit(const VarExpr &expr) override
    {
        printer.os << expr.name;
    }

    /// @brief Print an array element access with its index expression.
    /// @details Formats the reference as ``name(index)`` by recursively printing
    ///          the index expression inside parentheses.
    void visit(const ArrayExpr &expr) override
    {
        printer.os << expr.name << '(';
        expr.index->accept(*this);
        printer.os << ')';
    }

    /// @brief Print a unary expression with explicit operator tokens.
    /// @details Uses prefix notation (e.g., ``(NOT expr)``) to avoid ambiguity
    ///          when multiple unary operators are chained.
    void visit(const UnaryExpr &expr) override
    {
        printer.os << '(';
        switch (expr.op)
        {
            case UnaryExpr::Op::LogicalNot:
                printer.os << "NOT ";
                break;
            case UnaryExpr::Op::Plus:
                printer.os << "+ ";
                break;
            case UnaryExpr::Op::Negate:
                printer.os << "- ";
                break;
        }
        expr.expr->accept(*this);
        printer.os << ')';
    }

    /// @brief Print a binary expression using prefix notation.
    /// @details Uses a static operator table aligned with the enum to keep the
    ///          implementation data driven.  Prefix notation (``(+ lhs rhs)``)
    ///          makes evaluation order explicit.
    void visit(const BinaryExpr &expr) override
    {
        static constexpr std::array<const char *, 17> ops = {"+",
                                                             "-",
                                                             "*",
                                                             "/",
                                                             "^",
                                                             "\\",
                                                             "MOD",
                                                             "=",
                                                             "<>",
                                                             "<",
                                                             "<=",
                                                             ">",
                                                             ">=",
                                                             "ANDALSO",
                                                             "ORELSE",
                                                             "AND",
                                                             "OR"};
        printer.os << '(' << ops[static_cast<size_t>(expr.op)] << ' ';
        expr.lhs->accept(*this);
        printer.os << ' ';
        expr.rhs->accept(*this);
        printer.os << ')';
    }

    /// @brief Print a builtin call including the builtin mnemonic and arguments.
    /// @details Prepends the canonical builtin name obtained from
    ///          @ref getBuiltinInfo and then prints each argument recursively.
    void visit(const BuiltinCallExpr &expr) override
    {
        printer.os << '(' << getBuiltinInfo(expr.builtin).name;
        for (const auto &arg : expr.args)
        {
            printer.os << ' ';
            arg->accept(*this);
        }
        printer.os << ')';
    }

    /// @brief Print an LBOUND expression with its array operand.
    /// @details Emits ``(LBOUND name)`` to match the surface syntax while making
    ///          the operand explicit.
    void visit(const LBoundExpr &expr) override
    {
        printer.os << "(LBOUND " << expr.name << ')';
    }

    /// @brief Print a UBOUND expression with its array operand.
    /// @details Mirrors @ref visit(const LBoundExpr &) but uses the ``UBOUND``
    ///          mnemonic.
    void visit(const UBoundExpr &expr) override
    {
        printer.os << "(UBOUND " << expr.name << ')';
    }

    /// @brief Print a user-defined call expression with its argument list.
    /// @details Prints ``(callee arg1 arg2 ...)`` so nested calls remain
    ///          unambiguous and easy to diff.
    void visit(const CallExpr &expr) override
    {
        printer.os << '(' << expr.callee;
        for (const auto &arg : expr.args)
        {
            printer.os << ' ';
            arg->accept(*this);
        }
        printer.os << ')';
    }

    /// @brief Print an object construction expression.
    /// @details Formats as ``(NEW Class arg1 arg2 ...)`` to highlight both the
    ///          constructor target and arguments.
    void visit(const NewExpr &expr) override
    {
        printer.os << "(NEW " << expr.className;
        for (const auto &arg : expr.args)
        {
            printer.os << ' ';
            arg->accept(*this);
        }
        printer.os << ')';
    }

    /// @brief Print the ME receiver expression.
    /// @details Emits the ``ME`` keyword verbatim, matching BASIC's implicit
    ///          receiver semantics.
    void visit(const MeExpr &) override
    {
        printer.os << "ME";
    }

    /// @brief Print a member access expression as base.member.
    /// @details Encloses the access in parentheses to separate it from adjacent
    ///          syntax when nested inside larger expressions.
    void visit(const MemberAccessExpr &expr) override
    {
        printer.os << '(';
        expr.base->accept(*this);
        printer.os << '.' << expr.member << ')';
    }

    /// @brief Print a method invocation on an object instance.
    /// @details Uses ``(base.method arg1 ...)`` notation so both the receiver and
    ///          argument order remain explicit.
    void visit(const MethodCallExpr &expr) override
    {
        printer.os << '(';
        expr.base->accept(*this);
        printer.os << '.' << expr.method;
        for (const auto &arg : expr.args)
        {
            printer.os << ' ';
            arg->accept(*this);
        }
        printer.os << ')';
    }

  private:
    Printer &printer;
};

/// @brief Entry point used by AstPrinter to render an expression node.
/// @details Instantiates an @ref ExprPrinter bound to the caller-supplied
///          printer and forwards the expression for visitation.
void AstPrinter::printExpr(const Expr &expr, Printer &printer, PrintStyle &style)
{
    ExprPrinter exprPrinter{printer, style};
    exprPrinter.print(expr);
}

} // namespace il::frontends::basic
