//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/AST.hpp
// Purpose: Defines the complete AST for Viper Pascal.
// Key invariants: All nodes have SourceLoc; ownership via std::unique_ptr.
// Ownership/Lifetime: Nodes are owned by their parent containers.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/source_location.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace il::frontends::pascal
{

//===----------------------------------------------------------------------===//
// Forward Declarations
//===----------------------------------------------------------------------===//

struct Expr;
struct Stmt;
struct Decl;
struct TypeNode;
struct BlockStmt;

//===----------------------------------------------------------------------===//
// Expression Nodes
//===----------------------------------------------------------------------===//

/// @brief Discriminator for expression node kinds.
enum class ExprKind
{
    IntLiteral,
    RealLiteral,
    StringLiteral,
    BoolLiteral,
    NilLiteral,
    Name,
    Unary,
    Binary,
    Call,
    Index,
    Field,
    TypeCast,
    SetConstructor,
    AddressOf,
    Dereference,
    Is,          ///< Runtime type-check (expr is T)
};

/// @brief Base class for all Pascal expressions.
struct Expr
{
    ExprKind kind;
    il::support::SourceLoc loc;

    explicit Expr(ExprKind k, il::support::SourceLoc l = {}) : kind(k), loc(l) {}
    virtual ~Expr() = default;
};

/// @brief Integer literal expression.
struct IntLiteralExpr : Expr
{
    int64_t value;

    explicit IntLiteralExpr(int64_t v, il::support::SourceLoc l = {})
        : Expr(ExprKind::IntLiteral, l), value(v) {}
};

/// @brief Real (floating-point) literal expression.
struct RealLiteralExpr : Expr
{
    double value;

    explicit RealLiteralExpr(double v, il::support::SourceLoc l = {})
        : Expr(ExprKind::RealLiteral, l), value(v) {}
};

/// @brief String literal expression.
struct StringLiteralExpr : Expr
{
    std::string value;

    explicit StringLiteralExpr(std::string v, il::support::SourceLoc l = {})
        : Expr(ExprKind::StringLiteral, l), value(std::move(v)) {}
};

/// @brief Boolean literal expression (True/False).
struct BoolLiteralExpr : Expr
{
    bool value;

    explicit BoolLiteralExpr(bool v, il::support::SourceLoc l = {})
        : Expr(ExprKind::BoolLiteral, l), value(v) {}
};

/// @brief Nil literal expression.
struct NilLiteralExpr : Expr
{
    explicit NilLiteralExpr(il::support::SourceLoc l = {})
        : Expr(ExprKind::NilLiteral, l) {}
};

/// @brief Name/identifier expression (variable, constant, type reference).
struct NameExpr : Expr
{
    std::string name;

    explicit NameExpr(std::string n, il::support::SourceLoc l = {})
        : Expr(ExprKind::Name, l), name(std::move(n)) {}
};

/// @brief Unary operator expression.
struct UnaryExpr : Expr
{
    enum class Op
    {
        Neg,  ///< -x
        Not,  ///< not x
        Plus, ///< +x (identity)
    };

    Op op;
    std::unique_ptr<Expr> operand;

    UnaryExpr(Op o, std::unique_ptr<Expr> operand, il::support::SourceLoc l = {})
        : Expr(ExprKind::Unary, l), op(o), operand(std::move(operand)) {}
};

/// @brief Binary operator expression.
struct BinaryExpr : Expr
{
    enum class Op
    {
        // Arithmetic
        Add,    ///< +
        Sub,    ///< -
        Mul,    ///< *
        Div,    ///< / (real division)
        IntDiv, ///< div (integer division)
        Mod,    ///< mod

        // Comparison
        Eq, ///< =
        Ne, ///< <>
        Lt, ///< <
        Le, ///< <=
        Gt, ///< >
        Ge, ///< >=

        // Logical
        And, ///< and
        Or,  ///< or

        // Other
        In,       ///< in (set membership)
        Coalesce, ///< ?? (nil coalescing)
    };

    Op op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;

    BinaryExpr(Op o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r,
               il::support::SourceLoc loc = {})
        : Expr(ExprKind::Binary, loc), op(o), left(std::move(l)), right(std::move(r)) {}
};

/// @brief Function/procedure call expression.
struct CallExpr : Expr
{
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;

    /// @brief True if this is a constructor call (ClassName.Create pattern)
    mutable bool isConstructorCall{false};
    /// @brief For constructor calls, the class name being constructed
    mutable std::string constructorClassName;

    CallExpr(std::unique_ptr<Expr> callee, std::vector<std::unique_ptr<Expr>> args,
             il::support::SourceLoc l = {})
        : Expr(ExprKind::Call, l), callee(std::move(callee)), args(std::move(args)) {}
};

/// @brief Array indexing expression.
struct IndexExpr : Expr
{
    std::unique_ptr<Expr> base;
    std::vector<std::unique_ptr<Expr>> indices;

    IndexExpr(std::unique_ptr<Expr> base, std::vector<std::unique_ptr<Expr>> indices,
              il::support::SourceLoc l = {})
        : Expr(ExprKind::Index, l), base(std::move(base)), indices(std::move(indices)) {}
};

/// @brief Field/member access expression (record.field or object.member).
struct FieldExpr : Expr
{
    std::unique_ptr<Expr> base;
    std::string field;

    FieldExpr(std::unique_ptr<Expr> base, std::string field, il::support::SourceLoc l = {})
        : Expr(ExprKind::Field, l), base(std::move(base)), field(std::move(field)) {}
};

/// @brief Type cast expression.
struct TypeCastExpr : Expr
{
    std::unique_ptr<TypeNode> targetType;
    std::unique_ptr<Expr> operand;

    TypeCastExpr(std::unique_ptr<TypeNode> type, std::unique_ptr<Expr> operand,
                 il::support::SourceLoc l = {})
        : Expr(ExprKind::TypeCast, l), targetType(std::move(type)), operand(std::move(operand)) {}
};

/// @brief Runtime type-check expression: (expr is T)
struct IsExpr : Expr
{
    std::unique_ptr<Expr> operand;
    std::unique_ptr<TypeNode> targetType;

    IsExpr(std::unique_ptr<Expr> op, std::unique_ptr<TypeNode> type, il::support::SourceLoc l = {})
        : Expr(ExprKind::Is, l), operand(std::move(op)), targetType(std::move(type)) {}
};

/// @brief Set constructor expression [1, 2, 3] or [1..10].
struct SetConstructorExpr : Expr
{
    /// @brief A single element or range in a set constructor.
    struct Element
    {
        std::unique_ptr<Expr> start;
        std::unique_ptr<Expr> end; ///< nullptr for single element, non-null for range
    };

    std::vector<Element> elements;

    explicit SetConstructorExpr(std::vector<Element> elems, il::support::SourceLoc l = {})
        : Expr(ExprKind::SetConstructor, l), elements(std::move(elems)) {}
};

/// @brief Address-of expression (@variable).
struct AddressOfExpr : Expr
{
    std::unique_ptr<Expr> operand;

    explicit AddressOfExpr(std::unique_ptr<Expr> operand, il::support::SourceLoc l = {})
        : Expr(ExprKind::AddressOf, l), operand(std::move(operand)) {}
};

/// @brief Pointer dereference expression (ptr^).
struct DereferenceExpr : Expr
{
    std::unique_ptr<Expr> operand;

    explicit DereferenceExpr(std::unique_ptr<Expr> operand, il::support::SourceLoc l = {})
        : Expr(ExprKind::Dereference, l), operand(std::move(operand)) {}
};

//===----------------------------------------------------------------------===//
// Type Nodes
//===----------------------------------------------------------------------===//

/// @brief Discriminator for type node kinds.
enum class TypeKind
{
    Named,
    Optional,
    Array,
    Record,
    Pointer,
    Procedure,
    Function,
    Set,
    Range,
    Enum,
};

/// @brief Base class for all Pascal type nodes.
struct TypeNode
{
    TypeKind kind;
    il::support::SourceLoc loc;

    explicit TypeNode(TypeKind k, il::support::SourceLoc l = {}) : kind(k), loc(l) {}
    virtual ~TypeNode() = default;

    /// @brief Create a deep copy of this type node.
    virtual std::unique_ptr<TypeNode> clone() const = 0;
};

/// @brief Named type reference (Integer, String, TMyClass).
struct NamedTypeNode : TypeNode
{
    std::string name;

    explicit NamedTypeNode(std::string n, il::support::SourceLoc l = {})
        : TypeNode(TypeKind::Named, l), name(std::move(n)) {}

    std::unique_ptr<TypeNode> clone() const override
    {
        return std::make_unique<NamedTypeNode>(name, loc);
    }
};

/// @brief Optional type (type?).
struct OptionalTypeNode : TypeNode
{
    std::unique_ptr<TypeNode> inner;

    explicit OptionalTypeNode(std::unique_ptr<TypeNode> inner, il::support::SourceLoc l = {})
        : TypeNode(TypeKind::Optional, l), inner(std::move(inner)) {}

    std::unique_ptr<TypeNode> clone() const override
    {
        return std::make_unique<OptionalTypeNode>(inner ? inner->clone() : nullptr, loc);
    }
};

/// @brief Array type.
struct ArrayTypeNode : TypeNode
{
    /// @brief Dimension size expression. Arrays are always 0-based in v0.1.
    struct DimSize
    {
        std::unique_ptr<Expr> size; ///< Size expression (bounds are 0..size-1)
    };

    std::vector<DimSize> dimensions; ///< Empty for dynamic arrays
    std::unique_ptr<TypeNode> elementType;

    ArrayTypeNode(std::vector<DimSize> dims, std::unique_ptr<TypeNode> elemType,
                  il::support::SourceLoc l = {})
        : TypeNode(TypeKind::Array, l), dimensions(std::move(dims)),
          elementType(std::move(elemType)) {}

    std::unique_ptr<TypeNode> clone() const override
    {
        // For array types in field declarations, we only support dynamic arrays
        // (no dimensions) for cloning. Static array cloning would require Expr cloning.
        std::vector<DimSize> dims;
        return std::make_unique<ArrayTypeNode>(
            std::move(dims),
            elementType ? elementType->clone() : nullptr,
            loc);
    }
};

/// @brief Record field declaration.
struct RecordField
{
    std::string name;
    std::unique_ptr<TypeNode> type;
    il::support::SourceLoc loc;
};

/// @brief Record type.
struct RecordTypeNode : TypeNode
{
    std::vector<RecordField> fields;

    explicit RecordTypeNode(std::vector<RecordField> fields, il::support::SourceLoc l = {})
        : TypeNode(TypeKind::Record, l), fields(std::move(fields)) {}

    std::unique_ptr<TypeNode> clone() const override
    {
        // Record cloning would require cloning fields which need TypeNode clones
        // For now, return an empty record (semantic analyzer will handle errors)
        return std::make_unique<RecordTypeNode>(std::vector<RecordField>{}, loc);
    }
};

/// @brief Pointer type (^T).
struct PointerTypeNode : TypeNode
{
    std::unique_ptr<TypeNode> pointeeType;

    explicit PointerTypeNode(std::unique_ptr<TypeNode> pointee, il::support::SourceLoc l = {})
        : TypeNode(TypeKind::Pointer, l), pointeeType(std::move(pointee)) {}

    std::unique_ptr<TypeNode> clone() const override
    {
        return std::make_unique<PointerTypeNode>(
            pointeeType ? pointeeType->clone() : nullptr, loc);
    }
};

/// @brief Parameter declaration for procedure/function types.
struct ParamSpec
{
    std::string name;
    std::unique_ptr<TypeNode> type;
    bool isVar{false};   ///< var parameter (pass by reference)
    bool isConst{false}; ///< const parameter
    il::support::SourceLoc loc;
};

/// @brief Procedure type (procedure(params)).
struct ProcedureTypeNode : TypeNode
{
    std::vector<ParamSpec> params;

    explicit ProcedureTypeNode(std::vector<ParamSpec> params, il::support::SourceLoc l = {})
        : TypeNode(TypeKind::Procedure, l), params(std::move(params)) {}

    std::unique_ptr<TypeNode> clone() const override
    {
        // Procedure types are rarely used as field types; return empty params
        return std::make_unique<ProcedureTypeNode>(std::vector<ParamSpec>{}, loc);
    }
};

/// @brief Function type (function(params): returnType).
struct FunctionTypeNode : TypeNode
{
    std::vector<ParamSpec> params;
    std::unique_ptr<TypeNode> returnType;

    FunctionTypeNode(std::vector<ParamSpec> params, std::unique_ptr<TypeNode> retType,
                     il::support::SourceLoc l = {})
        : TypeNode(TypeKind::Function, l), params(std::move(params)),
          returnType(std::move(retType)) {}

    std::unique_ptr<TypeNode> clone() const override
    {
        // Function types are rarely used as field types; return empty params
        return std::make_unique<FunctionTypeNode>(
            std::vector<ParamSpec>{},
            returnType ? returnType->clone() : nullptr,
            loc);
    }
};

/// @brief Set type (set of T).
struct SetTypeNode : TypeNode
{
    std::unique_ptr<TypeNode> elementType;

    explicit SetTypeNode(std::unique_ptr<TypeNode> elemType, il::support::SourceLoc l = {})
        : TypeNode(TypeKind::Set, l), elementType(std::move(elemType)) {}

    std::unique_ptr<TypeNode> clone() const override
    {
        return std::make_unique<SetTypeNode>(
            elementType ? elementType->clone() : nullptr, loc);
    }
};

/// @brief Subrange type (low..high).
struct RangeTypeNode : TypeNode
{
    std::unique_ptr<Expr> low;
    std::unique_ptr<Expr> high;

    RangeTypeNode(std::unique_ptr<Expr> low, std::unique_ptr<Expr> high,
                  il::support::SourceLoc l = {})
        : TypeNode(TypeKind::Range, l), low(std::move(low)), high(std::move(high)) {}

    std::unique_ptr<TypeNode> clone() const override
    {
        // Range cloning would require Expr cloning; return null bounds
        return std::make_unique<RangeTypeNode>(nullptr, nullptr, loc);
    }
};

/// @brief Enumeration type (Red, Green, Blue).
struct EnumTypeNode : TypeNode
{
    std::vector<std::string> values;

    explicit EnumTypeNode(std::vector<std::string> values, il::support::SourceLoc l = {})
        : TypeNode(TypeKind::Enum, l), values(std::move(values)) {}

    std::unique_ptr<TypeNode> clone() const override
    {
        return std::make_unique<EnumTypeNode>(values, loc);
    }
};

//===----------------------------------------------------------------------===//
// Statement Nodes
//===----------------------------------------------------------------------===//

/// @brief Discriminator for statement node kinds.
enum class StmtKind
{
    Assign,
    Call,
    Block,
    If,
    Case,
    For,
    ForIn,
    While,
    Repeat,
    Break,
    Continue,
    Exit,
    Raise,
    TryExcept,
    TryFinally,
    With,
    Inherited,
    Empty,
};

/// @brief Base class for all Pascal statements.
struct Stmt
{
    StmtKind kind;
    il::support::SourceLoc loc;

    explicit Stmt(StmtKind k, il::support::SourceLoc l = {}) : kind(k), loc(l) {}
    virtual ~Stmt() = default;
};

/// @brief Assignment statement (target := value).
struct AssignStmt : Stmt
{
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;

    AssignStmt(std::unique_ptr<Expr> target, std::unique_ptr<Expr> value,
               il::support::SourceLoc l = {})
        : Stmt(StmtKind::Assign, l), target(std::move(target)), value(std::move(value)) {}
};

/// @brief Procedure/function call statement.
struct CallStmt : Stmt
{
    std::unique_ptr<Expr> call; ///< Must be a CallExpr

    explicit CallStmt(std::unique_ptr<Expr> call, il::support::SourceLoc l = {})
        : Stmt(StmtKind::Call, l), call(std::move(call)) {}
};

/// @brief Block statement (begin...end).
struct BlockStmt : Stmt
{
    std::vector<std::unique_ptr<Stmt>> stmts;

    explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> stmts = {},
                       il::support::SourceLoc l = {})
        : Stmt(StmtKind::Block, l), stmts(std::move(stmts)) {}
};

