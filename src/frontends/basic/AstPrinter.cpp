#include "frontends/basic/AstPrinter.h"
#include <iomanip>
#include <sstream>

namespace il::basic {

static void printStmtNoLine(const Stmt *s, std::ostream &os) {
  AstPrinter::printStmt(s, os, false);
}

static void printExprWrap(const Expr *e, std::ostream &os) {
  AstPrinter::printExpr(e, os);
}

void AstPrinter::printExpr(const Expr *e, std::ostream &os) {
  switch (e->kind) {
  case Expr::Kind::Int:
    os << "Int(" << static_cast<const IntExpr*>(e)->value << ")";
    break;
  case Expr::Kind::String:
    os << "Str(" << std::quoted(static_cast<const StringExpr*>(e)->value) << ")";
    break;
  case Expr::Kind::Var:
    os << "Var(" << static_cast<const VarExpr*>(e)->name << ")";
    break;
  case Expr::Kind::Binary: {
    auto *b = static_cast<const BinaryExpr*>(e);
    os << "Bin(" << b->op << ", ";
    printExprWrap(b->lhs.get(), os);
    os << ", ";
    printExprWrap(b->rhs.get(), os);
    os << ")";
    break;
  }
  case Expr::Kind::Call: {
    auto *c = static_cast<const CallExpr*>(e);
    os << "Call(" << c->name;
    for (size_t i = 0; i < c->args.size(); ++i) {
      os << (i == 0 ? ", " : ", ");
      printExprWrap(c->args[i].get(), os);
    }
    os << ")";
    break;
  }
  }
}

void AstPrinter::printStmt(const Stmt *s, std::ostream &os, bool include_line) {
  if (include_line) {
    os << s->line << ':';
  }
  switch (s->kind) {
  case Stmt::Kind::Print:
    os << "PRINT(";
    printExprWrap(static_cast<const PrintStmt*>(s)->expr.get(), os);
    os << ')';
    break;
  case Stmt::Kind::Let: {
    auto *l = static_cast<const LetStmt*>(s);
    os << "LET(" << l->name << ", ";
    printExprWrap(l->expr.get(), os);
    os << ')';
    break;
  }
  case Stmt::Kind::If: {
    auto *i = static_cast<const IfStmt*>(s);
    os << "IF(";
    printExprWrap(i->cond.get(), os);
    os << ", ";
    printStmtNoLine(i->then_branch.get(), os);
    if (i->else_branch) {
      os << ", ";
      printStmtNoLine(i->else_branch.get(), os);
    }
    os << ')';
    break;
  }
  case Stmt::Kind::While: {
    auto *w = static_cast<const WhileStmt*>(s);
    os << "WHILE(";
    printExprWrap(w->cond.get(), os);
    os << ")[";
    for (size_t idx = 0; idx < w->body.size(); ++idx) {
      if (idx) os << ' ';
      printStmt(w->body[idx].get(), os, true);
      if (idx + 1 < w->body.size()) os << ';';
    }
    os << ']';
    break;
  }
  case Stmt::Kind::Goto: {
    auto *g = static_cast<const GotoStmt*>(s);
    os << "GOTO(" << g->target << ')';
    break;
  }
  case Stmt::Kind::End:
    os << "END";
    break;
  }
}

std::string AstPrinter::print(const Program &prog) {
  std::ostringstream oss;
  for (size_t i = 0; i < prog.statements.size(); ++i) {
    printStmt(prog.statements[i].get(), oss, true);
    if (i + 1 < prog.statements.size()) oss << '\n';
  }
  return oss.str();
}

} // namespace il::basic
