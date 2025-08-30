// File: src/frontends/basic/AstPrinter.cpp
// Purpose: Implements BASIC AST printer for debugging.
// Key invariants: None.
// Ownership/Lifetime: Printer does not own AST nodes.
// Links: docs/class-catalog.md
#include "frontends/basic/AstPrinter.h"
#include <sstream>

namespace il::frontends::basic {

std::string AstPrinter::dump(const Program &prog) {
  std::string out;
  for (auto &stmt : prog.statements) {
    out += std::to_string(stmt->line) + ": " + dump(*stmt) + "\n";
  }
  return out;
}

std::string AstPrinter::dump(const Stmt &stmt) {
  if (auto *p = dynamic_cast<const PrintStmt *>(&stmt)) {
    return "(PRINT " + dump(*p->expr) + ")";
  } else if (auto *l = dynamic_cast<const LetStmt *>(&stmt)) {
    return "(LET " + l->name + " " + dump(*l->expr) + ")";
  } else if (auto *i = dynamic_cast<const IfStmt *>(&stmt)) {
    std::string res = "(IF " + dump(*i->cond) + " THEN " + dump(*i->then_branch);
    if (i->else_branch)
      res += " ELSE " + dump(*i->else_branch);
    res += ")";
    return res;
  } else if (auto *w = dynamic_cast<const WhileStmt *>(&stmt)) {
    std::string res = "(WHILE " + dump(*w->cond) + " {";
    bool first = true;
    for (auto &s : w->body) {
      if (!first)
        res += " ";
      first = false;
      res += std::to_string(s->line) + ":" + dump(*s);
    }
    res += "})";
    return res;
  } else if (auto *g = dynamic_cast<const GotoStmt *>(&stmt)) {
    return "(GOTO " + std::to_string(g->target) + ")";
  } else if (dynamic_cast<const EndStmt *>(&stmt)) {
    return "(END)";
  }
  return "(?)";
}

std::string AstPrinter::dump(const Expr &expr) {
  if (auto *i = dynamic_cast<const IntExpr *>(&expr)) {
    return std::to_string(i->value);
  } else if (auto *s = dynamic_cast<const StringExpr *>(&expr)) {
    return std::string("\"") + s->value + "\"";
  } else if (auto *v = dynamic_cast<const VarExpr *>(&expr)) {
    return v->name;
  } else if (auto *b = dynamic_cast<const BinaryExpr *>(&expr)) {
    const char *op = "?";
    switch (b->op) {
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
    }
    return std::string("(") + op + " " + dump(*b->lhs) + " " + dump(*b->rhs) + ")";
  } else if (auto *c = dynamic_cast<const CallExpr *>(&expr)) {
    std::string name = c->builtin == CallExpr::Builtin::Len ? "LEN" : "MID$";
    std::string res = "(" + name;
    for (auto &a : c->args)
      res += " " + dump(*a);
    res += ")";
    return res;
  }
  return "?";
}

} // namespace il::frontends::basic