/// @brief If statement.
struct IfStmt : Stmt
{
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch; ///< May be nullptr

    IfStmt(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> thenBr,
           std::unique_ptr<Stmt> elseBr = nullptr, il::support::SourceLoc l = {})
        : Stmt(StmtKind::If, l), condition(std::move(cond)), thenBranch(std::move(thenBr)),
          elseBranch(std::move(elseBr)) {}
};

/// @brief Case statement arm.
struct CaseArm
{
    std::vector<std::unique_ptr<Expr>> labels; ///< May include ranges
    std::unique_ptr<Stmt> body;
    il::support::SourceLoc loc;
};

/// @brief Case statement.
struct CaseStmt : Stmt
{
    std::unique_ptr<Expr> expr;
    std::vector<CaseArm> arms;
    std::unique_ptr<Stmt> elseBody; ///< May be nullptr

    CaseStmt(std::unique_ptr<Expr> expr, std::vector<CaseArm> arms,
             std::unique_ptr<Stmt> elseBody = nullptr, il::support::SourceLoc l = {})
        : Stmt(StmtKind::Case, l), expr(std::move(expr)), arms(std::move(arms)),
          elseBody(std::move(elseBody)) {}
};

/// @brief For loop direction.
enum class ForDirection
{
    To,
    Downto
};

