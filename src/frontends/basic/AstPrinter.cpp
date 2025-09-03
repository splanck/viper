// File: src/frontends/basic/AstPrinter.cpp
// Purpose: Implements BASIC AST printer for debugging.
// Key invariants: None.
// Ownership/Lifetime: Printer does not own AST nodes.
// Links: docs/class-catalog.md

#include "frontends/basic/AstPrinter.hpp"
#include <sstream>

namespace il::frontends::basic
{

std::string AstPrinter::dump(const Program &prog)
{
    std::string out;
    for (auto &stmt : prog.statements)
    {
        out += std::to_string(stmt->line) + ": " + dump(*stmt) + "\n";
    }
    return out;
}

std::string AstPrinter::dump(const Stmt &stmt)
{
    if (auto *lst = dynamic_cast<const StmtList *>(&stmt))
    {
        std::string res = "(SEQ";
        for (auto &s : lst->stmts)
            res += " " + dump(*s);
        res += ")";
        return res;
    }
    else if (auto *p = dynamic_cast<const PrintStmt *>(&stmt))
    {
        std::string res = "(PRINT";
        for (const auto &it : p->items)
        {
            res += " ";
            switch (it.kind)
            {
                case PrintItem::Kind::Expr:
                    res += dump(*it.expr);
                    break;
                case PrintItem::Kind::Comma:
                    res += ",";
                    break;
                case PrintItem::Kind::Semicolon:
                    res += ";";
                    break;
            }
        }
        res += ")";
        return res;
    }
    else if (auto *l = dynamic_cast<const LetStmt *>(&stmt))
    {
        return "(LET " + dump(*l->target) + " " + dump(*l->expr) + ")";
    }
    else if (auto *d = dynamic_cast<const DimStmt *>(&stmt))
    {
        return "(DIM " + d->name + " " + dump(*d->size) + ")";
    }
    else if (auto *r = dynamic_cast<const RandomizeStmt *>(&stmt))
    {
        return "(RANDOMIZE " + dump(*r->seed) + ")";
    }
    else if (auto *i = dynamic_cast<const IfStmt *>(&stmt))
    {
        std::string res = "(IF " + dump(*i->cond) + " THEN " + dump(*i->then_branch);
        for (const auto &e : i->elseifs)
        {
            res += " ELSEIF " + dump(*e.cond) + " THEN " + dump(*e.then_branch);
        }
        if (i->else_branch)
            res += " ELSE " + dump(*i->else_branch);
        res += ")";
        return res;
    }
    else if (auto *w = dynamic_cast<const WhileStmt *>(&stmt))
    {
        std::string res = "(WHILE " + dump(*w->cond) + " {";
        bool first = true;
        for (auto &s : w->body)
        {
            if (!first)
                res += " ";
            first = false;
            res += std::to_string(s->line) + ":" + dump(*s);
        }
        res += "})";
        return res;
    }
    else if (auto *f = dynamic_cast<const ForStmt *>(&stmt))
    {
        std::string res = "(FOR " + f->var + " = " + dump(*f->start) + " TO " + dump(*f->end);
        if (f->step)
            res += " STEP " + dump(*f->step);
        res += " {";
        bool first = true;
        for (auto &s : f->body)
        {
            if (!first)
                res += " ";
            first = false;
            res += std::to_string(s->line) + ":" + dump(*s);
        }
        res += "})";
        return res;
    }
    else if (auto *n = dynamic_cast<const NextStmt *>(&stmt))
    {
        return "(NEXT " + n->var + ")";
    }
    else if (auto *g = dynamic_cast<const GotoStmt *>(&stmt))
    {
        return "(GOTO " + std::to_string(g->target) + ")";
    }
    else if (dynamic_cast<const EndStmt *>(&stmt))
    {
        return "(END)";
    }
    return "(?)";
}

std::string AstPrinter::dump(const Expr &expr)
{
    if (auto *i = dynamic_cast<const IntExpr *>(&expr))
    {
        return std::to_string(i->value);
    }
    else if (auto *f = dynamic_cast<const FloatExpr *>(&expr))
    {
        std::ostringstream os;
        os << f->value;
        return os.str();
    }
    else if (auto *s = dynamic_cast<const StringExpr *>(&expr))
    {
        return std::string("\"") + s->value + "\"";
    }
    else if (auto *v = dynamic_cast<const VarExpr *>(&expr))
    {
        return v->name;
    }
    else if (auto *b = dynamic_cast<const BinaryExpr *>(&expr))
    {
        const char *op = "?";
        switch (b->op)
        {
            case BinaryExpr::Op::Add:
                op = "+";
                break;
            case BinaryExpr::Op::Sub:
                op = "-";
                break;
            case BinaryExpr::Op::Mul:
                op = "*";
                break;
            case BinaryExpr::Op::Div:
                op = "/";
                break;
            case BinaryExpr::Op::IDiv:
                op = "\\";
                break;
            case BinaryExpr::Op::Mod:
                op = "MOD";
                break;
            case BinaryExpr::Op::Eq:
                op = "=";
                break;
            case BinaryExpr::Op::Ne:
                op = "<>";
                break;
            case BinaryExpr::Op::Lt:
                op = "<";
                break;
            case BinaryExpr::Op::Le:
                op = "<=";
                break;
            case BinaryExpr::Op::Gt:
                op = ">";
                break;
            case BinaryExpr::Op::Ge:
                op = ">=";
                break;
            case BinaryExpr::Op::And:
                op = "AND";
                break;
            case BinaryExpr::Op::Or:
                op = "OR";
                break;
        }
        return std::string("(") + op + " " + dump(*b->lhs) + " " + dump(*b->rhs) + ")";
    }
    else if (auto *u = dynamic_cast<const UnaryExpr *>(&expr))
    {
        return "(NOT " + dump(*u->expr) + ")";
    }
    else if (auto *c = dynamic_cast<const CallExpr *>(&expr))
    {
        std::string name;
        switch (c->builtin)
        {
            case CallExpr::Builtin::Len:
                name = "LEN";
                break;
            case CallExpr::Builtin::Mid:
                name = "MID$";
                break;
            case CallExpr::Builtin::Left:
                name = "LEFT$";
                break;
            case CallExpr::Builtin::Right:
                name = "RIGHT$";
                break;
            case CallExpr::Builtin::Str:
                name = "STR$";
                break;
            case CallExpr::Builtin::Val:
                name = "VAL";
                break;
            case CallExpr::Builtin::Int:
                name = "INT";
                break;
            case CallExpr::Builtin::Sqr:
                name = "SQR";
                break;
            case CallExpr::Builtin::Abs:
                name = "ABS";
                break;
            case CallExpr::Builtin::Floor:
                name = "FLOOR";
                break;
            case CallExpr::Builtin::Ceil:
                name = "CEIL";
                break;
            case CallExpr::Builtin::Sin:
                name = "SIN";
                break;
            case CallExpr::Builtin::Cos:
                name = "COS";
                break;
            case CallExpr::Builtin::Pow:
                name = "POW";
                break;
            case CallExpr::Builtin::Rnd:
                name = "RND";
                break;
        }
        std::string res = "(" + name;
        for (auto &a : c->args)
            res += " " + dump(*a);
        res += ")";
        return res;
    }
    else if (auto *a = dynamic_cast<const ArrayExpr *>(&expr))
    {
        return a->name + "(" + dump(*a->index) + ")";
    }
    return "?";
}

} // namespace il::frontends::basic
