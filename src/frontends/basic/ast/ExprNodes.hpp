//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ast/ExprNodes.hpp
// Purpose: Defines BASIC expression nodes and visitors for the front-end AST.
// Key invariants: Expressions retain source locations for diagnostics.
// Ownership/Lifetime: Nodes are owned via std::unique_ptr managed by callers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"
#include "support/source_location.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace il::frontends::basic
{

/// @brief Qualified identifier with dotted segments and source location.
struct QualifiedName
{
    std::vector<std::string> segments; ///< Dotted path in declaration order.
    il::support::SourceLoc loc{};      ///< Source location of the first segment.
};

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
    virtual void visit(const AddressOfExpr &) = 0;
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
    virtual void visit(AddressOfExpr &) = 0;
};

/// @brief Base class for all BASIC expressions.
struct Expr
{
    /// @brief Discriminator identifying the concrete expression subclass.
    enum class Kind
    {
        Int,
        Float,
        String,
        Bool,
        Var,
        Array,
        LBound,
        UBound,
        Unary,
        Binary,
        BuiltinCall,
        Call,
        New,
        Me,
        MemberAccess,
        MethodCall,
        Is,
        As,
        AddressOf,
    };

    /// Source location of the expression in the source file.
    il::support::SourceLoc loc{};

    virtual ~Expr() = default;

    /// @brief Retrieve the discriminator for this expression.
    [[nodiscard]] constexpr Kind kind() const noexcept
    {
        return kind_;
    }

    /// @brief Accept a visitor to process this expression.
    virtual void accept(ExprVisitor &visitor) const = 0;
    /// @brief Accept a mutable visitor to process this expression.
    virtual void accept(MutExprVisitor &visitor) = 0;

  protected:
    /// @brief Construct expression with specified kind.
    constexpr explicit Expr(Kind k) noexcept : kind_(k) {}

  private:
    Kind kind_;
};

/// @brief Signed integer literal expression.
struct IntExpr : Expr
{
    IntExpr() : Expr(Kind::Int) {}

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
    FloatExpr() : Expr(Kind::Float) {}

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
    StringExpr() : Expr(Kind::String) {}

    /// Owned UTF-8 string contents without surrounding quotes.
    std::string value;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Boolean literal expression.
struct BoolExpr : Expr
{
    BoolExpr() : Expr(Kind::Bool) {}

    /// Literal boolean value parsed from the source.
    bool value{false};
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Reference to a scalar variable.
struct VarExpr : Expr
{
    VarExpr() : Expr(Kind::Var) {}

    /// Variable name including optional type suffix.
    std::string name;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Array element access A(i) or A(i,j) for multi-dimensional arrays.
struct ArrayExpr : Expr
{
    ArrayExpr() : Expr(Kind::Array) {}

    /// Name of the array variable being indexed.
    std::string name;

    /// Zero-based index expression for single-dimensional arrays; owned and non-null.
    /// @deprecated Use @ref indices for multi-dimensional support.
    ExprPtr index;

    /// Index expressions for multi-dimensional arrays (owned).
    /// For single-dimensional arrays, this contains one element (backward compatible).
    std::vector<ExprPtr> indices;

    /// Resolved array extents from semantic analysis (BUG-020 fix).
    /// @details Stored during semantic analysis so the lowerer can compute correct
    ///          flattened indices for multi-dimensional arrays even after procedure
    ///          scope cleanup erases the temporary ArrayMetadata entries.
    std::vector<long long> resolvedExtents;

    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Query the logical lower bound of an array.
struct LBoundExpr : Expr
{
    LBoundExpr() : Expr(Kind::LBound) {}

    /// Name of the array operand queried for its lower bound.
    std::string name;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Query the logical upper bound of an array.
struct UBoundExpr : Expr
{
    UBoundExpr() : Expr(Kind::UBound) {}

    /// Name of the array operand queried for its upper bound.
    std::string name;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Unary expression (e.g., NOT, unary plus/minus).
struct UnaryExpr : Expr
{
    UnaryExpr() : Expr(Kind::Unary) {}

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
    BinaryExpr() : Expr(Kind::Binary) {}

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
    BuiltinCallExpr() : Expr(Kind::BuiltinCall) {}

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
        Tan,
        Atn,
        Exp,
        Log,
        Sgn,
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
        Timer,
        Argc,
        ArgGet,
        Command,
        Err
    } builtin{Builtin::Len};

    /// Argument expressions passed to the builtin; owned.
    std::vector<ExprPtr> args;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Call to user-defined FUNCTION or SUB.
struct CallExpr : Expr
{
    CallExpr() : Expr(Kind::Call) {}

    /// Procedure name to invoke.
    Identifier callee;

    /// Optional qualified callee path when a dotted name was parsed.
    /// When non-empty, `callee` contains the dot-joined string as well for
    /// backward compatibility with existing passes.
    std::vector<std::string> calleeQualified;

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
    NewExpr() : Expr(Kind::New) {}

    /// Name of the class type to instantiate.
    std::string className;

    /// Optional qualified class/type name segments. When non-empty, className
    /// stores the dot-joined form for compatibility with existing passes.
    std::vector<std::string> qualifiedType;

    /// Arguments passed to the constructor.
    std::vector<ExprPtr> args;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Reference to the receiver instance inside methods.
struct MeExpr : Expr
{
    MeExpr() : Expr(Kind::Me) {}

    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Access a member field on an object.
struct MemberAccessExpr : Expr
{
    MemberAccessExpr() : Expr(Kind::MemberAccess) {}

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
    MethodCallExpr() : Expr(Kind::MethodCall) {}

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
    IsExpr() : Expr(Kind::Is) {}

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
    AsExpr() : Expr(Kind::As) {}

    /// Value being cast.
    ExprPtr value;
    /// Dotted type name segments.
    std::vector<std::string> typeName;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

/// @brief Expression that obtains a function pointer: `ADDRESSOF SubOrFunction`.
/// Used for threading APIs that require callback functions.
struct AddressOfExpr : Expr
{
    AddressOfExpr() : Expr(Kind::AddressOf) {}

    /// Name of the SUB or FUNCTION whose address is being taken.
    std::string targetName;
    void accept(ExprVisitor &visitor) const override;
    void accept(MutExprVisitor &visitor) override;
};

} // namespace il::frontends::basic
