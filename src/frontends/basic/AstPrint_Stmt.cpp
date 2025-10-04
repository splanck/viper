// File: src/frontends/basic/AstPrint_Stmt.cpp
// Purpose: Implements statement printing logic for the BASIC AST printer.
// Key invariants: All statement node kinds must be handled.
// Ownership/Lifetime: Operates on non-owning AST references.
// Notes: Relies on PrintStyle to manage spacing and body formatting.
// Links: docs/codemap.md

#include "frontends/basic/AstPrinter.hpp"

namespace il::frontends::basic
{

namespace
{

const char *typeToString(Type ty)
{
    switch (ty)
    {
        case Type::I64:
            return "I64";
        case Type::F64:
            return "F64";
        case Type::Str:
            return "STR";
        case Type::Bool:
            return "BOOLEAN";
    }
    return "I64";
}

const char *openModeToString(OpenStmt::Mode mode)
{
    switch (mode)
    {
        case OpenStmt::Mode::Input:
            return "INPUT";
        case OpenStmt::Mode::Output:
            return "OUTPUT";
        case OpenStmt::Mode::Append:
            return "APPEND";
        case OpenStmt::Mode::Binary:
            return "BINARY";
        case OpenStmt::Mode::Random:
            return "RANDOM";
    }
    return "INPUT";
}

} // namespace

struct AstPrinter::StmtPrinter final : StmtVisitor
{
    StmtPrinter(Printer &printer, PrintStyle &style) : printer(printer), style(style) {}

    void print(const Stmt &stmt)
    {
        stmt.accept(*this);
    }

    template <typename NodeT> void visit([[maybe_unused]] const NodeT &)
    {
        static_assert(sizeof(NodeT) == 0, "Unhandled statement node in AstPrinter");
    }

    void visit(const PrintStmt &stmt) override
    {
        printer.os << "(PRINT";
        for (const auto &item : stmt.items)
        {
            printer.os << ' ';
            switch (item.kind)
            {
                case PrintItem::Kind::Expr:
                    AstPrinter::printExpr(*item.expr, printer, style);
                    break;
                case PrintItem::Kind::Comma:
                    printer.os << ',';
                    break;
                case PrintItem::Kind::Semicolon:
                    printer.os << ';';
                    break;
            }
        }
        printer.os << ')';
    }

    void visit(const PrintChStmt &stmt) override
    {
        printer.os << "(PRINT#";
        style.writeChannelPrefix();
        if (stmt.channelExpr)
        {
            AstPrinter::printExpr(*stmt.channelExpr, printer, style);
        }
        else
        {
            style.writeNull();
        }
        style.writeArgsPrefix();
        bool first = true;
        for (const auto &arg : stmt.args)
        {
            style.separate(first);
            if (arg)
            {
                AstPrinter::printExpr(*arg, printer, style);
            }
            else
            {
                style.writeNull();
            }
        }
        style.writeArgsSuffix();
        if (!stmt.trailingNewline)
            style.writeNoNewlineTag();
        printer.os << ')';
    }

    void visit(const LetStmt &stmt) override
    {
        printer.os << "(LET ";
        AstPrinter::printExpr(*stmt.target, printer, style);
        printer.os << ' ';
        AstPrinter::printExpr(*stmt.expr, printer, style);
        printer.os << ')';
    }

    void visit(const DimStmt &stmt) override
    {
        printer.os << "(DIM " << stmt.name;
        if (stmt.isArray)
        {
            if (stmt.size)
            {
                printer.os << ' ';
                AstPrinter::printExpr(*stmt.size, printer, style);
            }
            if (stmt.type != Type::I64)
                printer.os << " AS " << typeToString(stmt.type);
        }
        else
        {
            printer.os << " AS " << typeToString(stmt.type);
        }
        printer.os << ')';
    }

    void visit(const ReDimStmt &stmt) override
    {
        printer.os << "(REDIM " << stmt.name;
        if (stmt.size)
        {
            printer.os << ' ';
            AstPrinter::printExpr(*stmt.size, printer, style);
        }
        printer.os << ')';
    }

    void visit(const RandomizeStmt &stmt) override
    {
        printer.os << "(RANDOMIZE ";
        AstPrinter::printExpr(*stmt.seed, printer, style);
        printer.os << ')';
    }

    void visit(const IfStmt &stmt) override
    {
        printer.os << "(IF ";
        AstPrinter::printExpr(*stmt.cond, printer, style);
        printer.os << " THEN ";
        stmt.then_branch->accept(*this);
        for (const auto &elseif : stmt.elseifs)
        {
            printer.os << " ELSEIF ";
            AstPrinter::printExpr(*elseif.cond, printer, style);
            printer.os << " THEN ";
            elseif.then_branch->accept(*this);
        }
        if (stmt.else_branch)
        {
            printer.os << " ELSE ";
            stmt.else_branch->accept(*this);
        }
        printer.os << ')';
    }

    void visit(const WhileStmt &stmt) override
    {
        printer.os << "(WHILE ";
        AstPrinter::printExpr(*stmt.cond, printer, style);
        printNumberedBody(stmt.body);
    }

    void visit(const DoStmt &stmt) override
    {
        printer.os << "(DO "
                    << (stmt.testPos == DoStmt::TestPos::Pre ? "pre" : "post") << ' ';
        switch (stmt.condKind)
        {
            case DoStmt::CondKind::None:
                printer.os << "NONE";
                break;
            case DoStmt::CondKind::While:
                printer.os << "WHILE";
                break;
            case DoStmt::CondKind::Until:
                printer.os << "UNTIL";
                break;
        }
        if (stmt.condKind != DoStmt::CondKind::None && stmt.cond)
        {
            printer.os << ' ';
            AstPrinter::printExpr(*stmt.cond, printer, style);
        }
        printNumberedBody(stmt.body);
    }

