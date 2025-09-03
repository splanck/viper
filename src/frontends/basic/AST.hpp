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

struct FloatExpr : Expr
{
    double value;
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
        IDiv,
        Mod,
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
        Mid,
        Left,
        Right
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

/// @brief Item within a PRINT statement.
struct PrintItem
{
    /// @brief Kind of item to output.
    enum class Kind
    {
        Expr,      ///< Expression to print.
        Comma,     ///< Insert a space.
        Semicolon, ///< Insert nothing.
    } kind = Kind::Expr;

    /// @brief Expression value when @ref kind == Expr.
    ExprPtr expr;
};

/// @brief PRINT statement outputting a sequence of expressions and separators.
/// Trailing semicolon suppresses the automatic newline.
/// @invariant items.size() > 0
struct PrintStmt : Stmt
{
    std::vector<PrintItem> items; ///< Items printed in order; unless the last item is
                                  ///< a semicolon, a newline is appended.
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

/// @brief IF statement with optional ELSEIF chain and ELSE branch.
struct IfStmt : Stmt
{
    /// @brief ELSEIF arm.
    struct ElseIf
    {
        ExprPtr cond;        ///< Condition expression.
        StmtPtr then_branch; ///< Executed when condition is true.
    };

    ExprPtr cond;                ///< Initial IF condition.
    StmtPtr then_branch;         ///< THEN branch when @ref cond is true.
    std::vector<ElseIf> elseifs; ///< Zero or more ELSEIF arms evaluated in order.
    StmtPtr else_branch;         ///< Optional trailing ELSE branch (may be null).
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

/// @brief INPUT statement to read from stdin into a variable, optionally
/// displaying a string literal prompt.
struct InputStmt : Stmt
{
    ExprPtr prompt;  ///< Optional prompt string literal (nullptr if absent).
    std::string var; ///< Target variable name (may end with '$').
};

/// @brief Sequence of statements executed left-to-right on one BASIC line.
struct StmtList : Stmt
{
    std::vector<StmtPtr> stmts; ///< Ordered statements sharing the same line.
};

struct Program
{
    std::vector<StmtPtr> statements;
};

} // namespace il::frontends::basic
