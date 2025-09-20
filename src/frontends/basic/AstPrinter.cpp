// File: src/frontends/basic/AstPrinter.cpp
// Purpose: Implements BASIC AST printer for debugging.
// Key invariants: None.
// Ownership/Lifetime: Printer does not own AST nodes.
// Notes: Uses internal Printer helper for consistent formatting.
// Links: docs/class-catalog.md

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/BuiltinRegistry.hpp"
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

} // namespace

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
/// @details Dispatches on the runtime type of @p stmt to emit the
/// corresponding s-expression-like representation.
void AstPrinter::dump(const Stmt &stmt, Printer &p)
{
    // Dispatch on concrete statement type via dynamic_cast chain.
    if (auto *lst = dynamic_cast<const StmtList *>(&stmt))
    {
        p.os << "(SEQ";
        for (auto &s : lst->stmts)
        {
            p.os << ' ';
            dump(*s, p);
        }
        p.os << ')';
    }
    else if (auto *pr = dynamic_cast<const PrintStmt *>(&stmt))
    {
        p.os << "(PRINT";
        // Each print item may be an expression or formatting token.
        for (const auto &it : pr->items)
        {
            p.os << ' ';
            switch (it.kind)
            {
                case PrintItem::Kind::Expr:
                    dump(*it.expr, p);
                    break;
                case PrintItem::Kind::Comma:
                    p.os << ',';
                    break;
                case PrintItem::Kind::Semicolon:
                    p.os << ';';
                    break;
            }
        }
        p.os << ')';
    }
    else if (auto *l = dynamic_cast<const LetStmt *>(&stmt))
    {
        p.os << "(LET ";
        dump(*l->target, p);
        p.os << ' ';
        dump(*l->expr, p);
        p.os << ')';
    }
    else if (auto *d = dynamic_cast<const DimStmt *>(&stmt))
    {
        p.os << "(DIM " << d->name;
        if (d->isArray)
        {
            if (d->size)
            {
                p.os << ' ';
                dump(*d->size, p);
            }
            if (d->type != Type::I64)
                p.os << " AS " << typeToString(d->type);
        }
        else
        {
            p.os << " AS " << typeToString(d->type);
        }
        p.os << ')';
    }
    else if (auto *r = dynamic_cast<const RandomizeStmt *>(&stmt))
    {
        p.os << "(RANDOMIZE ";
        dump(*r->seed, p);
        p.os << ')';
    }
    else if (auto *i = dynamic_cast<const IfStmt *>(&stmt))
    {
        p.os << "(IF ";
        dump(*i->cond, p);
        p.os << " THEN ";
        dump(*i->then_branch, p);
        // Emit any ELSEIF branches in order of appearance.
        for (const auto &e : i->elseifs)
        {
            p.os << " ELSEIF ";
            dump(*e.cond, p);
            p.os << " THEN ";
            dump(*e.then_branch, p);
        }
        // Optional final ELSE branch.
        if (i->else_branch)
        {
            p.os << " ELSE ";
            dump(*i->else_branch, p);
        }
        p.os << ')';
    }
    else if (auto *w = dynamic_cast<const WhileStmt *>(&stmt))
    {
        p.os << "(WHILE ";
        dump(*w->cond, p);
        p.os << " {";
        bool first = true;
        // Serialize loop body with embedded source line numbers.
        for (auto &s : w->body)
        {
            if (!first)
                p.os << ' ';
            first = false;
            p.os << std::to_string(s->line) << ':';
            dump(*s, p);
        }
        p.os << "})";
    }
    else if (auto *f = dynamic_cast<const ForStmt *>(&stmt))
    {
        p.os << "(FOR " << f->var << " = ";
        dump(*f->start, p);
        p.os << " TO ";
        dump(*f->end, p);
        // STEP clause is optional.
        if (f->step)
        {
            p.os << " STEP ";
            dump(*f->step, p);
        }
        p.os << " {";
        bool first = true;
        // Body statements carry line numbers to mirror source.
        for (auto &s : f->body)
        {
            if (!first)
                p.os << ' ';
            first = false;
            p.os << std::to_string(s->line) << ':';
            dump(*s, p);
        }
        p.os << "})";
    }
    else if (auto *n = dynamic_cast<const NextStmt *>(&stmt))
    {
        p.os << "(NEXT " << n->var << ')';
    }
    else if (auto *g = dynamic_cast<const GotoStmt *>(&stmt))
    {
        p.os << "(GOTO " << g->target << ')';
    }
    else if (auto *r = dynamic_cast<const ReturnStmt *>(&stmt))
    {
        p.os << "(RETURN";
        if (r->value)
        {
            p.os << ' ';
            dump(*r->value, p);
        }
        p.os << ')';
    }
    else if (auto *f = dynamic_cast<const FunctionDecl *>(&stmt))
    {
        p.os << "(FUNCTION " << f->name << " RET " << typeToString(f->ret) << " (";
        bool first = true;
        // Parameters are printed in declaration order.
        for (auto &pa : f->params)
        {
            if (!first)
                p.os << ' ';
            first = false;
            p.os << pa.name;
            if (pa.is_array)
                p.os << "()";
        }
        p.os << ") {";
        bool firstStmt = true;
        // Function body statements with line numbers.
        for (auto &s : f->body)
        {
            if (!firstStmt)
                p.os << ' ';
            firstStmt = false;
            p.os << std::to_string(s->line) << ':';
            dump(*s, p);
        }
        p.os << "})";
    }
    else if (auto *sb = dynamic_cast<const SubDecl *>(&stmt))
    {
        p.os << "(SUB " << sb->name << " (";
        bool first = true;
        // Serialize parameter list similar to functions.
        for (auto &pa : sb->params)
        {
            if (!first)
                p.os << ' ';
            first = false;
            p.os << pa.name;
            if (pa.is_array)
                p.os << "()";
        }
        p.os << ") {";
        bool firstStmt = true;
        // Dump body statements with their line numbers.
        for (auto &s : sb->body)
        {
            if (!firstStmt)
                p.os << ' ';
            firstStmt = false;
            p.os << std::to_string(s->line) << ':';
            dump(*s, p);
        }
        p.os << "})";
    }
    else if (dynamic_cast<const EndStmt *>(&stmt))
    {
        p.os << "(END)";
    }
    else
    {
        p.os << "(?)";
    }
}

