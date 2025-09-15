// File: src/frontends/basic/semantic/SymbolCollector.cpp
// Purpose: Implements BASIC symbol and label collection pass.
// Key invariants: Only definitions recorded; traversal covers nested statements.
// Ownership/Lifetime: Borrows DiagnosticEmitter; AST nodes owned externally.
// Links: docs/class-catalog.md

#include "frontends/basic/semantic/SymbolCollector.hpp"

using namespace il::frontends::basic;

namespace il::frontends::basic::semantic
{

void SymbolCollector::collect(const Program &prog)
{
    for (const auto &p : prog.procs)
    {
        if (auto *f = dynamic_cast<FunctionDecl *>(p.get()))
        {
            for (const auto &s : f->body)
                visitStmt(*s);
        }
        else if (auto *sb = dynamic_cast<SubDecl *>(p.get()))
        {
            for (const auto &s : sb->body)
                visitStmt(*s);
        }
    }
    for (const auto &s : prog.main)
        visitStmt(*s);
}

void SymbolCollector::visitStmt(const Stmt &s)
{
    labels_.insert(s.line);
    if (auto *l = dynamic_cast<const LetStmt *>(&s))
    {
        if (auto *v = dynamic_cast<VarExpr *>(l->target.get()))
            symbols_.insert(v->name);
    }
    else if (auto *g = dynamic_cast<const GotoStmt *>(&s))
    {
        labelRefs_.insert(g->target);
    }
    else if (auto *lst = dynamic_cast<const StmtList *>(&s))
    {
        for (const auto &st : lst->stmts)
            visitStmt(*st);
    }
    else if (auto *ifs = dynamic_cast<const IfStmt *>(&s))
    {
        visitStmt(*ifs->then_branch);
        for (const auto &ei : ifs->elseifs)
            visitStmt(*ei.then_branch);
        if (ifs->else_branch)
            visitStmt(*ifs->else_branch);
    }
    else if (auto *wh = dynamic_cast<const WhileStmt *>(&s))
    {
        for (const auto &st : wh->body)
            visitStmt(*st);
    }
    else if (auto *fs = dynamic_cast<const ForStmt *>(&s))
    {
        for (const auto &st : fs->body)
            visitStmt(*st);
    }
}

} // namespace il::frontends::basic::semantic