/// @brief For loop statement.
struct ForStmt : Stmt
{
    std::string loopVar;
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> bound;
    ForDirection direction;
    std::unique_ptr<Stmt> body;

    ForStmt(std::string var, std::unique_ptr<Expr> start, std::unique_ptr<Expr> bound,
            ForDirection dir, std::unique_ptr<Stmt> body, il::support::SourceLoc l = {})
        : Stmt(StmtKind::For, l), loopVar(std::move(var)), start(std::move(start)),
          bound(std::move(bound)), direction(dir), body(std::move(body)) {}
};

/// @brief For-in loop statement (iteration over collection).
struct ForInStmt : Stmt
{
    std::string loopVar;
    std::unique_ptr<Expr> collection;
    std::unique_ptr<Stmt> body;

    ForInStmt(std::string var, std::unique_ptr<Expr> collection, std::unique_ptr<Stmt> body,
              il::support::SourceLoc l = {})
        : Stmt(StmtKind::ForIn, l), loopVar(std::move(var)), collection(std::move(collection)),
          body(std::move(body)) {}
};

/// @brief While loop statement.
struct WhileStmt : Stmt
{
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;

    WhileStmt(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> body,
              il::support::SourceLoc l = {})
        : Stmt(StmtKind::While, l), condition(std::move(cond)), body(std::move(body)) {}
};

