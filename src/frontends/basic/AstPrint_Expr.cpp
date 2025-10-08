// File: src/frontends/basic/AstPrint_Expr.cpp
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

struct AstPrinter::ExprPrinter final : ExprVisitor
{
    ExprPrinter(Printer &printer, [[maybe_unused]] PrintStyle &style) : printer(printer) {}

    void print(const Expr &expr)
    {
        expr.accept(*this);
    }

    template <typename NodeT> void visit([[maybe_unused]] const NodeT &)
    {
        static_assert(sizeof(NodeT) == 0, "Unhandled expression node in AstPrinter");
    }

    void visit(const IntExpr &expr) override
    {
        printer.os << expr.value;
    }

    void visit(const FloatExpr &expr) override
    {
        std::ostringstream os;
        os << expr.value;
        printer.os << os.str();
    }

    void visit(const StringExpr &expr) override
    {
        printer.os << '"' << expr.value << '"';
    }

    void visit(const BoolExpr &expr) override
    {
        printer.os << (expr.value ? "TRUE" : "FALSE");
    }

    void visit(const VarExpr &expr) override
    {
        printer.os << expr.name;
    }

    void visit(const ArrayExpr &expr) override
    {
        printer.os << expr.name << '(';
        expr.index->accept(*this);
        printer.os << ')';
    }

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

    void visit(const LBoundExpr &expr) override
    {
        printer.os << "(LBOUND " << expr.name << ')';
    }

    void visit(const UBoundExpr &expr) override
    {
        printer.os << "(UBOUND " << expr.name << ')';
    }

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

void AstPrinter::printExpr(const Expr &expr, Printer &printer, PrintStyle &style)
{
    ExprPrinter exprPrinter{printer, style};
    exprPrinter.print(expr);
}

} // namespace il::frontends::basic

