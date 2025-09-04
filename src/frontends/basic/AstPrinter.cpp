// File: src/frontends/basic/AstPrinter.cpp
// Purpose: Implements BASIC AST printer for debugging.
// Key invariants: None.
// Ownership/Lifetime: Printer does not own AST nodes.
// Notes: Uses internal Printer helper for consistent formatting.
// Links: docs/class-catalog.md

#include "frontends/basic/AstPrinter.hpp"
#include <array>
#include <sstream>

namespace il::frontends::basic
{

void AstPrinter::Printer::line(std::string_view text)
{
    for (int i = 0; i < indent; ++i)
        os << "  ";
    os << text << '\n';
}

AstPrinter::Printer::Indent AstPrinter::Printer::push()
{
    ++indent;
    return Indent{*this};
}

std::string AstPrinter::dump(const Program &prog)
{
    std::ostringstream os;
    Printer p{os};
    for (auto &stmt : prog.statements)
    {
        std::ostringstream line_os;
        line_os << stmt->line << ": ";
        Printer line_p{line_os};
        dump(*stmt, line_p);
        p.line(line_os.str());
    }
    return os.str();
}

void AstPrinter::dump(const Stmt &stmt, Printer &p)
{
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
        p.os << "(DIM " << d->name << ' ';
        dump(*d->size, p);
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
        for (const auto &e : i->elseifs)
        {
            p.os << " ELSEIF ";
            dump(*e.cond, p);
            p.os << " THEN ";
            dump(*e.then_branch, p);
        }
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
        if (f->step)
        {
            p.os << " STEP ";
            dump(*f->step, p);
        }
        p.os << " {";
        bool first = true;
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
    else if (dynamic_cast<const EndStmt *>(&stmt))
    {
        p.os << "(END)";
    }
    else
    {
        p.os << "(?)";
    }
}

void AstPrinter::dump(const Expr &expr, Printer &p)
{
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
        static constexpr std::array<const char *, 14> ops = {
            "+", "-", "*", "/", "\\", "MOD", "=", "<>", "<", "<=", ">", ">=", "AND", "OR"};
        p.os << '(' << ops[static_cast<size_t>(b->op)] << ' ';
        dump(*b->lhs, p);
        p.os << ' ';
        dump(*b->rhs, p);
        p.os << ')';
    }
    else if (auto *u = dynamic_cast<const UnaryExpr *>(&expr))
    {
        p.os << "(NOT ";
        dump(*u->expr, p);
        p.os << ')';
    }
    else if (auto *c = dynamic_cast<const CallExpr *>(&expr))
    {
        static constexpr std::array<const char *, 15> names = {"LEN",
                                                               "MID$",
                                                               "LEFT$",
                                                               "RIGHT$",
                                                               "STR$",
                                                               "VAL",
                                                               "INT",
                                                               "SQR",
                                                               "ABS",
                                                               "FLOOR",
                                                               "CEIL",
                                                               "SIN",
                                                               "COS",
                                                               "POW",
                                                               "RND"};
        p.os << '(' << names[static_cast<size_t>(c->builtin)];
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