/// @brief Repeat-until loop statement.
struct RepeatStmt : Stmt
{
    std::unique_ptr<Stmt> body;
    std::unique_ptr<Expr> condition;

    RepeatStmt(std::unique_ptr<Stmt> body, std::unique_ptr<Expr> cond,
               il::support::SourceLoc l = {})
        : Stmt(StmtKind::Repeat, l), body(std::move(body)), condition(std::move(cond)) {}
};

/// @brief Break statement.
struct BreakStmt : Stmt
{
    explicit BreakStmt(il::support::SourceLoc l = {}) : Stmt(StmtKind::Break, l) {}
};

/// @brief Continue statement.
struct ContinueStmt : Stmt
{
    explicit ContinueStmt(il::support::SourceLoc l = {}) : Stmt(StmtKind::Continue, l) {}
};

/// @brief Exit statement (early return from function/procedure).
/// @details `Exit;` returns from procedure. `Exit(value);` returns value from function.
struct ExitStmt : Stmt
{
    std::unique_ptr<Expr> value; ///< Optional return value (for functions)

    explicit ExitStmt(il::support::SourceLoc l = {}) : Stmt(StmtKind::Exit, l) {}
    ExitStmt(std::unique_ptr<Expr> val, il::support::SourceLoc l = {})
        : Stmt(StmtKind::Exit, l), value(std::move(val)) {}
};

