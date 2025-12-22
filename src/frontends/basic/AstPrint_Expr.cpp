//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
/// @brief Expression-printing visitor used by the BASIC AST printer.
/// @details Implements the concrete visitor that walks expression nodes and
///          serialises them into the `AstPrinter` stream using a stable textual
///          representation.  Formatting choices mirror the statement printer so
///          AST dumps remain both human-readable and deterministic.

namespace il::frontends::basic
{

namespace
{
} // namespace

/// @brief Visitor that renders expression nodes to the printer's stream.
/// @details Each override emits prefix-style textual forms that match the
///          companion statement printer, ensuring dumps remain stable and easy
///          to parse visually.
struct AstPrinter::ExprPrinter final : ExprVisitor
{
    /// @brief Construct the visitor with a destination printer.
    /// @details The style parameter is currently unused but preserved so
    ///          expression and statement visitors share a consistent signature.
    ///          Future formatting tweaks can opt into style-specific behaviour
    ///          without changing call sites.
    ExprPrinter(Printer &printer, [[maybe_unused]] PrintStyle &style) : printer(printer) {}

    /// @brief Dispatch expression printing through the visitor interface.
    /// @details Delegates to @ref Expr::accept, enabling virtual dispatch across
    ///          the node hierarchy while keeping the call site concise.
    void print(const Expr &expr)
    {
        expr.accept(*this);
    }

    /// @brief Fallback for unhandled expression types; triggers a build-time failure.
    /// @details Employs a `static_assert` so that adding a new node kind without
    ///          updating the printer yields a compiler error rather than a silent
    ///          omission from dumps.
    template <typename NodeT> void visit([[maybe_unused]] const NodeT &)
    {
        static_assert(sizeof(NodeT) == 0, "Unhandled expression node in AstPrinter");
    }

    /// @brief Print an integer literal expression.
    /// @details Writes the literal value verbatim, relying on the parser to have
    ///          normalised the token text already.
    void visit(const IntExpr &expr) override
    {
        printer.os << expr.value;
    }

    /// @brief Print a floating-point literal expression preserving precision.
    /// @details Streams the value through an @c ostringstream so the default C++
    ///          formatting rules apply while avoiding locale-sensitive
    ///          behaviour.
    void visit(const FloatExpr &expr) override
    {
        std::ostringstream os;
        os << expr.value;
        printer.os << os.str();
    }

    /// @brief Print a string literal with surrounding quotes.
    /// @details Emits the value wrapped in double quotes.  Characters are
    ///          written verbatim because the parser already normalises escape
    ///          sequences during AST construction.
    void visit(const StringExpr &expr) override
    {
        printer.os << '"' << expr.value << '"';
    }

    /// @brief Print a boolean literal as TRUE/FALSE tokens.
    /// @details Uses uppercase tokens to match the BASIC surface syntax and
    ///          golden tests.
    void visit(const BoolExpr &expr) override
    {
        printer.os << (expr.value ? "TRUE" : "FALSE");
    }

    /// @brief Print a variable reference by name.
    /// @details Emits the canonical identifier spelling stored on the node.
    void visit(const VarExpr &expr) override
    {
        printer.os << expr.name;
    }

    /// @brief Print an array element access with its index expression(s).
    /// @details Emits `name(expr)` or `name(i,j,k)` preserving the syntactic
    ///          order of the original index expressions.
    void visit(const ArrayExpr &expr) override
    {
        printer.os << expr.name << '(';
        bool first = true;
        for (const auto &indexPtr : expr.indices)
        {
            if (!indexPtr)
                continue;
            if (!first)
                printer.os << ',';
            first = false;
            indexPtr->accept(*this);
        }
        printer.os << ')';
    }

    /// @brief Print a unary expression with explicit operator tokens.
    /// @details Uses prefix notation to avoid ambiguity with chained unary
    ///          operators while keeping the textual output compact.
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
    /// @details The operator table mirrors the enum order so the visitor remains
    ///          data driven; prefix notation keeps evaluation order explicit for
    ///          nested expressions.
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
    /// @details Prepends the builtin mnemonic obtained from metadata and prints
    ///          each argument separated by spaces to match the prefix style used
    ///          throughout dumps.
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
    /// @details Emits `(LBOUND name)` mirroring the parser's canonical form.
    void visit(const LBoundExpr &expr) override
    {
        printer.os << "(LBOUND " << expr.name << ')';
    }

