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

namespace il::frontends::basic
{

namespace
{

/// @brief Convert a semantic type enum to a human-readable mnemonic.
///
/// The AST printer uses short mnemonics to keep dumps compact while still
/// distinguishing string and boolean types.
///
/// @param ty Semantic type recorded on a declaration or expression.
/// @return Pointer to a static string describing the type.
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

/// @brief Convert an OPEN statement mode to its textual keyword.
///
/// The helper ensures the printer emits canonical casing for each mode.
///
/// @param mode Enumerated mode stored on the AST.
/// @return Pointer to a static keyword string.
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
    /// @brief Construct the visitor bound to a printer and formatting style.
    StmtPrinter(Printer &printer, PrintStyle &style) : printer(printer), style(style) {}

    /// @brief Render a statement by dispatching through the visitor interface.
    void print(const Stmt &stmt)
    {
        stmt.accept(*this);
    }

    /// @brief Catch-all handler to surface missing visitor implementations.
    template <typename NodeT> void visit([[maybe_unused]] const NodeT &)
    {
        static_assert(sizeof(NodeT) == 0, "Unhandled statement node in AstPrinter");
    }

    void visit(const LabelStmt &) override
    {
        printer.os << "(LABEL)";
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
        if (stmt.mode == PrintChStmt::Mode::Write)
        {
            printer.os << "(WRITE#";
        }
        else
        {
            printer.os << "(PRINT#";
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

    void visit(const CallStmt &stmt) override
    {
        printer.os << "(CALL";
        if (stmt.call)
        {
            printer.os << ' ';
            AstPrinter::printExpr(*stmt.call, printer, style);
        }
        printer.os << ')';
    }

    void visit(const ClsStmt &) override
    {
        printer.os << "(CLS)";
    }

    void visit(const ColorStmt &stmt) override
    {
        printer.os << "(COLOR ";
        if (stmt.fg)
        {
            AstPrinter::printExpr(*stmt.fg, printer, style);
        }
        else
        {
            style.writeNull();
        }
        printer.os << ' ';
        if (stmt.bg)
        {
            AstPrinter::printExpr(*stmt.bg, printer, style);
        }
        else
        {
            style.writeNull();
        }
        printer.os << ')';
    }

    void visit(const LocateStmt &stmt) override
    {
        printer.os << "(LOCATE ";
        if (stmt.row)
        {
            AstPrinter::printExpr(*stmt.row, printer, style);
        }
        else
        {
            style.writeNull();
        }
        if (stmt.col)
        {
            printer.os << ' ';
            AstPrinter::printExpr(*stmt.col, printer, style);
        }
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

    void visit(const SelectCaseStmt &stmt) override
    {
        printer.os << "(SELECT CASE ";
        if (stmt.selector)
            AstPrinter::printExpr(*stmt.selector, printer, style);
        else
            style.writeNull();
        for (const auto &arm : stmt.arms)
        {
            printer.os << " (CASE";
            for (auto label : arm.labels)
                printer.os << ' ' << label;
            printer.os << ')';
            printNumberedBody(arm.body);
        }
        if (!stmt.elseBody.empty())
        {
            printer.os << " (CASE ELSE)";
            printNumberedBody(stmt.elseBody);
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

    void visit(const GosubStmt &stmt) override
    {
        printer.os << "(GOSUB " << stmt.targetLine << ')';
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

    void visit(const SeekStmt &stmt) override
    {
        printer.os << "(SEEK";
        style.writeChannelPrefix();
        if (stmt.channelExpr)
        {
            AstPrinter::printExpr(*stmt.channelExpr, printer, style);
        }
        else
        {
            style.writeNull();
        }
        printer.os << " pos=";
        if (stmt.positionExpr)
        {
            AstPrinter::printExpr(*stmt.positionExpr, printer, style);
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
        bool firstItem = true;
        auto writeItemPrefix = [&] {
            if (firstItem)
            {
                printer.os << ' ';
                firstItem = false;
            }
            else
            {
                printer.os << ", ";
            }
        };
        if (stmt.prompt)
        {
            writeItemPrefix();
            AstPrinter::printExpr(*stmt.prompt, printer, style);
        }
        for (const auto &name : stmt.vars)
        {
            writeItemPrefix();
            printer.os << name;
        }
        printer.os << ')';
    }

    void visit(const InputChStmt &stmt) override
    {
        printer.os << "(INPUT#";
        style.writeChannelPrefix();
        printer.os << stmt.channel;
        printer.os << " target=" << stmt.target.name << ')';
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
        if (stmt.isGosubReturn)
            printer.os << " GOSUB";
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

    void visit(const DeleteStmt &stmt) override
    {
        printer.os << "(DELETE ";
        AstPrinter::printExpr(*stmt.target, printer, style);
        printer.os << ')';
    }

    void visit(const ConstructorDecl &stmt) override
    {
        printer.os << "(CONSTRUCTOR (";
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

    void visit(const DestructorDecl &stmt) override
    {
        printer.os << "(DESTRUCTOR";
        printNumberedBody(stmt.body);
    }

    void visit(const MethodDecl &stmt) override
    {
        printer.os << "(METHOD " << stmt.name;
        if (stmt.ret)
            printer.os << " RET " << typeToString(*stmt.ret);
        printer.os << " (";
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

    void visit(const ClassDecl &stmt) override
    {
        printer.os << "(CLASS " << stmt.name;
        if (!stmt.fields.empty())
        {
            printer.os << " (FIELDS";
            for (const auto &field : stmt.fields)
            {
                printer.os << ' ' << field.name << ':' << typeToString(field.type);
            }
            printer.os << ')';
        }
        printNumberedBody(stmt.members);
    }

    void visit(const TypeDecl &stmt) override
    {
        printer.os << "(TYPE " << stmt.name;
        if (!stmt.fields.empty())
        {
            printer.os << " (FIELDS";
            for (const auto &field : stmt.fields)
            {
                printer.os << ' ' << field.name << ':' << typeToString(field.type);
            }
            printer.os << ')';
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