/// @brief Inherited statement (call to base class method).
/// @details `inherited;` calls the base version of the current method.
///          `inherited MethodName(args);` calls a specific base method.
struct InheritedStmt : Stmt
{
    std::string methodName;                    ///< Empty for implicit (same method name)
    std::vector<std::unique_ptr<Expr>> args;   ///< Arguments for the call

    explicit InheritedStmt(il::support::SourceLoc l = {}) : Stmt(StmtKind::Inherited, l) {}

    InheritedStmt(std::string name, std::vector<std::unique_ptr<Expr>> arguments,
                  il::support::SourceLoc l = {})
        : Stmt(StmtKind::Inherited, l), methodName(std::move(name)), args(std::move(arguments)) {}
};

/// @brief Raise statement (exception throwing).
struct RaiseStmt : Stmt
{
    std::unique_ptr<Expr> exception; ///< May be nullptr for re-raise

    explicit RaiseStmt(std::unique_ptr<Expr> exc = nullptr, il::support::SourceLoc l = {})
        : Stmt(StmtKind::Raise, l), exception(std::move(exc)) {}
};

/// @brief Exception handler in try-except.
struct ExceptHandler
{
    std::string varName;  ///< May be empty
    std::string typeName; ///< Exception type name
    std::unique_ptr<Stmt> body;
    il::support::SourceLoc loc;
};

