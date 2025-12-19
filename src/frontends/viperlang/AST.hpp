//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/viperlang/AST.hpp
// Purpose: Abstract Syntax Tree types for ViperLang.
// Key invariants: All AST nodes own their children via unique_ptr.
// Ownership/Lifetime: AST nodes are owned by their parent or the parser.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diagnostics.hpp"
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace il::frontends::viperlang
{

// Forward declarations
struct Expr;
struct Stmt;
struct TypeNode;
struct Decl;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using TypePtr = std::unique_ptr<TypeNode>;
using DeclPtr = std::unique_ptr<Decl>;

//===----------------------------------------------------------------------===//
// Source Location
//===----------------------------------------------------------------------===//

using SourceLoc = il::support::SourceLoc;

//===----------------------------------------------------------------------===//
// Type Nodes
//===----------------------------------------------------------------------===//

/// Type node kinds
enum class TypeKind
{
    Named,    // Integer, String, MyClass
    Generic,  // List[T], Map[K,V]
    Optional, // T?
    Function, // (A, B) -> C
    Tuple,    // (A, B)
};

/// Base type node
struct TypeNode
{
    TypeKind kind;
    SourceLoc loc;

    TypeNode(TypeKind k, SourceLoc l) : kind(k), loc(l) {}

    virtual ~TypeNode() = default;
};

/// Named type: Integer, String, MyClass
struct NamedType : TypeNode
{
    std::string name;

    NamedType(SourceLoc l, std::string n) : TypeNode(TypeKind::Named, l), name(std::move(n)) {}
};

/// Generic type: List[T], Map[K,V]
struct GenericType : TypeNode
{
    std::string name;
    std::vector<TypePtr> args;

    GenericType(SourceLoc l, std::string n, std::vector<TypePtr> a)
        : TypeNode(TypeKind::Generic, l), name(std::move(n)), args(std::move(a))
    {
    }
};

/// Optional type: T?
struct OptionalType : TypeNode
{
    TypePtr inner;

    OptionalType(SourceLoc l, TypePtr i) : TypeNode(TypeKind::Optional, l), inner(std::move(i)) {}
};

/// Function type: (A, B) -> C
struct FunctionType : TypeNode
{
    std::vector<TypePtr> params;
    TypePtr returnType; // nullptr = void

    FunctionType(SourceLoc l, std::vector<TypePtr> p, TypePtr ret)
        : TypeNode(TypeKind::Function, l), params(std::move(p)), returnType(std::move(ret))
    {
    }
};

/// Tuple type: (A, B)
struct TupleType : TypeNode
{
    std::vector<TypePtr> elements;

    TupleType(SourceLoc l, std::vector<TypePtr> e)
        : TypeNode(TypeKind::Tuple, l), elements(std::move(e))
    {
    }
};

//===----------------------------------------------------------------------===//
// Expression Nodes
//===----------------------------------------------------------------------===//

/// Expression kinds
enum class ExprKind
{
    // Literals
    IntLiteral,
    NumberLiteral,
    StringLiteral,
    BoolLiteral,
    NullLiteral,
    UnitLiteral,

    // Names
    Ident,
    SelfExpr,

    // Operations
    Binary,
    Unary,
    Ternary,
    Call,
    Index,
    Field,
    OptionalChain,
    Coalesce,
    Is,
    As,
    Range,

    // Construction
    New,
    Lambda,
    ListLiteral,
    MapLiteral,
    SetLiteral,

    // Control flow (as expressions)
    If,
    Match,
    Block,
};

/// Base expression node
struct Expr
{
    ExprKind kind;
    SourceLoc loc;

    Expr(ExprKind k, SourceLoc l) : kind(k), loc(l) {}

    virtual ~Expr() = default;
};

/// Integer literal: 42, 0xFF
struct IntLiteralExpr : Expr
{
    int64_t value;

    IntLiteralExpr(SourceLoc l, int64_t v) : Expr(ExprKind::IntLiteral, l), value(v) {}
};

/// Number literal: 3.14
struct NumberLiteralExpr : Expr
{
    double value;

    NumberLiteralExpr(SourceLoc l, double v) : Expr(ExprKind::NumberLiteral, l), value(v) {}
};

/// String literal: "hello"
struct StringLiteralExpr : Expr
{
    std::string value;

    StringLiteralExpr(SourceLoc l, std::string v)
        : Expr(ExprKind::StringLiteral, l), value(std::move(v))
    {
    }
};

/// Boolean literal: true, false
struct BoolLiteralExpr : Expr
{
    bool value;

    BoolLiteralExpr(SourceLoc l, bool v) : Expr(ExprKind::BoolLiteral, l), value(v) {}
};

/// Null literal: null
struct NullLiteralExpr : Expr
{
    NullLiteralExpr(SourceLoc l) : Expr(ExprKind::NullLiteral, l) {}
};

/// Unit literal: ()
struct UnitLiteralExpr : Expr
{
    UnitLiteralExpr(SourceLoc l) : Expr(ExprKind::UnitLiteral, l) {}
};

/// Identifier expression: foo
struct IdentExpr : Expr
{
    std::string name;

    IdentExpr(SourceLoc l, std::string n) : Expr(ExprKind::Ident, l), name(std::move(n)) {}
};

/// Self expression: self
struct SelfExpr : Expr
{
    SelfExpr(SourceLoc l) : Expr(ExprKind::SelfExpr, l) {}
};

/// Binary operator
enum class BinaryOp
{
    Add,
    Sub,
    Mul,
    Div,
    Mod, // Arithmetic
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge, // Comparison
    And,
    Or, // Logical
    BitAnd,
    BitOr,
    BitXor, // Bitwise
    Assign, // Assignment
};

/// Binary expression: a + b
struct BinaryExpr : Expr
{
    BinaryOp op;
    ExprPtr left;
    ExprPtr right;

    BinaryExpr(SourceLoc l, BinaryOp o, ExprPtr lhs, ExprPtr rhs)
        : Expr(ExprKind::Binary, l), op(o), left(std::move(lhs)), right(std::move(rhs))
    {
    }
};

/// Unary operator
enum class UnaryOp
{
    Neg,    // -
    Not,    // !
    BitNot, // ~
};

/// Unary expression: -a, !a
struct UnaryExpr : Expr
{
    UnaryOp op;
    ExprPtr operand;

    UnaryExpr(SourceLoc l, UnaryOp o, ExprPtr e)
        : Expr(ExprKind::Unary, l), op(o), operand(std::move(e))
    {
    }
};

/// Ternary expression: a ? b : c
struct TernaryExpr : Expr
{
    ExprPtr condition;
    ExprPtr thenExpr;
    ExprPtr elseExpr;

    TernaryExpr(SourceLoc l, ExprPtr c, ExprPtr t, ExprPtr e)
        : Expr(ExprKind::Ternary, l), condition(std::move(c)), thenExpr(std::move(t)),
          elseExpr(std::move(e))
    {
    }
};

/// Call argument (positional or named)
struct CallArg
{
    std::optional<std::string> name; // For named args
    ExprPtr value;
};

/// Call expression: f(x, y)
struct CallExpr : Expr
{
    ExprPtr callee;
    std::vector<CallArg> args;

    CallExpr(SourceLoc l, ExprPtr c, std::vector<CallArg> a)
        : Expr(ExprKind::Call, l), callee(std::move(c)), args(std::move(a))
    {
    }
};

/// Index expression: a[i]
struct IndexExpr : Expr
{
    ExprPtr base;
    ExprPtr index;

    IndexExpr(SourceLoc l, ExprPtr b, ExprPtr i)
        : Expr(ExprKind::Index, l), base(std::move(b)), index(std::move(i))
    {
    }
};

/// Field expression: a.b
struct FieldExpr : Expr
{
    ExprPtr base;
    std::string field;

    FieldExpr(SourceLoc l, ExprPtr b, std::string f)
        : Expr(ExprKind::Field, l), base(std::move(b)), field(std::move(f))
    {
    }
};

/// Optional chain expression: a?.b
struct OptionalChainExpr : Expr
{
    ExprPtr base;
    std::string field;

    OptionalChainExpr(SourceLoc l, ExprPtr b, std::string f)
        : Expr(ExprKind::OptionalChain, l), base(std::move(b)), field(std::move(f))
    {
    }
};

/// Coalesce expression: a ?? b
struct CoalesceExpr : Expr
{
    ExprPtr left;
    ExprPtr right;

    CoalesceExpr(SourceLoc l, ExprPtr lhs, ExprPtr rhs)
        : Expr(ExprKind::Coalesce, l), left(std::move(lhs)), right(std::move(rhs))
    {
    }
};

/// Is expression: x is T
struct IsExpr : Expr
{
    ExprPtr value;
    TypePtr type;

    IsExpr(SourceLoc l, ExprPtr v, TypePtr t)
        : Expr(ExprKind::Is, l), value(std::move(v)), type(std::move(t))
    {
    }
};

/// As expression: x as T
struct AsExpr : Expr
{
    ExprPtr value;
    TypePtr type;

    AsExpr(SourceLoc l, ExprPtr v, TypePtr t)
        : Expr(ExprKind::As, l), value(std::move(v)), type(std::move(t))
    {
    }
};

/// Range expression: a..b or a..=b
struct RangeExpr : Expr
{
    ExprPtr start;
    ExprPtr end;
    bool inclusive; // ..= vs ..

    RangeExpr(SourceLoc l, ExprPtr s, ExprPtr e, bool incl)
        : Expr(ExprKind::Range, l), start(std::move(s)), end(std::move(e)), inclusive(incl)
    {
    }
};

/// New expression: new Foo(args)
struct NewExpr : Expr
{
    TypePtr type;
    std::vector<CallArg> args;

    NewExpr(SourceLoc l, TypePtr t, std::vector<CallArg> a)
        : Expr(ExprKind::New, l), type(std::move(t)), args(std::move(a))
    {
    }
};

/// Lambda parameter
struct LambdaParam
{
    std::string name;
    TypePtr type; // nullptr = inferred
};

/// Lambda expression: (x) => x + 1
struct LambdaExpr : Expr
{
    std::vector<LambdaParam> params;
    TypePtr returnType; // nullptr = inferred
    ExprPtr body;

    LambdaExpr(SourceLoc l, std::vector<LambdaParam> p, TypePtr ret, ExprPtr b)
        : Expr(ExprKind::Lambda, l), params(std::move(p)), returnType(std::move(ret)),
          body(std::move(b))
    {
    }
};

/// List literal: [1, 2, 3]
struct ListLiteralExpr : Expr
{
    std::vector<ExprPtr> elements;

    ListLiteralExpr(SourceLoc l, std::vector<ExprPtr> e)
        : Expr(ExprKind::ListLiteral, l), elements(std::move(e))
    {
    }
};

/// Map entry: key: value
struct MapEntry
{
    ExprPtr key;
    ExprPtr value;
};

/// Map literal: {"a": 1, "b": 2}
struct MapLiteralExpr : Expr
{
    std::vector<MapEntry> entries;

    MapLiteralExpr(SourceLoc l, std::vector<MapEntry> e)
        : Expr(ExprKind::MapLiteral, l), entries(std::move(e))
    {
    }
};

/// Set literal: {1, 2, 3}
struct SetLiteralExpr : Expr
{
    std::vector<ExprPtr> elements;

    SetLiteralExpr(SourceLoc l, std::vector<ExprPtr> e)
        : Expr(ExprKind::SetLiteral, l), elements(std::move(e))
    {
    }
};

// Forward declare BlockExpr (defined after statements)
struct BlockExpr;

/// If expression: if (c) a else b
struct IfExpr : Expr
{
    ExprPtr condition;
    ExprPtr thenBranch;
    ExprPtr elseBranch; // required for if expressions

    IfExpr(SourceLoc l, ExprPtr c, ExprPtr t, ExprPtr e)
        : Expr(ExprKind::If, l), condition(std::move(c)), thenBranch(std::move(t)),
          elseBranch(std::move(e))
    {
    }
};

/// Match arm: Pattern => Expr
struct MatchArm
{
    struct Pattern
    {
        enum class Kind
        {
            Wildcard,
            Literal,
            Binding,
            Constructor,
            Tuple
        };
        Kind kind;
        std::string binding;              // For Binding and Constructor kinds
        std::vector<Pattern> subpatterns; // For Constructor and Tuple
        ExprPtr literal;                  // For Literal
        ExprPtr guard;                    // Optional guard condition
    };

    Pattern pattern;
    ExprPtr body;
};

/// Match expression
struct MatchExpr : Expr
{
    ExprPtr scrutinee;
    std::vector<MatchArm> arms;

    MatchExpr(SourceLoc l, ExprPtr s, std::vector<MatchArm> a)
        : Expr(ExprKind::Match, l), scrutinee(std::move(s)), arms(std::move(a))
    {
    }
};

//===----------------------------------------------------------------------===//
// Statement Nodes
//===----------------------------------------------------------------------===//

/// Statement kinds
enum class StmtKind
{
    Block,
    Expr,
    Var,
    If,
    While,
    For,
    ForIn,
    Return,
    Break,
    Continue,
    Guard,
    Match,
};

/// Base statement node
struct Stmt
{
    StmtKind kind;
    SourceLoc loc;

    Stmt(StmtKind k, SourceLoc l) : kind(k), loc(l) {}

    virtual ~Stmt() = default;
};

/// Block statement: { ... }
struct BlockStmt : Stmt
{
    std::vector<StmtPtr> statements;

    BlockStmt(SourceLoc l, std::vector<StmtPtr> s)
        : Stmt(StmtKind::Block, l), statements(std::move(s))
    {
    }
};

/// Block expression (block as expression)
struct BlockExpr : Expr
{
    std::vector<StmtPtr> statements;
    ExprPtr value; // Final expression (optional)

    BlockExpr(SourceLoc l, std::vector<StmtPtr> s, ExprPtr v)
        : Expr(ExprKind::Block, l), statements(std::move(s)), value(std::move(v))
    {
    }
};

/// Expression statement
struct ExprStmt : Stmt
{
    ExprPtr expr;

    ExprStmt(SourceLoc l, ExprPtr e) : Stmt(StmtKind::Expr, l), expr(std::move(e)) {}
};

/// Variable declaration: var x = 1 or final x = 1
struct VarStmt : Stmt
{
    std::string name;
    TypePtr type;        // nullptr = inferred
    ExprPtr initializer; // nullptr = default
    bool isFinal;

    VarStmt(SourceLoc l, std::string n, TypePtr t, ExprPtr init, bool final)
        : Stmt(StmtKind::Var, l), name(std::move(n)), type(std::move(t)),
          initializer(std::move(init)), isFinal(final)
    {
    }
};

/// If statement
struct IfStmt : Stmt
{
    ExprPtr condition;
    StmtPtr thenBranch;
    StmtPtr elseBranch; // nullptr if no else

    IfStmt(SourceLoc l, ExprPtr c, StmtPtr t, StmtPtr e)
        : Stmt(StmtKind::If, l), condition(std::move(c)), thenBranch(std::move(t)),
          elseBranch(std::move(e))
    {
    }
};

/// While statement
struct WhileStmt : Stmt
{
    ExprPtr condition;
    StmtPtr body;

    WhileStmt(SourceLoc l, ExprPtr c, StmtPtr b)
        : Stmt(StmtKind::While, l), condition(std::move(c)), body(std::move(b))
    {
    }
};

/// For statement: for (init; cond; update) body
struct ForStmt : Stmt
{
    StmtPtr init; // VarStmt or ExprStmt
    ExprPtr condition;
    ExprPtr update;
    StmtPtr body;

    ForStmt(SourceLoc l, StmtPtr i, ExprPtr c, ExprPtr u, StmtPtr b)
        : Stmt(StmtKind::For, l), init(std::move(i)), condition(std::move(c)), update(std::move(u)),
          body(std::move(b))
    {
    }
};

/// For-in statement: for (x in collection) body
struct ForInStmt : Stmt
{
    std::string variable;
    ExprPtr iterable;
    StmtPtr body;

    ForInStmt(SourceLoc l, std::string v, ExprPtr i, StmtPtr b)
        : Stmt(StmtKind::ForIn, l), variable(std::move(v)), iterable(std::move(i)),
          body(std::move(b))
    {
    }
};

/// Return statement
struct ReturnStmt : Stmt
{
    ExprPtr value; // nullptr for void return

    ReturnStmt(SourceLoc l, ExprPtr v) : Stmt(StmtKind::Return, l), value(std::move(v)) {}
};

/// Break statement
struct BreakStmt : Stmt
{
    BreakStmt(SourceLoc l) : Stmt(StmtKind::Break, l) {}
};

/// Continue statement
struct ContinueStmt : Stmt
{
    ContinueStmt(SourceLoc l) : Stmt(StmtKind::Continue, l) {}
};

/// Guard statement: guard (cond) else { return }
struct GuardStmt : Stmt
{
    ExprPtr condition;
    StmtPtr elseBlock;

    GuardStmt(SourceLoc l, ExprPtr c, StmtPtr e)
        : Stmt(StmtKind::Guard, l), condition(std::move(c)), elseBlock(std::move(e))
    {
    }
};

/// Match statement (when not used as expression)
struct MatchStmt : Stmt
{
    ExprPtr scrutinee;
    std::vector<MatchArm> arms;

    MatchStmt(SourceLoc l, ExprPtr s, std::vector<MatchArm> a)
        : Stmt(StmtKind::Match, l), scrutinee(std::move(s)), arms(std::move(a))
    {
    }
};

//===----------------------------------------------------------------------===//
// Declaration Nodes
//===----------------------------------------------------------------------===//

/// Declaration kinds
enum class DeclKind
{
    Module,
    Import,
    Value,
    Entity,
    Interface,
    Function,
    Field,
    Method,
    Constructor,
};

/// Visibility
enum class Visibility
{
    Private, // default for entity fields
    Public,  // default for value fields, exposed
};

/// Base declaration node
struct Decl
{
    DeclKind kind;
    SourceLoc loc;

    Decl(DeclKind k, SourceLoc l) : kind(k), loc(l) {}

    virtual ~Decl() = default;
};

/// Function parameter
struct Param
{
    std::string name;
    TypePtr type;
    ExprPtr defaultValue; // nullptr if no default
};

/// Function declaration
struct FunctionDecl : Decl
{
    std::string name;
    std::vector<std::string> genericParams; // [T, U]
    std::vector<Param> params;
    TypePtr returnType; // nullptr = void
    StmtPtr body;       // nullptr for interface methods
    Visibility visibility = Visibility::Private;
    bool isOverride = false;

    FunctionDecl(SourceLoc l, std::string n) : Decl(DeclKind::Function, l), name(std::move(n)) {}
};

/// Field declaration (in value/entity)
struct FieldDecl : Decl
{
    std::string name;
    TypePtr type;
    ExprPtr initializer; // nullptr = default
    Visibility visibility = Visibility::Private;
    bool isFinal = false;
    bool isWeak = false;

    FieldDecl(SourceLoc l, std::string n) : Decl(DeclKind::Field, l), name(std::move(n)) {}
};

/// Method declaration (function in value/entity)
struct MethodDecl : Decl
{
    std::string name;
    std::vector<std::string> genericParams;
    std::vector<Param> params;
    TypePtr returnType;
    StmtPtr body;
    Visibility visibility = Visibility::Private;
    bool isOverride = false;

    MethodDecl(SourceLoc l, std::string n) : Decl(DeclKind::Method, l), name(std::move(n)) {}
};

/// Constructor declaration
struct ConstructorDecl : Decl
{
    std::vector<Param> params;
    StmtPtr body;
    Visibility visibility = Visibility::Public;

    ConstructorDecl(SourceLoc l) : Decl(DeclKind::Constructor, l) {}
};

/// Value type declaration
struct ValueDecl : Decl
{
    std::string name;
    std::vector<std::string> genericParams;
    std::vector<std::string> interfaces; // implements
    std::vector<DeclPtr> members;        // fields and methods

    ValueDecl(SourceLoc l, std::string n) : Decl(DeclKind::Value, l), name(std::move(n)) {}
};

/// Entity type declaration
struct EntityDecl : Decl
{
    std::string name;
    std::vector<std::string> genericParams;
    std::string baseClass;               // extends (empty = none)
    std::vector<std::string> interfaces; // implements
    std::vector<DeclPtr> members;        // fields and methods

    EntityDecl(SourceLoc l, std::string n) : Decl(DeclKind::Entity, l), name(std::move(n)) {}
};

/// Interface declaration
struct InterfaceDecl : Decl
{
    std::string name;
    std::vector<std::string> genericParams;
    std::vector<DeclPtr> members; // method signatures only

    InterfaceDecl(SourceLoc l, std::string n) : Decl(DeclKind::Interface, l), name(std::move(n)) {}
};

/// Import declaration
struct ImportDecl : Decl
{
    std::string path;  // e.g., "Viper.IO.File"
    std::string alias; // empty if no alias

    ImportDecl(SourceLoc l, std::string p) : Decl(DeclKind::Import, l), path(std::move(p)) {}
};

/// Module declaration (top-level)
struct ModuleDecl : Decl
{
    std::string name;
    std::vector<ImportDecl> imports;
    std::vector<DeclPtr> declarations;

    ModuleDecl(SourceLoc l, std::string n) : Decl(DeclKind::Module, l), name(std::move(n)) {}
};

} // namespace il::frontends::viperlang
