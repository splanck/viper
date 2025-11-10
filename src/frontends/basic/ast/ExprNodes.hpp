// File: src/frontends/basic/ast/ExprNodes.hpp
// Purpose: Defines BASIC expression nodes and visitors for the front-end AST.
// Key invariants: Expressions retain source locations for diagnostics.
// Ownership/Lifetime: Nodes are owned via std::unique_ptr managed by callers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"
#include "support/source_location.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace il::frontends::basic
{

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
    virtual void visit(const NewExpr &) = 0;
    virtual void visit(const MeExpr &) = 0;
    virtual void visit(const MemberAccessExpr &) = 0;
    virtual void visit(const MethodCallExpr &) = 0;
    virtual void visit(const IsExpr &) = 0;
    virtual void visit(const AsExpr &) = 0;
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
    virtual void visit(NewExpr &) = 0;
    virtual void visit(MeExpr &) = 0;
    virtual void visit(MemberAccessExpr &) = 0;
    virtual void visit(MethodCallExpr &) = 0;
    virtual void visit(IsExpr &) = 0;
    virtual void visit(AsExpr &) = 0;
};

/// @brief Base class for all BASIC expressions.
struct Expr
{
    /// Source location of the expression in the source file.
    il::support::SourceLoc loc{};
    virtual ~Expr() = default;
    /// @brief Accept a visitor to process this expression.
    virtual void accept(ExprVisitor &visitor) const = 0;
    /// @brief Accept a mutable visitor to process this expression.
    virtual void accept(MutExprVisitor &visitor) = 0;
};

/// @brief Signed integer literal expression.
struct IntExpr : Expr
{
    /// Literal 64-bit numeric value parsed from the source.
    std::int64_t value{0};
    /// Optional BASIC suffix enforcing INTEGER or LONG semantics.
    enum class Suffix
    {
        None,
        Integer,
        Long,
    } suffix{Suffix::None};
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Floating-point literal expression.
struct FloatExpr : Expr
{
    /// Literal double-precision value parsed from the source.
    double value{0.0};
    /// Optional BASIC suffix distinguishing SINGLE from DOUBLE.
    enum class Suffix
    {
        None,
        Single,
        Double,
    } suffix{Suffix::None};
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
    bool value{false};
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

/// @brief Unary expression (e.g., NOT, unary plus/minus).
struct UnaryExpr : Expr
{
    /// Unary operator applied to @ref expr.
    enum class Op
    {
        LogicalNot,
        Plus,
        Negate,
    } op{};

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
    } op{};

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
        Asc,
        InKey,
        GetKey,
        Eof,
        Lof,
        Loc,
        Timer
    } builtin{Builtin::Len};

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
    il::support::SourceLoc loc{};
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Allocate a new instance of a class.
struct NewExpr : Expr
{
    /// Name of the class type to instantiate.
    std::string className;

    /// Arguments passed to the constructor.
    std::vector<ExprPtr> args;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Reference to the receiver instance inside methods.
struct MeExpr : Expr
{
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Access a member field on an object.
struct MemberAccessExpr : Expr
{
    /// Base expression evaluating to an object.
    ExprPtr base;

    /// Member field being accessed.
    std::string member;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Invoke a method on an object instance.
struct MethodCallExpr : Expr
{
    /// Base expression evaluating to the receiver instance.
    ExprPtr base;

    /// Method name to invoke.
    std::string method;

    /// Arguments passed to the method call.
    std::vector<ExprPtr> args;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Runtime type check expression: `value IS Type.Name`.
struct IsExpr : Expr
{
    /// Value being tested.
    ExprPtr value;
    /// Dotted type name segments.
    std::vector<std::string> typeName;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Type ascription/cast expression: `value AS Type.Name`.
struct AsExpr : Expr
{
    /// Value being cast.
    ExprPtr value;
    /// Dotted type name segments.
    std::vector<std::string> typeName;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

} // namespace il::frontends::basic