/// @brief Try-except statement.
struct TryExceptStmt : Stmt
{
    std::unique_ptr<BlockStmt> tryBody;
    std::vector<ExceptHandler> handlers;
    std::unique_ptr<Stmt> elseBody; ///< May be nullptr

    TryExceptStmt(std::unique_ptr<BlockStmt> tryBody, std::vector<ExceptHandler> handlers,
                  std::unique_ptr<Stmt> elseBody = nullptr, il::support::SourceLoc l = {})
        : Stmt(StmtKind::TryExcept, l), tryBody(std::move(tryBody)),
          handlers(std::move(handlers)), elseBody(std::move(elseBody)) {}
};

/// @brief Try-finally statement.
struct TryFinallyStmt : Stmt
{
    std::unique_ptr<BlockStmt> tryBody;
    std::unique_ptr<BlockStmt> finallyBody;

    TryFinallyStmt(std::unique_ptr<BlockStmt> tryBody, std::unique_ptr<BlockStmt> finallyBody,
                   il::support::SourceLoc l = {})
        : Stmt(StmtKind::TryFinally, l), tryBody(std::move(tryBody)),
          finallyBody(std::move(finallyBody)) {}
};

/// @brief With statement.
struct WithStmt : Stmt
{
    std::vector<std::unique_ptr<Expr>> objects;
    std::unique_ptr<Stmt> body;

    WithStmt(std::vector<std::unique_ptr<Expr>> objs, std::unique_ptr<Stmt> body,
             il::support::SourceLoc l = {})
        : Stmt(StmtKind::With, l), objects(std::move(objs)), body(std::move(body)) {}
};

/// @brief Empty statement (just a semicolon).
struct EmptyStmt : Stmt
{
    explicit EmptyStmt(il::support::SourceLoc l = {}) : Stmt(StmtKind::Empty, l) {}
};

//===----------------------------------------------------------------------===//
// Declaration Nodes
//===----------------------------------------------------------------------===//

/// @brief Discriminator for declaration node kinds.
enum class DeclKind
{
    Const,
    Var,
    Type,
    Procedure,
    Function,
    Class,
    Interface,
    Constructor,
    Destructor,
    Method,
    Property,
    Label,
    Uses,
};

/// @brief Base class for all Pascal declarations.
struct Decl
{
    DeclKind kind;
    il::support::SourceLoc loc;

    explicit Decl(DeclKind k, il::support::SourceLoc l = {}) : kind(k), loc(l) {}
    virtual ~Decl() = default;
};

/// @brief Constant declaration.
struct ConstDecl : Decl
{
    std::string name;
    std::unique_ptr<TypeNode> type; ///< May be nullptr (inferred)
    std::unique_ptr<Expr> value;

    ConstDecl(std::string name, std::unique_ptr<Expr> value,
              std::unique_ptr<TypeNode> type = nullptr, il::support::SourceLoc l = {})
        : Decl(DeclKind::Const, l), name(std::move(name)), type(std::move(type)),
          value(std::move(value)) {}
};

/// @brief Variable declaration.
struct VarDecl : Decl
{
    std::vector<std::string> names;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expr> init; ///< May be nullptr

