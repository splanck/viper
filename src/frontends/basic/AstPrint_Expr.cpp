// File: src/frontends/basic/AstPrint_Expr.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements expression printing logic for the BASIC AST printer.
// Key invariants: All expression node kinds must be handled.
// Ownership/Lifetime: Operates on non-owning AST references.
// Notes: Uses BuiltinRegistry metadata for builtin call names.
// Links: docs/codemap.md

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include <array>
#include <sstream>

namespace il::frontends::basic
{

namespace
{
} // namespace

/// @brief Visitor that renders expression nodes to the printer's stream.
struct AstPrinter::ExprPrinter final : ExprVisitor
{
    /// @brief Construct the visitor with a destination printer.
    ExprPrinter(Printer &printer, [[maybe_unused]] PrintStyle &style) : printer(printer) {}

    /// @brief Dispatch expression printing through the visitor interface.
    void print(const Expr &expr)
    {
        expr.accept(*this);
    }

    /// @brief Fallback for unhandled expression types; triggers a build-time failure.
    template <typename NodeT> void visit([[maybe_unused]] const NodeT &)
    {
        static_assert(sizeof(NodeT) == 0, "Unhandled expression node in AstPrinter");
    }

    /// @brief Print an integer literal expression.
    void visit(const IntExpr &expr) override
    {
        printer.os << expr.value;
    }

    /// @brief Print a floating-point literal expression preserving precision.
    void visit(const FloatExpr &expr) override
    {
        std::ostringstream os;
        os << expr.value;
        printer.os << os.str();
    }

    /// @brief Print a string literal with surrounding quotes.
    void visit(const StringExpr &expr) override
    {
        printer.os << '"' << expr.value << '"';
    }

    /// @brief Print a boolean literal as TRUE/FALSE tokens.
    void visit(const BoolExpr &expr) override
    {
        printer.os << (expr.value ? "TRUE" : "FALSE");
    }

    /// @brief Print a variable reference by name.
    void visit(const VarExpr &expr) override
    {
        printer.os << expr.name;
    }

    /// @brief Print an array element access with its index expression.
    void visit(const ArrayExpr &expr) override
    {
        printer.os << expr.name << '(';
        expr.index->accept(*this);
        printer.os << ')';
    }

    /// @brief Print a unary expression with explicit operator tokens.
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
    void visit(const LBoundExpr &expr) override
    {
        printer.os << "(LBOUND " << expr.name << ')';
    }

    /// @brief Print a UBOUND expression with its array operand.
    void visit(const UBoundExpr &expr) override
    {
        printer.os << "(UBOUND " << expr.name << ')';
    }

    /// @brief Print a user-defined call expression with its argument list.
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

  private:
    Printer &printer;
};

/// @brief Entry point used by AstPrinter to render an expression node.
void AstPrinter::printExpr(const Expr &expr, Printer &printer, PrintStyle &style)
{
    ExprPrinter exprPrinter{printer, style};
    exprPrinter.print(expr);
}

} // namespace il::frontends::basic

