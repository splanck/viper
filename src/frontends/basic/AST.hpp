// File: src/frontends/basic/AST.hpp
// Purpose: Declares BASIC front-end abstract syntax tree nodes.
// Key invariants: Nodes carry source locations.
// Ownership/Lifetime: Caller owns nodes via std::unique_ptr.
// Links: docs/codemap.md
#pragma once

#include "support/source_location.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace il::frontends::basic
{

struct IntExpr;
struct FloatExpr;
struct StringExpr;
struct BoolExpr;
struct VarExpr;
struct ArrayExpr;
struct UnaryExpr;
struct BinaryExpr;
struct BuiltinCallExpr;
struct LBoundExpr;
struct UBoundExpr;
struct CallExpr;

struct PrintStmt;
struct LetStmt;
struct DimStmt;
struct ReDimStmt;
struct RandomizeStmt;
struct IfStmt;
struct WhileStmt;
struct DoStmt;
struct ForStmt;
struct NextStmt;
struct ExitStmt;
struct GotoStmt;
struct EndStmt;
struct OpenStmt;
struct CloseStmt;
struct InputStmt;
struct ReturnStmt;
struct OnErrorGoto;
struct Resume;
struct FunctionDecl;
struct SubDecl;
struct StmtList;

/// @brief Visitor interface for BASIC expressions.
struct ExprVisitor
{
    virtual ~ExprVisitor() = default;
    virtual void visit(const IntExpr &) = 0;
    virtual void visit(const FloatExpr &) = 0;
    virtual void visit(const StringExpr &) = 0;
    virtual void visit(const BoolExpr &) = 0;
    virtual void visit(const VarExpr &) = 0;
    virtual void visit(const ArrayExpr &) = 0;
    virtual void visit(const UnaryExpr &) = 0;
    virtual void visit(const BinaryExpr &) = 0;
    virtual void visit(const BuiltinCallExpr &) = 0;
    virtual void visit(const LBoundExpr &) = 0;
    virtual void visit(const UBoundExpr &) = 0;
    virtual void visit(const CallExpr &) = 0;
};

/// @brief Visitor interface for mutable BASIC expressions.
struct MutExprVisitor
{
    virtual ~MutExprVisitor() = default;
    virtual void visit(IntExpr &) = 0;
    virtual void visit(FloatExpr &) = 0;
    virtual void visit(StringExpr &) = 0;
    virtual void visit(BoolExpr &) = 0;
    virtual void visit(VarExpr &) = 0;
    virtual void visit(ArrayExpr &) = 0;
    virtual void visit(UnaryExpr &) = 0;
    virtual void visit(BinaryExpr &) = 0;
    virtual void visit(BuiltinCallExpr &) = 0;
    virtual void visit(LBoundExpr &) = 0;
    virtual void visit(UBoundExpr &) = 0;
    virtual void visit(CallExpr &) = 0;
};

/// @brief Visitor interface for BASIC statements.
struct StmtVisitor
{
    virtual ~StmtVisitor() = default;
    virtual void visit(const PrintStmt &) = 0;
    virtual void visit(const LetStmt &) = 0;
    virtual void visit(const DimStmt &) = 0;
    virtual void visit(const ReDimStmt &) = 0;
    virtual void visit(const RandomizeStmt &) = 0;
    virtual void visit(const IfStmt &) = 0;
    virtual void visit(const WhileStmt &) = 0;
    virtual void visit(const DoStmt &) = 0;
    virtual void visit(const ForStmt &) = 0;
    virtual void visit(const NextStmt &) = 0;
    virtual void visit(const ExitStmt &) = 0;
    virtual void visit(const GotoStmt &) = 0;
    virtual void visit(const OpenStmt &) = 0;
    virtual void visit(const CloseStmt &) = 0;
    virtual void visit(const OnErrorGoto &) = 0;
    virtual void visit(const Resume &) = 0;
    virtual void visit(const EndStmt &) = 0;
    virtual void visit(const InputStmt &) = 0;
    virtual void visit(const ReturnStmt &) = 0;
    virtual void visit(const FunctionDecl &) = 0;
    virtual void visit(const SubDecl &) = 0;
    virtual void visit(const StmtList &) = 0;
};

/// @brief Visitor interface for mutable BASIC statements.
struct MutStmtVisitor
{
    virtual ~MutStmtVisitor() = default;
    virtual void visit(PrintStmt &) = 0;
    virtual void visit(LetStmt &) = 0;
    virtual void visit(DimStmt &) = 0;
    virtual void visit(ReDimStmt &) = 0;
    virtual void visit(RandomizeStmt &) = 0;
    virtual void visit(IfStmt &) = 0;
    virtual void visit(WhileStmt &) = 0;
    virtual void visit(DoStmt &) = 0;
    virtual void visit(ForStmt &) = 0;
    virtual void visit(NextStmt &) = 0;
    virtual void visit(ExitStmt &) = 0;
    virtual void visit(GotoStmt &) = 0;
    virtual void visit(OpenStmt &) = 0;
    virtual void visit(CloseStmt &) = 0;
    virtual void visit(OnErrorGoto &) = 0;
    virtual void visit(Resume &) = 0;
    virtual void visit(EndStmt &) = 0;
    virtual void visit(InputStmt &) = 0;
    virtual void visit(ReturnStmt &) = 0;
    virtual void visit(FunctionDecl &) = 0;
    virtual void visit(SubDecl &) = 0;
    virtual void visit(StmtList &) = 0;
};

/// @brief Base class for all BASIC expressions.
struct Expr
{
    /// Source location of the expression in the source file.
    il::support::SourceLoc loc;
    virtual ~Expr() = default;
    /// @brief Accept a visitor to process this expression.
    virtual void accept(ExprVisitor &visitor) const = 0;
    /// @brief Accept a mutable visitor to process this expression.
    virtual void accept(MutExprVisitor &visitor) = 0;
};

using ExprPtr = std::unique_ptr<Expr>;

using Identifier = std::string;

/// @brief BASIC primitive types.
enum class Type
{
    I64,
    F64,
    Str,
    Bool,
};

/// @brief Signed integer literal expression.
struct IntExpr : Expr
{
    /// Literal 64-bit numeric value parsed from the source.
    int64_t value;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Floating-point literal expression.
struct FloatExpr : Expr
{
    /// Literal double-precision value parsed from the source.
    double value;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief String literal expression.
struct StringExpr : Expr
{
    /// Owned UTF-8 string contents without surrounding quotes.
    std::string value;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Boolean literal expression.
struct BoolExpr : Expr
{
    /// Literal boolean value parsed from the source.
    bool value = false;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Reference to a scalar variable.
struct VarExpr : Expr
{
    /// Variable name including optional type suffix.
    std::string name;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Array element access A(i).
struct ArrayExpr : Expr
{
    /// Name of the array variable being indexed.
    std::string name;
    /// Zero-based index expression; owned and non-null.
    ExprPtr index;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Query the logical lower bound of an array.
struct LBoundExpr : Expr
{
    /// Name of the array operand queried for its lower bound.
    std::string name;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Query the logical upper bound of an array.
struct UBoundExpr : Expr
{
    /// Name of the array operand queried for its upper bound.
    std::string name;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Unary expression (e.g., NOT).
struct UnaryExpr : Expr
{
    /// Unary operator applied to @ref expr.
    enum class Op
    {
        LogicalNot
    } op;

    /// Operand expression; owned and non-null.
    ExprPtr expr;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
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
        Pow,
        IDiv,
        Mod,
        Eq,
        Ne,
        Lt,
        Le,
        Gt,
        Ge,
        LogicalAndShort,
        LogicalOrShort,
        LogicalAnd,
        LogicalOr,
    } op;

    /// Left-hand operand expression; owned and non-null.
    ExprPtr lhs;

    /// Right-hand operand expression; owned and non-null.
    ExprPtr rhs;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
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
        Cint,
        Clng,
        Csng,
        Cdbl,
        Int,
        Fix,
        Round,
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
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
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
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Base class for all BASIC statements.
struct Stmt
{
    /// BASIC line number associated with this statement.
    int line = 0;

    /// Source location of the first token in the statement.
    il::support::SourceLoc loc;

    virtual ~Stmt() = default;
    /// @brief Accept a visitor to process this statement.
    virtual void accept(StmtVisitor &visitor) const = 0;
    /// @brief Accept a mutable visitor to process this statement.
    virtual void accept(MutStmtVisitor &visitor) = 0;
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
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief Assignment statement to variable or array element.
struct LetStmt : Stmt
{
    /// Variable or ArrayExpr on the left-hand side; owned.
    ExprPtr target;

    /// Value expression to store; owned and non-null.
    ExprPtr expr;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief DIM statement allocating array storage.
struct DimStmt : Stmt
{
    /// Array name being declared.
    std::string name;

    /// Number of elements to allocate when @ref isArray is true; may be null for scalars.
    ExprPtr size;

    /// Declared BASIC type for this DIM.
    Type type = Type::I64;

    /// True when DIM declares an array; false for scalar declarations.
    bool isArray = true;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief REDIM statement resizing an existing array.
struct ReDimStmt : Stmt
{
    /// Array name whose storage is being reallocated.
    std::string name;

    /// Number of elements in the resized array; owned and non-null.
    ExprPtr size;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief RANDOMIZE statement seeding the pseudo-random generator.
struct RandomizeStmt : Stmt
{
    /// Numeric seed expression, truncated to i64; owned and non-null.
    ExprPtr seed;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
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
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief WHILE ... WEND loop statement.
struct WhileStmt : Stmt
{
    /// Loop continuation condition; owned and non-null.
    ExprPtr cond;

    /// Body statements executed while @ref cond is true.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief DO ... LOOP statement supporting WHILE and UNTIL tests.
struct DoStmt : Stmt
{
    /// Condition kind controlling loop continuation.
    enum class CondKind
    {
        None,  ///< No explicit condition; loop runs until EXIT.
        While, ///< Continue while condition evaluates to true.
        Until, ///< Continue until condition evaluates to true.
    } condKind{CondKind::None};

    /// Whether condition is evaluated before or after executing the body.
    enum class TestPos
    {
        Pre,  ///< Evaluate condition before each iteration.
        Post, ///< Evaluate condition after executing the body.
    } testPos{TestPos::Pre};

    /// Continuation condition; null when @ref condKind == CondKind::None.
    ExprPtr cond;

    /// Ordered statements forming the loop body.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
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
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief NEXT statement closing a FOR.
struct NextStmt : Stmt
{
    /// Loop variable after NEXT.
    std::string var;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief EXIT statement leaving the innermost enclosing loop.
struct ExitStmt : Stmt
{
    /// Loop type targeted by this EXIT.
    enum class LoopKind
    {
        For,   ///< EXIT FOR
        While, ///< EXIT WHILE
        Do,    ///< EXIT DO
    } kind{LoopKind::While};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief GOTO statement transferring control to a line number.
struct GotoStmt : Stmt
{
    /// Target line number to jump to.
    int target;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief OPEN statement configuring a file channel.
struct OpenStmt : Stmt
{
    /// File path expression.
    ExprPtr pathExpr;

    /// Access mode requested for the channel.
    enum class Mode
    {
        Input = 0,  ///< OPEN ... FOR INPUT
        Output = 1, ///< OPEN ... FOR OUTPUT
        Append = 2, ///< OPEN ... FOR APPEND
        Binary = 3, ///< OPEN ... FOR BINARY
        Random = 4, ///< OPEN ... FOR RANDOM
    } mode{Mode::Input};

    /// Channel number expression that follows the '#'.
    ExprPtr channelExpr;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief CLOSE statement releasing a file channel.
struct CloseStmt : Stmt
{
    /// Channel number expression that follows the '#'.
    ExprPtr channelExpr;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief ON ERROR GOTO statement configuring error handler target.
struct OnErrorGoto : Stmt
{
    /// Destination line for error handler when @ref toZero is false.
    int target = 0;

    /// True when the statement uses "GOTO 0" to disable the handler.
    bool toZero = false;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief RESUME statement controlling error-handler resumption.
struct Resume : Stmt
{
    /// Resumption strategy following an error handler.
    enum class Mode
    {
        Same,  ///< Resume execution at the failing statement.
        Next,  ///< Resume at the statement following the failure site.
        Label, ///< Resume at a labeled line.
    } mode{Mode::Same};

    /// Target line label when @ref mode == Mode::Label.
    int target = 0;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief END statement terminating program execution.
struct EndStmt : Stmt
{
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief INPUT statement to read from stdin into a variable, optionally
/// displaying a string literal prompt.
struct InputStmt : Stmt
{
    /// Optional prompt string literal (nullptr if absent).
    ExprPtr prompt;

    /// Target variable name (may end with '$').
    std::string var;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief RETURN statement optionally yielding a value.
struct ReturnStmt : Stmt
{
    /// Expression whose value is returned; null when no expression is provided.
    ExprPtr value;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
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
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
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
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief Sequence of statements executed left-to-right on one BASIC line.
struct StmtList : Stmt
{
    /// Ordered statements sharing the same line.
    std::vector<StmtPtr> stmts;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
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