    VarDecl(std::vector<std::string> names, std::unique_ptr<TypeNode> type,
            std::unique_ptr<Expr> init = nullptr, il::support::SourceLoc l = {})
        : Decl(DeclKind::Var, l), names(std::move(names)), type(std::move(type)),
          init(std::move(init)) {}
};

/// @brief Type declaration (type alias or definition).
struct TypeDecl : Decl
{
    std::string name;
    std::unique_ptr<TypeNode> type;

    TypeDecl(std::string name, std::unique_ptr<TypeNode> type, il::support::SourceLoc l = {})
        : Decl(DeclKind::Type, l), name(std::move(name)), type(std::move(type)) {}
};

/// @brief Parameter declaration for procedures/functions.
struct ParamDecl
{
    std::string name;
    std::unique_ptr<TypeNode> type;
    bool isVar{false};
    bool isConst{false};
    std::unique_ptr<Expr> defaultValue; ///< May be nullptr
    il::support::SourceLoc loc;
};

/// @brief Procedure declaration.
struct ProcedureDecl : Decl
{
    std::string name;
    std::string className;  ///< Empty for free procedures; class name for methods
    std::vector<ParamDecl> params;
    std::vector<std::unique_ptr<Decl>> localDecls;
    std::unique_ptr<BlockStmt> body; ///< May be nullptr (forward declaration)
    bool isForward{false};
    bool isVirtual{false};   ///< Marked virtual (overridable)
    bool isOverride{false};  ///< Marked override (must match base virtual)
    bool isAbstract{false};  ///< Marked abstract (no implementation)

    ProcedureDecl(std::string name, std::vector<ParamDecl> params, il::support::SourceLoc l = {})
        : Decl(DeclKind::Procedure, l), name(std::move(name)), params(std::move(params)) {}

    /// @brief Check if this is a method (belongs to a class).
    bool isMethod() const { return !className.empty(); }
};

/// @brief Function declaration.
struct FunctionDecl : Decl
{
    std::string name;
    std::string className;  ///< Empty for free functions; class name for methods
    std::vector<ParamDecl> params;
    std::unique_ptr<TypeNode> returnType;
    std::vector<std::unique_ptr<Decl>> localDecls;
    std::unique_ptr<BlockStmt> body; ///< May be nullptr (forward declaration)
    bool isForward{false};
    bool isVirtual{false};   ///< Marked virtual (overridable)
    bool isOverride{false};  ///< Marked override (must match base virtual)
    bool isAbstract{false};  ///< Marked abstract (no implementation)

    FunctionDecl(std::string name, std::vector<ParamDecl> params,
                 std::unique_ptr<TypeNode> returnType, il::support::SourceLoc l = {})
        : Decl(DeclKind::Function, l), name(std::move(name)), params(std::move(params)),
          returnType(std::move(returnType)) {}

    /// @brief Check if this is a method (belongs to a class).
    bool isMethod() const { return !className.empty(); }
};

/// @brief Visibility section in a class.
enum class Visibility
{
    Private,
    Public,
};

/// @brief Method signature in interface or class.
struct MethodSig
{
    std::string name;
    std::vector<ParamDecl> params;
    std::unique_ptr<TypeNode> returnType; ///< nullptr for procedures
    bool isVirtual{false};
    bool isOverride{false};
    bool isAbstract{false};
    il::support::SourceLoc loc;
};

/// @brief Property declaration.
struct PropertyDecl : Decl
{
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::string getter; ///< Getter method name (may be empty)
    std::string setter; ///< Setter method name (may be empty)
    Visibility visibility{Visibility::Public};

    PropertyDecl(std::string name, std::unique_ptr<TypeNode> type, il::support::SourceLoc l = {})
        : Decl(DeclKind::Property, l), name(std::move(name)), type(std::move(type)) {}
};

/// @brief Class member (field or method).
struct ClassMember
{
    Visibility visibility{Visibility::Private};

    /// @brief Member type discriminator.
    enum class Kind
    {
        Field,
        Method,
        Constructor,
        Destructor,
        Property,
    };
    Kind memberKind;

    // Field members
    std::string fieldName;
    std::unique_ptr<TypeNode> fieldType;
    bool isWeak{false}; ///< Weak reference field

