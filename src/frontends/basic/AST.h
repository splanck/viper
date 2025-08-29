#pragma once
#include <memory>
#include <string>
#include <vector>
#include "support/source_manager.h"

namespace il::basic {

struct Expr {
  enum class Kind { Int, String, Var, Binary, Call };
  Kind kind;
  support::SourceLoc loc;
  virtual ~Expr() = default;
};

struct IntExpr : Expr {
  int value;
  explicit IntExpr(int v, support::SourceLoc l) : value(v) { kind = Kind::Int; loc = l; }
};

struct StringExpr : Expr {
  std::string value;
  StringExpr(std::string v, support::SourceLoc l) : value(std::move(v)) { kind = Kind::String; loc = l; }
};

struct VarExpr : Expr {
  std::string name;
  VarExpr(std::string n, support::SourceLoc l) : name(std::move(n)) { kind = Kind::Var; loc = l; }
};

struct BinaryExpr : Expr {
  std::string op;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  BinaryExpr(std::string o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, support::SourceLoc loc);
};

struct CallExpr : Expr {
  std::string name;
  std::vector<std::unique_ptr<Expr>> args;
  CallExpr(std::string n, std::vector<std::unique_ptr<Expr>> a, support::SourceLoc l);
};

struct Stmt {
  enum class Kind { Print, Let, If, While, Goto, End };
  Kind kind;
  int line = 0;
  support::SourceLoc loc;
  virtual ~Stmt() = default;
};

struct PrintStmt : Stmt {
  std::unique_ptr<Expr> expr;
  PrintStmt(std::unique_ptr<Expr> e, int ln, support::SourceLoc l);
};

struct LetStmt : Stmt {
  std::string name;
  std::unique_ptr<Expr> expr;
  LetStmt(std::string n, std::unique_ptr<Expr> e, int ln, support::SourceLoc l);
};

struct IfStmt : Stmt {
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> then_branch;
  std::unique_ptr<Stmt> else_branch;
  IfStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> t, std::unique_ptr<Stmt> e, int ln, support::SourceLoc l);
};

struct WhileStmt : Stmt {
  std::unique_ptr<Expr> cond;
  std::vector<std::unique_ptr<Stmt>> body;
  WhileStmt(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Stmt>> b, int ln, support::SourceLoc l);
};

struct GotoStmt : Stmt {
  int target;
  GotoStmt(int tgt, int ln, support::SourceLoc l);
};

struct EndStmt : Stmt {
  EndStmt(int ln, support::SourceLoc l);
};

struct Program {
  std::vector<std::unique_ptr<Stmt>> statements;
};

} // namespace il::basic
