// File: src/frontends/basic/AST.hpp
// Purpose: Declares BASIC front-end abstract syntax tree nodes.
// Key invariants: Nodes carry source locations.
// Ownership/Lifetime: Caller owns nodes via std::unique_ptr.
// Links: docs/class-catalog.md
#pragma once

#include "support/source_manager.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace il::frontends::basic
{

/// @brief Base class for all BASIC expressions.
struct Expr
{
    /// Source location of the expression in the source file.
    il::support::SourceLoc loc;
    virtual ~Expr() = default;
};

using ExprPtr = std::unique_ptr<Expr>;

using Identifier = std::string;

/// @brief BASIC primitive types.
enum class Type
{
    I64,
    F64,
    Str,
};

/// @brief Signed integer literal expression.
struct IntExpr : Expr
{
    /// Literal 64-bit numeric value parsed from the source.
    int64_t value;
};

/// @brief Floating-point literal expression.
struct FloatExpr : Expr
{
    /// Literal double-precision value parsed from the source.
    double value;
};

/// @brief String literal expression.
struct StringExpr : Expr
{
    /// Owned UTF-8 string contents without surrounding quotes.
    std::string value;
};

/// @brief Reference to a scalar variable.
struct VarExpr : Expr
{
    /// Variable name including optional type suffix.
    std::string name;
};

/// @brief Array element access A(i).
struct ArrayExpr : Expr
{
    /// Name of the array variable being indexed.
    std::string name;
    /// Zero-based index expression; owned and non-null.
    ExprPtr index;
};

/// @brief Unary expression (e.g., NOT).
struct UnaryExpr : Expr
{
    /// Unary operator applied to @ref expr.
    enum class Op
    {
        Not
    } op;

    /// Operand expression; owned and non-null.
    ExprPtr expr;
};

/// @brief Binary expression combining two operands.
struct BinaryExpr : Expr
{
    /// Binary operator applied to @ref lhs and @ref rhs.
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

    /// Left-hand operand expression; owned and non-null.
    ExprPtr lhs;

    /// Right-hand operand expression; owned and non-null.
    ExprPtr rhs;
};

/// @brief Call to a BASIC builtin function.
struct BuiltinCallExpr : Expr
{
    /// Which builtin function to invoke.
    enum class Builtin
    {
        Len,
        Mid,
        Left,
        Right,
        Str,
        Val,
        Int,
        Sqr,
        Abs,
        Floor,
        Ceil,
        Sin,
        Cos,
        Pow,
        Rnd,
        Instr,
        Ltrim,
        Rtrim,
        Trim,
        Ucase,
        Lcase,
        Chr,
        Asc
    } builtin;

    /// Argument expressions passed to the builtin; owned.
    std::vector<ExprPtr> args;
};

/// @brief Call to user-defined FUNCTION or SUB.
struct CallExpr : Expr
{
    /// Procedure name to invoke.
    Identifier callee;

    /// Ordered argument expressions; owned.
    std::vector<ExprPtr> args;

    /// Source location of the call operator.
    il::support::SourceLoc loc;
};

/// @brief Base class for all BASIC statements.
struct Stmt
{
    /// BASIC line number associated with this statement.
    int line = 0;

    /// Source location of the first token in the statement.
    il::support::SourceLoc loc;

    virtual ~Stmt() = default;
};

using StmtPtr = std::unique_ptr<Stmt>;
/// Either FunctionDecl or SubDecl.
using ProcDecl = StmtPtr;

/// @brief Item within a PRINT statement.
struct PrintItem
{
    /// Kind of item to output.
    enum class Kind
    {
        Expr,      ///< Expression to print.
        Comma,     ///< Insert a space.
        Semicolon, ///< Insert nothing.
    } kind = Kind::Expr;

    /// Expression value when @ref kind == Kind::Expr; owned.
    ExprPtr expr;
};

/// @brief PRINT statement outputting a sequence of expressions and separators.
/// Trailing semicolon suppresses the automatic newline.
/// @invariant items.size() > 0
struct PrintStmt : Stmt
{
    /// Items printed in order; unless the last item is a semicolon, a newline is appended.
    std::vector<PrintItem> items;
};

/// @brief Assignment statement to variable or array element.
struct LetStmt : Stmt
{
    /// Variable or ArrayExpr on the left-hand side; owned.
    ExprPtr target;

    /// Value expression to store; owned and non-null.
    ExprPtr expr;
};

/// @brief DIM statement allocating array storage.
struct DimStmt : Stmt
{
    /// Array name being declared.
    std::string name;

    /// Number of elements to allocate; owned expression, non-null.
    ExprPtr size;
};

/// @brief RANDOMIZE statement seeding the pseudo-random generator.
struct RandomizeStmt : Stmt
{
    /// Numeric seed expression, truncated to i64; owned and non-null.
    ExprPtr seed;
};

/// @brief IF statement with optional ELSEIF chain and ELSE branch.
struct IfStmt : Stmt
{
    /// @brief ELSEIF arm.
    struct ElseIf
    {
        /// Condition expression controlling this arm; owned and non-null.
        ExprPtr cond;

        /// Executed when @ref cond evaluates to true; owned and non-null.
        StmtPtr then_branch;
    };

    /// Initial IF condition; owned and non-null.
    ExprPtr cond;

    /// THEN branch when @ref cond is true; owned and non-null.
    StmtPtr then_branch;

    /// Zero or more ELSEIF arms evaluated in order.
    std::vector<ElseIf> elseifs;

    /// Optional trailing ELSE branch (may be null) executed when no condition matched.
    StmtPtr else_branch;
};

/// @brief WHILE ... WEND loop statement.
struct WhileStmt : Stmt
{
    /// Loop continuation condition; owned and non-null.
    ExprPtr cond;

    /// Body statements executed while @ref cond is true.
    std::vector<StmtPtr> body;
};

/// @brief FOR ... NEXT loop statement.
struct ForStmt : Stmt
{
    /// Loop variable name controlling the iteration.
    std::string var;

    /// Initial value assigned to @ref var; owned and non-null.
    ExprPtr start;

    /// Loop end value; owned and non-null.
    ExprPtr end;

    /// Optional step expression; null means 1.
    ExprPtr step;

    /// Body statements executed each iteration.
    std::vector<StmtPtr> body;
};

/// @brief NEXT statement closing a FOR.
struct NextStmt : Stmt
{
    /// Loop variable after NEXT.
    std::string var;
};

/// @brief GOTO statement transferring control to a line number.
struct GotoStmt : Stmt
{
    /// Target line number to jump to.
    int target;
};

/// @brief END statement terminating program execution.
struct EndStmt : Stmt
{
};

/// @brief INPUT statement to read from stdin into a variable, optionally
/// displaying a string literal prompt.
struct InputStmt : Stmt
{
    /// Optional prompt string literal (nullptr if absent).
    ExprPtr prompt;

    /// Target variable name (may end with '$').
    std::string var;
};

/// @brief RETURN statement optionally yielding a value.
struct ReturnStmt : Stmt
{
    /// Expression whose value is returned; null when no expression is provided.
    ExprPtr value;
};

/// @brief Parameter in FUNCTION or SUB declaration.
struct Param
{
    /// Parameter name including optional suffix.
    Identifier name;

    /// Resolved type from suffix.
    Type type = Type::I64;

    /// True if parameter declared with ().
    bool is_array = false;

    /// Source location of the parameter name.
    il::support::SourceLoc loc;
};

/// @brief FUNCTION declaration with optional parameters and return type.
struct FunctionDecl : Stmt
{
    /// Function name including suffix.
    Identifier name;

    /// Return type derived from name suffix.
    Type ret = Type::I64;

    /// Ordered parameter list.
    std::vector<Param> params;

    /// Function body statements.
    std::vector<StmtPtr> body;

    /// Location of trailing END FUNCTION keyword.
    il::support::SourceLoc endLoc;
};

/// @brief SUB declaration representing a void procedure.
struct SubDecl : Stmt
{
    /// Subroutine name including suffix.
    Identifier name;

    /// Ordered parameter list.
    std::vector<Param> params;

    /// Body statements.
    std::vector<StmtPtr> body;
};

/// @brief Sequence of statements executed left-to-right on one BASIC line.
struct StmtList : Stmt
{
    /// Ordered statements sharing the same line.
    std::vector<StmtPtr> stmts;
};

/// @brief Root node partitioning procedure declarations from main statements.
struct Program
{
    /// FUNCTION/SUB declarations in order.
    std::vector<ProcDecl> procs;

    /// Top-level statements forming program entry.
    std::vector<StmtPtr> main;

    /// Location of first token in source.
    il::support::SourceLoc loc;
};

} // namespace il::frontends::basic