    // Method members
    std::unique_ptr<Decl> methodDecl; ///< FunctionDecl, ProcedureDecl, ConstructorDecl, etc.

    // Property members
    std::unique_ptr<PropertyDecl> property;

    il::support::SourceLoc loc;
};

/// @brief Constructor declaration.
struct ConstructorDecl : Decl
{
    std::string name;      ///< Usually "Create"
    std::string className; ///< Owning class name (for method implementations)
    std::vector<ParamDecl> params;
    std::vector<std::unique_ptr<Decl>> localDecls;
    std::unique_ptr<BlockStmt> body; ///< May be nullptr (forward declaration)
    bool isForward{false};

    ConstructorDecl(std::string name, std::vector<ParamDecl> params,
                    il::support::SourceLoc l = {})
        : Decl(DeclKind::Constructor, l), name(std::move(name)), params(std::move(params)) {}
};

/// @brief Destructor declaration.
struct DestructorDecl : Decl
{
    std::string name;      ///< Usually "Destroy"
    std::string className; ///< Owning class name (for method implementations)
    std::vector<std::unique_ptr<Decl>> localDecls;
    std::unique_ptr<BlockStmt> body; ///< May be nullptr (forward declaration)
    bool isForward{false};

    explicit DestructorDecl(std::string name = "Destroy", il::support::SourceLoc l = {})
        : Decl(DeclKind::Destructor, l), name(std::move(name)) {}
};

/// @brief Class declaration.
struct ClassDecl : Decl
{
    std::string name;
    std::string baseClass;                ///< Empty if none
    std::vector<std::string> interfaces;  ///< Implemented interfaces
    std::vector<ClassMember> members;

    ClassDecl(std::string name, il::support::SourceLoc l = {})
        : Decl(DeclKind::Class, l), name(std::move(name)) {}
};

/// @brief Interface declaration.
struct InterfaceDecl : Decl
{
    std::string name;
    std::vector<std::string> baseInterfaces; ///< Extended interfaces
    std::vector<MethodSig> methods;

    InterfaceDecl(std::string name, il::support::SourceLoc l = {})
        : Decl(DeclKind::Interface, l), name(std::move(name)) {}
};

/// @brief Label declaration.
struct LabelDecl : Decl
{
    std::vector<std::string> labels;

    explicit LabelDecl(std::vector<std::string> labels, il::support::SourceLoc l = {})
        : Decl(DeclKind::Label, l), labels(std::move(labels)) {}
};

/// @brief Uses declaration (unit imports).
struct UsesDecl : Decl
{
    std::vector<std::string> units;

    explicit UsesDecl(std::vector<std::string> units, il::support::SourceLoc l = {})
        : Decl(DeclKind::Uses, l), units(std::move(units)) {}
};

//===----------------------------------------------------------------------===//
// Top-Level Structures
//===----------------------------------------------------------------------===//

/// @brief Pascal program.
struct Program
{
    std::string name;
    std::vector<std::string> usedUnits;
    std::vector<std::unique_ptr<Decl>> decls;
    std::unique_ptr<BlockStmt> body;
    il::support::SourceLoc loc;
};

/// @brief Pascal unit.
struct Unit
{
    std::string name;
    std::vector<std::string> usedUnits;              ///< Units used in interface
    std::vector<std::unique_ptr<Decl>> interfaceDecls;
    std::vector<std::string> implUsedUnits;          ///< Units used in implementation
    std::vector<std::unique_ptr<Decl>> implDecls;
    std::unique_ptr<BlockStmt> initSection;          ///< May be nullptr
    std::unique_ptr<BlockStmt> finalSection;         ///< May be nullptr
    il::support::SourceLoc loc;
};

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

/// @brief Get the name of an ExprKind for debugging.
const char *exprKindToString(ExprKind kind);

/// @brief Get the name of a StmtKind for debugging.
const char *stmtKindToString(StmtKind kind);

/// @brief Get the name of a DeclKind for debugging.
const char *declKindToString(DeclKind kind);

/// @brief Get the name of a TypeKind for debugging.
const char *typeKindToString(TypeKind kind);

} // namespace il::frontends::pascal
