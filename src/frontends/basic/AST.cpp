#include "frontends/basic/AST.h"

namespace il::basic {

BinaryExpr::BinaryExpr(std::string o, std::unique_ptr<Expr> l,
                       std::unique_ptr<Expr> r, support::SourceLoc loc)
    : op(std::move(o)), lhs(std::move(l)), rhs(std::move(r)) {
  kind = Kind::Binary;
  this->loc = loc;
}

CallExpr::CallExpr(std::string n, std::vector<std::unique_ptr<Expr>> a,
                   support::SourceLoc l)
    : name(std::move(n)), args(std::move(a)) {
  kind = Kind::Call;
  loc = l;
}

PrintStmt::PrintStmt(std::unique_ptr<Expr> e, int ln, support::SourceLoc l)
    : expr(std::move(e)) {
  kind = Kind::Print;
  line = ln;
  loc = l;
}

LetStmt::LetStmt(std::string n, std::unique_ptr<Expr> e, int ln,
                 support::SourceLoc l)
    : name(std::move(n)), expr(std::move(e)) {
  kind = Kind::Let;
  line = ln;
  loc = l;
}

IfStmt::IfStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> t,
               std::unique_ptr<Stmt> e, int ln, support::SourceLoc l)
    : cond(std::move(c)), then_branch(std::move(t)), else_branch(std::move(e)) {
  kind = Kind::If;
  line = ln;
  loc = l;
}

WhileStmt::WhileStmt(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Stmt>> b,
                     int ln, support::SourceLoc l)
    : cond(std::move(c)), body(std::move(b)) {
  kind = Kind::While;
  line = ln;
  loc = l;
}

GotoStmt::GotoStmt(int tgt, int ln, support::SourceLoc l) : target(tgt) {
  kind = Kind::Goto;
  line = ln;
  loc = l;
}

EndStmt::EndStmt(int ln, support::SourceLoc l) {
  kind = Kind::End;
  line = ln;
  loc = l;
}

} // namespace il::basic
