// File: src/frontends/basic/AST.h
// Purpose: Declares BASIC front-end abstract syntax tree nodes.
// Key invariants: Nodes carry source locations.
// Ownership/Lifetime: Caller owns nodes via std::unique_ptr.
// Links: docs/class-catalog.md
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

/// @brief Unary expression (e.g., NOT).
struct UnaryExpr : Expr {
  /// @brief Unary operators supported.
  enum class Op { Not } op;
  /// @brief Operand expression.
  ExprPtr expr;
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
    And,
    Or,
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
/// @brief INPUT statement reading a line into a variable.
struct InputStmt : Stmt {
  std::string name; ///< Target variable name (NAME or NAME$).
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
/// @brief FOR ... NEXT loop statement.
struct ForStmt : Stmt {
  std::string var;           ///< Loop variable name.
  ExprPtr start;             ///< Initial value.
  ExprPtr end;               ///< Loop end value.
  ExprPtr step;              ///< Optional step expression; null means 1.
  std::vector<StmtPtr> body; ///< Body statements executed each iteration.
};
/// @brief NEXT statement closing a FOR.
struct NextStmt : Stmt {
  std::string var; ///< Loop variable after NEXT.
};
struct GotoStmt : Stmt {
  int target;
};
struct EndStmt : Stmt {};

struct Program {
  std::vector<StmtPtr> statements;
};

} // namespace il::frontends::basic
