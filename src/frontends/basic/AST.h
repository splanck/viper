#pragma once
#include "support/source_manager.h"
#include <memory>
#include <string>
#include <vector>

namespace il::frontends::basic {

struct Expr {
  il::support::SourceLoc loc;
  virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntExpr : Expr {
  int value;
};
struct StringExpr : Expr {
  std::string value;
};
struct VarExpr : Expr {
  std::string name;
};

struct BinaryExpr : Expr {
  enum class Op {
    Add,
    Sub,
    Mul,
    Div,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
  } op;
  ExprPtr lhs;
  ExprPtr rhs;
};

struct CallExpr : Expr {
  enum class Builtin { Len, Mid } builtin;
  std::vector<ExprPtr> args;
};

struct Stmt {
  int line = 0;
  il::support::SourceLoc loc;
  virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct PrintStmt : Stmt {
  ExprPtr expr;
};
struct LetStmt : Stmt {
  std::string name;
  ExprPtr expr;
};
struct IfStmt : Stmt {
  ExprPtr cond;
  StmtPtr then_branch;
  StmtPtr else_branch; // may be null
};
struct WhileStmt : Stmt {
  ExprPtr cond;
  std::vector<StmtPtr> body;
};
struct GotoStmt : Stmt {
  int target;
};
struct EndStmt : Stmt {};

struct Program {
  std::vector<StmtPtr> statements;
};

} // namespace il::frontends::basic