    void visit(const ForStmt &stmt) override
    {
        printer.os << "(FOR " << stmt.var << " = ";
        AstPrinter::printExpr(*stmt.start, printer, style);
        printer.os << " TO ";
        AstPrinter::printExpr(*stmt.end, printer, style);
        if (stmt.step)
        {
            printer.os << " STEP ";
            AstPrinter::printExpr(*stmt.step, printer, style);
        }
        printNumberedBody(stmt.body);
    }

    void visit(const NextStmt &stmt) override
    {
        printer.os << "(NEXT " << stmt.var << ')';
    }

    void visit(const ExitStmt &stmt) override
    {
        printer.os << "(EXIT ";
        switch (stmt.kind)
        {
            case ExitStmt::LoopKind::For:
                printer.os << "FOR";
                break;
            case ExitStmt::LoopKind::While:
                printer.os << "WHILE";
                break;
            case ExitStmt::LoopKind::Do:
                printer.os << "DO";
                break;
        }
        printer.os << ')';
    }

    void visit(const GotoStmt &stmt) override
    {
        printer.os << "(GOTO " << stmt.target << ')';
    }

    void visit(const OpenStmt &stmt) override
    {
        printer.os << "(OPEN mode=" << openModeToString(stmt.mode) << '('
                    << static_cast<int>(stmt.mode) << ") path=";
        if (stmt.pathExpr)
        {
            AstPrinter::printExpr(*stmt.pathExpr, printer, style);
        }
        else
        {
            style.writeNull();
        }
        style.writeChannelPrefix();
        if (stmt.channelExpr)
        {
            AstPrinter::printExpr(*stmt.channelExpr, printer, style);
        }
        else
        {
            style.writeNull();
        }
        printer.os << ')';
    }

    void visit(const CloseStmt &stmt) override
    {
        printer.os << "(CLOSE";
        style.writeChannelPrefix();
        if (stmt.channelExpr)
        {
            AstPrinter::printExpr(*stmt.channelExpr, printer, style);
        }
        else
        {
            style.writeNull();
        }
        printer.os << ')';
    }

    void visit(const OnErrorGoto &stmt) override
    {
        printer.os << "(ON-ERROR GOTO ";
        if (stmt.toZero)
        {
            printer.os << '0';
        }
        else
        {
            printer.os << stmt.target;
        }
        printer.os << ')';
    }

    void visit(const Resume &stmt) override
    {
        printer.os << "(RESUME";
        switch (stmt.mode)
        {
            case Resume::Mode::Same:
                break;
            case Resume::Mode::Next:
                printer.os << " NEXT";
                break;
            case Resume::Mode::Label:
                printer.os << ' ' << stmt.target;
                break;
        }
        printer.os << ')';
    }

    void visit(const EndStmt &) override
    {
        printer.os << "(END)";
    }

    void visit(const InputStmt &stmt) override
    {
        printer.os << "(INPUT";
        if (stmt.prompt)
        {
            printer.os << ' ';
            AstPrinter::printExpr(*stmt.prompt, printer, style);
            printer.os << ',';
        }
        printer.os << ' ' << stmt.var << ')';
    }

    void visit(const LineInputChStmt &stmt) override
    {
        printer.os << "(LINE-INPUT#";
        style.writeChannelPrefix();
        if (stmt.channelExpr)
        {
            AstPrinter::printExpr(*stmt.channelExpr, printer, style);
        }
        else
        {
            style.writeNull();
        }
        printer.os << " target=";
        if (stmt.targetVar)
        {
            AstPrinter::printExpr(*stmt.targetVar, printer, style);
        }
        else
        {
            style.writeNull();
        }
        printer.os << ')';
    }

    void visit(const ReturnStmt &stmt) override
    {
        printer.os << "(RETURN";
        if (stmt.value)
        {
            printer.os << ' ';
            AstPrinter::printExpr(*stmt.value, printer, style);
        }
        printer.os << ')';
    }

    void visit(const FunctionDecl &stmt) override
    {
        printer.os << "(FUNCTION " << stmt.name << " RET " << typeToString(stmt.ret) << " (";
        bool firstParam = true;
        for (const auto &param : stmt.params)
        {
            if (!firstParam)
                printer.os << ' ';
            firstParam = false;
            printer.os << param.name;
            if (param.is_array)
                printer.os << "()";
        }
        printer.os << ")";
        printNumberedBody(stmt.body);
    }

    void visit(const SubDecl &stmt) override
    {
        printer.os << "(SUB " << stmt.name << " (";
        bool firstParam = true;
        for (const auto &param : stmt.params)
        {
            if (!firstParam)
                printer.os << ' ';
            firstParam = false;
            printer.os << param.name;
            if (param.is_array)
                printer.os << "()";
        }
        printer.os << ")";
        printNumberedBody(stmt.body);
    }

    void visit(const StmtList &stmt) override
    {
        printer.os << "(SEQ";
        for (const auto &subStmt : stmt.stmts)
        {
            printer.os << ' ';
            subStmt->accept(*this);
        }
        printer.os << ')';
    }

  private:
    void printNumberedBody(const std::vector<std::unique_ptr<Stmt>> &body)
    {
        style.openBody();
        bool first = true;
        for (const auto &bodyStmt : body)
        {
            style.separate(first);
            style.writeLineNumber(bodyStmt->line);
            bodyStmt->accept(*this);
        }
        style.closeBody();
    }

    Printer &printer;
    PrintStyle &style;
};

void AstPrinter::printStmt(const Stmt &stmt, Printer &printer, PrintStyle &style)
{
    StmtPrinter stmtPrinter{printer, style};
    stmtPrinter.print(stmt);
}

} // namespace il::frontends::basic

