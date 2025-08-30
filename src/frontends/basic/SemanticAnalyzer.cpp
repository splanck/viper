// File: src/frontends/basic/SemanticAnalyzer.cpp
// Purpose: Implements BASIC semantic analyzer that collects symbols and labels
//          and validates variable usage.
// Key invariants: Symbol table reflects only definitions; unknown references
//                 produce diagnostics.
// Ownership/Lifetime: Borrowed DiagnosticEngine; AST nodes owned externally.
// Links: docs/class-catalog.md
#include "frontends/basic/SemanticAnalyzer.h"
#include <algorithm>
#include <limits>
#include <vector>

namespace il::frontends::basic {

namespace {
/// @brief Compute Levenshtein distance between strings @p a and @p b.
static size_t levenshtein(const std::string &a, const std::string &b) {
  const size_t m = a.size();
  const size_t n = b.size();
  std::vector<size_t> prev(n + 1), cur(n + 1);
  for (size_t j = 0; j <= n; ++j)
    prev[j] = j;
  for (size_t i = 1; i <= m; ++i) {
    cur[0] = i;
    for (size_t j = 1; j <= n; ++j) {
      size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    std::swap(prev, cur);
  }
  return prev[n];
}
} // namespace

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
  } else if (auto *g = dynamic_cast<const GotoStmt *>(&s)) {
    labelRefs_.insert(g->target);
  } else if (dynamic_cast<const EndStmt *>(&s)) {
    // nothing
  }
}

void SemanticAnalyzer::visitExpr(const Expr &e) {
  if (auto *v = dynamic_cast<const VarExpr *>(&e)) {
    if (!symbols_.count(v->name)) {
      std::string best;
      size_t bestDist = std::numeric_limits<size_t>::max();
      for (const auto &s : symbols_) {
        size_t d = levenshtein(v->name, s);
        if (d < bestDist) {
          bestDist = d;
          best = s;
        }
      }
      std::string msg = "B1001: unknown variable '" + v->name + "'";
      if (!best.empty())
        msg += "; did you mean '" + best + "'?";
      de.report({il::support::Severity::Error, std::move(msg), v->loc});
    }
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