/// @brief Print an expression node to the printer.
/// @param expr Expression to dump.
/// @param p Printer receiving output.
/// @details Handles literals, variables, operators, and calls by
/// inspecting the dynamic expression type.
void AstPrinter::dump(const Expr &expr, Printer &p)
{
    // Dispatch on concrete expression type.
    if (auto *i = dynamic_cast<const IntExpr *>(&expr))
    {
        p.os << i->value;
    }
    else if (auto *f = dynamic_cast<const FloatExpr *>(&expr))
    {
        std::ostringstream os;
        os << f->value;
        p.os << os.str();
    }
    else if (auto *s = dynamic_cast<const StringExpr *>(&expr))
    {
        p.os << '"' << s->value << '"';
    }
    else if (auto *v = dynamic_cast<const VarExpr *>(&expr))
    {
        p.os << v->name;
    }
    else if (auto *b = dynamic_cast<const BinaryExpr *>(&expr))
    {
        static constexpr std::array<const char *, 16> ops = {
            "+",
            "-",
            "*",
            "/",
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
        // Binary expressions print operator then operands.
        p.os << '(' << ops[static_cast<size_t>(b->op)] << ' ';
        dump(*b->lhs, p);
        p.os << ' ';
        dump(*b->rhs, p);
        p.os << ')';
    }
    else if (auto *b = dynamic_cast<const BoolExpr *>(&expr))
    {
        p.os << (b->value ? "TRUE" : "FALSE");
    }
    else if (auto *u = dynamic_cast<const UnaryExpr *>(&expr))
    {
        p.os << "(NOT ";
        dump(*u->expr, p);
        p.os << ')';
    }
    else if (auto *c = dynamic_cast<const BuiltinCallExpr *>(&expr))
    {
        // Map builtin enum to name then dump arguments.
        p.os << '(' << getBuiltinInfo(c->builtin).name;
        for (auto &a : c->args)
        {
            p.os << ' ';
            dump(*a, p);
        }
        p.os << ')';
    }
    else if (auto *c = dynamic_cast<const CallExpr *>(&expr))
    {
        // User-defined call with callee name and arguments.
        p.os << '(' << c->callee;
        for (auto &a : c->args)
        {
            p.os << ' ';
            dump(*a, p);
        }
        p.os << ')';
    }
    else if (auto *a = dynamic_cast<const ArrayExpr *>(&expr))
    {
        p.os << a->name << '(';
        dump(*a->index, p);
        p.os << ')';
    }
    else
    {
        p.os << '?';
    }
}

} // namespace il::frontends::basic
