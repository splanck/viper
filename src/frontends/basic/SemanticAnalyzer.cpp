// File: src/frontends/basic/SemanticAnalyzer.cpp
// Purpose: Implements BASIC semantic analyzer that collects symbols and labels.
// Key invariants: Analyzer mutates internal sets but emits no diagnostics yet.
// Ownership/Lifetime: Borrowed DiagnosticEngine; AST nodes owned externally.
// Links: docs/class-catalog.md
#include "frontends/basic/SemanticAnalyzer.h"
#include <functional>

namespace il::frontends::basic {

void SemanticAnalyzer::analyze(const Program &prog) {
  symbols_.clear();
  labels_.clear();
  labelRefs_.clear();
  for (const auto &stmt : prog.statements) {
    if (stmt)
      labels_.insert(stmt->line);
    if (stmt)
      visitStmt(*stmt);
  }
}

void SemanticAnalyzer::visitStmt(const Stmt &s) {
  if (auto *p = dynamic_cast<const PrintStmt *>(&s)) {
    if (p->expr)
      visitExpr(*p->expr);
  } else if (auto *l = dynamic_cast<const LetStmt *>(&s)) {
    symbols_.insert(l->name);
    if (l->expr)
      visitExpr(*l->expr);
  } else if (auto *i = dynamic_cast<const IfStmt *>(&s)) {
    if (i->cond)
      visitExpr(*i->cond);
    if (i->then_branch)
      visitStmt(*i->then_branch);
    if (i->else_branch)
      visitStmt(*i->else_branch);
  } else if (auto *w = dynamic_cast<const WhileStmt *>(&s)) {
    if (w->cond)
      visitExpr(*w->cond);
    for (const auto &bs : w->body)
      if (bs)
        visitStmt(*bs);
  } else if (auto *f = dynamic_cast<const ForStmt *>(&s)) {
    symbols_.insert(f->var);
    if (f->start)
      visitExpr(*f->start);
    if (f->end)
      visitExpr(*f->end);
    if (f->step)
      visitExpr(*f->step);
    for (const auto &bs : f->body)
      if (bs)
        visitStmt(*bs);
  } else if (auto *n = dynamic_cast<const NextStmt *>(&s)) {
    symbols_.insert(n->var);
  } else if (auto *g = dynamic_cast<const GotoStmt *>(&s)) {
    labelRefs_.insert(g->target);
  } else if (dynamic_cast<const EndStmt *>(&s)) {
    // nothing
  }
}

void SemanticAnalyzer::visitExpr(const Expr &e) {
  if (auto *v = dynamic_cast<const VarExpr *>(&e)) {
    symbols_.insert(v->name);
  } else if (auto *u = dynamic_cast<const UnaryExpr *>(&e)) {
    if (u->expr)
      visitExpr(*u->expr);
  } else if (auto *b = dynamic_cast<const BinaryExpr *>(&e)) {
    if (b->lhs)
      visitExpr(*b->lhs);
    if (b->rhs)
      visitExpr(*b->rhs);
  } else if (auto *c = dynamic_cast<const CallExpr *>(&e)) {
    for (const auto &a : c->args)
      if (a)
        visitExpr(*a);
  } else {
    // IntExpr, StringExpr etc. have no symbols
  }
}

} // namespace il::frontends::basic