    /// @brief Print a UBOUND expression with its array operand.
    /// @details Mirrors the `LBOUND` formatting while using the appropriate
    ///          mnemonic.
    void visit(const UBoundExpr &expr) override
    {
        printer.os << "(UBOUND " << expr.name << ')';
    }

    /// @brief Print a user-defined call expression with its argument list.
    /// @details Emits `(callee arg1 arg2 ...)`, retaining the argument order
    ///          recorded in the AST.
    void visit(const CallExpr &expr) override
    {
        printer.os << '(';
        if (!expr.calleeQualified.empty())
        {
            for (size_t i = 0; i < expr.calleeQualified.size(); ++i)
            {
                if (i)
                    printer.os << '.';
                printer.os << expr.calleeQualified[i];
            }
        }
        else
        {
            printer.os << expr.callee;
        }
        for (const auto &arg : expr.args)
        {
            printer.os << ' ';
            arg->accept(*this);
        }
        printer.os << ')';
    }

    /// @brief Print an object construction expression.
    /// @details Serialises `(NEW ClassName args...)`, matching the lowering
    ///          pipeline's expectations when parsing dumps.
    void visit(const NewExpr &expr) override
    {
        printer.os << "(NEW ";
        if (!expr.qualifiedType.empty())
        {
            for (size_t i = 0; i < expr.qualifiedType.size(); ++i)
            {
                if (i)
                    printer.os << '.';
                printer.os << expr.qualifiedType[i];
            }
        }
        else
        {
            printer.os << expr.className;
        }
        for (const auto &arg : expr.args)
        {
            printer.os << ' ';
            arg->accept(*this);
        }
        printer.os << ')';
    }

    /// @brief Print the ME receiver expression.
    /// @details Emits the keyword `ME`, mirroring how the source language refers
    ///          to the current instance inside type members.
    void visit(const MeExpr &) override
    {
        printer.os << "Me";
    }

    /// @brief Print a member access expression as base.member.
    /// @details Wraps the expression in parentheses to keep nesting unambiguous
    ///          and prints the base expression followed by `.` and the member
    ///          identifier.
    void visit(const MemberAccessExpr &expr) override
    {
        printer.os << '(';
        expr.base->accept(*this);
        printer.os << '.' << expr.member << ')';
    }

    /// @brief Print a method invocation on an object instance.
    /// @details Prints the receiver expression, the method name, and each
    ///          argument using the same prefix convention as other calls so
    ///          chained invocations remain easy to read.
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

    /// @brief Print an IS expression as (IS <expr> <dotted-type>).
    void visit(const IsExpr &expr) override
    {
        printer.os << "(IS ";
        expr.value->accept(*this);
        printer.os << ' ';
        for (size_t i = 0; i < expr.typeName.size(); ++i)
        {
            if (i)
                printer.os << '.';
            printer.os << expr.typeName[i];
        }
        printer.os << ')';
    }

    /// @brief Print an AS expression as (AS <expr> <dotted-type>).
    void visit(const AsExpr &expr) override
    {
        printer.os << "(AS ";
        expr.value->accept(*this);
        printer.os << ' ';
        for (size_t i = 0; i < expr.typeName.size(); ++i)
        {
            if (i)
                printer.os << '.';
            printer.os << expr.typeName[i];
        }
        printer.os << ')';
    }

    /// @brief Print an ADDRESSOF expression as (ADDRESSOF <name>).
    void visit(const AddressOfExpr &expr) override
    {
        printer.os << "(ADDRESSOF " << expr.targetName << ')';
    }

  private:
    Printer &printer;
};

/// @brief Entry point used by AstPrinter to render an expression node.
/// @details Constructs the visitor and forwards the expression and style, which
///          allows the caller to remain agnostic to the concrete visitor
///          implementation.
void AstPrinter::printExpr(const Expr &expr, Printer &printer, PrintStyle &style)
{
    ExprPrinter exprPrinter{printer, style};
    exprPrinter.print(expr);
}

} // namespace il::frontends::basic
