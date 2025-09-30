// File: src/frontends/basic/AstPrinter.cpp
// Purpose: Implements BASIC AST printer for debugging.
// Key invariants: None.
// Ownership/Lifetime: Printer does not own AST nodes.
// Notes: Uses internal Printer helper for consistent formatting.
// Links: docs/codemap.md

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
#include <array>
#include <sstream>

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

struct AstPrinter::ExprPrinter final : ExprVisitor
{
    explicit ExprPrinter(Printer &printer) : printer(printer) {}

    void print(const Expr &expr)
    {
        expr.accept(*this);
    }

    template <typename NodeT> void visit([[maybe_unused]] const NodeT &)
    {
        static_assert(sizeof(NodeT) == 0, "Unhandled expression node in AstPrinter::ExprPrinter");
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
        printer.os << "(NOT ";
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

struct AstPrinter::StmtPrinter final : StmtVisitor
{
    explicit StmtPrinter(Printer &printer) : printer(printer), exprPrinter(printer) {}

    void print(const Stmt &stmt)
    {
        stmt.accept(*this);
    }

    template <typename NodeT> void visit([[maybe_unused]] const NodeT &)
    {
        static_assert(sizeof(NodeT) == 0, "Unhandled statement node in AstPrinter::StmtPrinter");
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
                    item.expr->accept(exprPrinter);
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

    void visit(const LetStmt &stmt) override
    {
        printer.os << "(LET ";
        stmt.target->accept(exprPrinter);
        printer.os << ' ';
        stmt.expr->accept(exprPrinter);
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
                stmt.size->accept(exprPrinter);
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
            stmt.size->accept(exprPrinter);
        }
        printer.os << ')';
    }

    void visit(const RandomizeStmt &stmt) override
    {
        printer.os << "(RANDOMIZE ";
        stmt.seed->accept(exprPrinter);
        printer.os << ')';
    }

    void visit(const IfStmt &stmt) override
    {
        printer.os << "(IF ";
        stmt.cond->accept(exprPrinter);
        printer.os << " THEN ";
        stmt.then_branch->accept(*this);
        for (const auto &elseif : stmt.elseifs)
        {
            printer.os << " ELSEIF ";
            elseif.cond->accept(exprPrinter);
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
        stmt.cond->accept(exprPrinter);
        printer.os << " {";
        bool first = true;
        for (const auto &bodyStmt : stmt.body)
        {
            if (!first)
                printer.os << ' ';
            first = false;
            printer.os << std::to_string(bodyStmt->line) << ':';
            bodyStmt->accept(*this);
        }
        printer.os << "})";
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
            stmt.cond->accept(exprPrinter);
        }
        printer.os << " {";
        bool first = true;
        for (const auto &bodyStmt : stmt.body)
        {
            if (!first)
                printer.os << ' ';
            first = false;
            printer.os << std::to_string(bodyStmt->line) << ':';
            bodyStmt->accept(*this);
        }
        printer.os << "})";
    }

    void visit(const ForStmt &stmt) override
    {
        printer.os << "(FOR " << stmt.var << " = ";
        stmt.start->accept(exprPrinter);
        printer.os << " TO ";
        stmt.end->accept(exprPrinter);
        if (stmt.step)
        {
            printer.os << " STEP ";
            stmt.step->accept(exprPrinter);
        }
        printer.os << " {";
        bool first = true;
        for (const auto &bodyStmt : stmt.body)
        {
            if (!first)
                printer.os << ' ';
            first = false;
            printer.os << std::to_string(bodyStmt->line) << ':';
            bodyStmt->accept(*this);
        }
        printer.os << "})";
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
            stmt.pathExpr->accept(exprPrinter);
        }
        else
        {
            printer.os << "<null>";
        }
        printer.os << " channel=#";
        if (stmt.channelExpr)
        {
            stmt.channelExpr->accept(exprPrinter);
        }
        else
        {
            printer.os << "<null>";
        }
        printer.os << ')';
    }

    void visit(const CloseStmt &stmt) override
    {
        printer.os << "(CLOSE channel=#";
        if (stmt.channelExpr)
        {
            stmt.channelExpr->accept(exprPrinter);
        }
        else
        {
            printer.os << "<null>";
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
            stmt.prompt->accept(exprPrinter);
            printer.os << ',';
        }
        printer.os << ' ' << stmt.var << ')';
    }

    void visit(const ReturnStmt &stmt) override
    {
        printer.os << "(RETURN";
        if (stmt.value)
        {
            printer.os << ' ';
            stmt.value->accept(exprPrinter);
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
        printer.os << ") {";
        bool firstStmt = true;
        for (const auto &bodyStmt : stmt.body)
        {
            if (!firstStmt)
                printer.os << ' ';
            firstStmt = false;
            printer.os << std::to_string(bodyStmt->line) << ':';
            bodyStmt->accept(*this);
        }
        printer.os << "})";
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
        printer.os << ") {";
        bool firstStmt = true;
        for (const auto &bodyStmt : stmt.body)
        {
            if (!firstStmt)
                printer.os << ' ';
            firstStmt = false;
            printer.os << std::to_string(bodyStmt->line) << ':';
            bodyStmt->accept(*this);
        }
        printer.os << "})";
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
    Printer &printer;
    ExprPrinter exprPrinter;
};

/// @brief Write a line of text to the underlying stream with current indentation.
/// @param text Line content to emit.
/// @note Appends a newline character and resets column position.
void AstPrinter::Printer::line(std::string_view text)
{
    for (int i = 0; i < indent; ++i)
        os << "  ";
    os << text << '\n';
}

/// @brief Increase indentation level and return RAII guard.
/// @return Indent object whose destruction restores previous indentation.
AstPrinter::Printer::Indent AstPrinter::Printer::push()
{
    ++indent;
    return Indent{*this};
}

/// @brief Restore the indentation level saved at construction time.
AstPrinter::Printer::Indent::~Indent()
{
    --p.indent;
}

/// @brief Serialize an entire BASIC program to a printable string.
/// @param prog Program whose procedures and main body are dumped.
/// @returns Concatenated text representation of @p prog.
std::string AstPrinter::dump(const Program &prog)
{
    std::ostringstream os;
    Printer p{os};
    for (auto &stmt : prog.procs)
    {
        std::ostringstream line_os;
        line_os << stmt->line << ": ";
        Printer line_p{line_os};
        dump(*stmt, line_p);
        p.line(line_os.str());
    }
    for (auto &stmt : prog.main)
    {
        std::ostringstream line_os;
        line_os << stmt->line << ": ";
        Printer line_p{line_os};
        dump(*stmt, line_p);
        p.line(line_os.str());
    }
    return os.str();
}

/// @brief Recursively print a statement node and its children.
/// @param stmt Statement to dump.
/// @param p Printer receiving the textual form.
void AstPrinter::dump(const Stmt &stmt, Printer &p)
{
    StmtPrinter printer{p};
    printer.print(stmt);
}

/// @brief Print an expression node to the printer.
/// @param expr Expression to dump.
/// @param p Printer receiving output.
void AstPrinter::dump(const Expr &expr, Printer &p)
{
    ExprPrinter printer{p};
    printer.print(expr);
}

} // namespace il::frontends::basic
