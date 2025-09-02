// File: src/frontends/basic/AST.hpp
// Purpose: Declares BASIC front-end abstract syntax tree nodes.
// Key invariants: Nodes carry source locations.
// Ownership/Lifetime: Caller owns nodes via std::unique_ptr.
// Links: docs/class-catalog.md
#pragma once

#include "support/source_manager.hpp"
#include <memory>
#include <string>
#include <vector>

namespace il::frontends::basic
{

struct Expr
{
    il::support::SourceLoc loc;
    virtual ~Expr() = default;
};

using ExprPtr = std::unique_ptr<Expr>;

struct IntExpr : Expr
{
    int value;
};

struct StringExpr : Expr
{
    std::string value;
};

struct VarExpr : Expr
{
    std::string name;
};

/// @brief Array element access A(i).
struct ArrayExpr : Expr
{
    std::string name; ///< Array variable name.
    ExprPtr index;    ///< Index expression (0-based).
};

/// @brief Unary expression (e.g., NOT).
struct UnaryExpr : Expr
{
    /// @brief Unary operators supported.
    enum class Op
    {
        Not
    } op;
    /// @brief Operand expression.
    ExprPtr expr;
};

struct BinaryExpr : Expr
{
    enum class Op
    {
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

struct CallExpr : Expr
{
    enum class Builtin
    {
        Len,
        Mid
    } builtin;
    std::vector<ExprPtr> args;
};

struct Stmt
{
    int line = 0;
    il::support::SourceLoc loc;
    virtual ~Stmt() = default;
};

using StmtPtr = std::unique_ptr<Stmt>;

/// @brief PRINT statement outputting expressions separated by spaces.
/// @invariant items.size() > 0
struct PrintStmt : Stmt
{
    std::vector<ExprPtr> items; ///< Expressions to print in order.
};

/// @brief Assignment statement to variable or array element.
struct LetStmt : Stmt
{
    ExprPtr target; ///< Variable or ArrayExpr on the left-hand side.
    ExprPtr expr;   ///< Value expression to store.
};

/// @brief DIM statement allocating array storage.
struct DimStmt : Stmt
{
    std::string name; ///< Array name.
    ExprPtr size;     ///< Element count expression.
};

struct IfStmt : Stmt
{
    ExprPtr cond;
    StmtPtr then_branch;
    StmtPtr else_branch; // may be null
};

struct WhileStmt : Stmt
{
    ExprPtr cond;
    std::vector<StmtPtr> body;
};

/// @brief FOR ... NEXT loop statement.
struct ForStmt : Stmt
{
    std::string var;           ///< Loop variable name.
    ExprPtr start;             ///< Initial value.
    ExprPtr end;               ///< Loop end value.
    ExprPtr step;              ///< Optional step expression; null means 1.
    std::vector<StmtPtr> body; ///< Body statements executed each iteration.
};

/// @brief NEXT statement closing a FOR.
struct NextStmt : Stmt
{
    std::string var; ///< Loop variable after NEXT.
};

struct GotoStmt : Stmt
{
    int target;
};

struct EndStmt : Stmt
{
};

/// @brief INPUT statement to read from stdin into a variable.
struct InputStmt : Stmt
{
    std::string var; ///< Target variable name (may end with '$').
};

struct Program
{
    std::vector<StmtPtr> statements;
};

} // namespace il::frontends::basic
