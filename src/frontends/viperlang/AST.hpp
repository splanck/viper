//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file AST.hpp
/// @brief Abstract Syntax Tree types for the ViperLang programming language.
///
/// @details This file defines the complete Abstract Syntax Tree (AST) node
/// hierarchy for ViperLang, a modern object-oriented language with value and
/// reference semantics. The AST is produced by the Parser and consumed by
/// the semantic analyzer (Sema) and IL lowerer (Lowerer).
///
/// ## Design Overview
///
/// The AST is organized into four main categories:
///
/// **1. Type Nodes (TypeNode hierarchy)**
/// Represent type annotations in the source code, such as:
/// - Named types: `Integer`, `String`, `MyClass`
/// - Generic types: `List[T]`, `Map[K, V]`
/// - Optional types: `T?`
/// - Function types: `(A, B) -> C`
/// - Tuple types: `(A, B)`
///
/// **2. Expression Nodes (Expr hierarchy)**
/// Represent expressions that compute values:
/// - Literals: integers, floats, strings, booleans, null
/// - Operations: binary, unary, ternary, range
/// - Access: identifiers, field access, indexing
/// - Calls: function/method invocation, constructor calls
/// - Control flow expressions: if-else, match, block expressions
///
/// **3. Statement Nodes (Stmt hierarchy)**
/// Represent statements that perform actions:
/// - Control flow: if, while, for, for-in, guard, match
/// - Declarations: var, final
/// - Jumps: return, break, continue
/// - Expression statements
///
/// **4. Declaration Nodes (Decl hierarchy)**
/// Represent top-level and type member declarations:
/// - Types: value, entity, interface
/// - Functions: global functions, methods, constructors
/// - Members: fields
/// - Modules: module declaration, imports
///
/// ## Ownership Model
///
/// All AST nodes own their children via `std::unique_ptr`. When a node is
/// destroyed, all its children are automatically cleaned up. The parser
/// owns the root ModuleDecl, which transitively owns the entire tree.
///
/// ## Memory Layout
///
/// Each node contains:
/// - A `kind` field identifying the specific node type (for safe downcasting)
/// - A `loc` field with source location information for error messages
/// - Type-specific data fields
///
/// ## Type Aliases
///
/// For convenience, smart pointer aliases are provided:
/// - `ExprPtr` = `std::unique_ptr<Expr>`
/// - `StmtPtr` = `std::unique_ptr<Stmt>`
/// - `TypePtr` = `std::unique_ptr<TypeNode>`
/// - `DeclPtr` = `std::unique_ptr<Decl>`
///
/// ## Usage Example
///
/// ```cpp
/// // Creating an integer literal expression
/// auto intExpr = std::make_unique<IntLiteralExpr>(loc, 42);
///
/// // Creating a binary addition expression
/// auto addExpr = std::make_unique<BinaryExpr>(
///     loc, BinaryOp::Add,
///     std::move(leftExpr),
///     std::move(rightExpr)
/// );
///
/// // Downcasting based on kind
/// if (expr->kind == ExprKind::Binary) {
///     auto *binary = static_cast<BinaryExpr*>(expr);
///     // ... process binary expression
/// }
/// ```
///
/// @invariant All AST nodes own their children via unique_ptr.
/// @invariant Every node has a valid source location for error reporting.
/// @invariant Node kind field matches the actual derived type.
///
/// @see Parser.hpp - Creates AST from tokens
/// @see Sema.hpp - Performs semantic analysis on AST
/// @see Lowerer.hpp - Converts AST to intermediate language
///
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

//===----------------------------------------------------------------------===//
/// @name Forward Declarations
/// @brief Forward declarations for AST node types and smart pointer aliases.
/// @details These enable circular references between node types (e.g., an
/// expression containing a block that contains statements).
/// @{
//===----------------------------------------------------------------------===//

struct Expr;
struct Stmt;
struct TypeNode;
struct Decl;

/// @brief Unique pointer to an expression node.
/// @details Expressions compute values and can be nested arbitrarily deep.
using ExprPtr = std::unique_ptr<Expr>;

/// @brief Unique pointer to a statement node.
/// @details Statements perform actions and may contain expressions.
using StmtPtr = std::unique_ptr<Stmt>;

/// @brief Unique pointer to a type annotation node.
/// @details Type nodes appear in variable declarations, function signatures,
/// and type casts. They represent syntactic type annotations, not resolved
/// semantic types (see Types.hpp for semantic types).
using TypePtr = std::unique_ptr<TypeNode>;

/// @brief Unique pointer to a declaration node.
/// @details Declarations introduce named entities: types, functions, fields.
using DeclPtr = std::unique_ptr<Decl>;

/// @}

//===----------------------------------------------------------------------===//
/// @name Source Location
/// @brief Type alias for source location tracking.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Source location for error messages and debugging.
/// @details Each AST node stores its source location to enable accurate
/// error reporting and source mapping during lowering.
using SourceLoc = il::support::SourceLoc;

/// @}

//===----------------------------------------------------------------------===//
/// @name Type Nodes
/// @brief AST nodes representing type annotations in source code.
/// @details These represent the syntactic form of types as written by the
/// programmer. The semantic analyzer resolves these to ViperType instances.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Enumerates the kinds of type annotation nodes.
/// @details Used for runtime type identification when processing type nodes.
/// Each TypeKind corresponds to exactly one TypeNode subclass.
enum class TypeKind
{
    /// @brief Simple named type reference.
    /// @details Examples: `Integer`, `String`, `MyClass`
    /// @see NamedType
    Named,

    /// @brief Parameterized generic type.
    /// @details Examples: `List[T]`, `Map[K, V]`, `Result[Success]`
    /// @see GenericType
    Generic,

    /// @brief Optional (nullable) type wrapper.
    /// @details Syntax: `T?` where T is any type.
    /// An optional type can hold either a value of type T or null.
    /// @see OptionalType
    Optional,

    /// @brief Function type with parameters and return type.
    /// @details Syntax: `(A, B) -> C` for a function taking A and B, returning C.
    /// Used for function references, lambdas, and closures.
    /// @see FunctionType
    Function,

    /// @brief Tuple type grouping multiple types.
    /// @details Syntax: `(A, B)` for a tuple containing A and B.
    /// Currently used primarily in pattern matching.
    /// @see TupleType
    Tuple,
};

/// @brief Base class for all type annotation nodes.
/// @details Type nodes represent type expressions as written in source code.
/// They are parsed from type annotations and later resolved to semantic
/// ViperType instances during semantic analysis.
///
/// ## Subclasses
/// - NamedType: Simple type names like `Integer` or `MyClass`
/// - GenericType: Parameterized types like `List[T]`
/// - OptionalType: Nullable types like `String?`
/// - FunctionType: Function signatures like `(Int) -> Bool`
/// - TupleType: Tuple types like `(Int, String)`
///
/// @invariant `kind` correctly identifies the concrete subclass type.
struct TypeNode
{
    /// @brief Identifies the concrete type node kind for downcasting.
    TypeKind kind;

    /// @brief Source location where this type annotation appears.
    SourceLoc loc;

    /// @brief Construct a type node with the given kind and location.
    /// @param k The specific type node kind.
    /// @param l Source location of the type annotation.
    TypeNode(TypeKind k, SourceLoc l) : kind(k), loc(l) {}

    /// @brief Virtual destructor for proper polymorphic cleanup.
    virtual ~TypeNode() = default;
};

/// @brief Named type reference: `Integer`, `String`, `MyClass`.
/// @details Represents a simple type reference by name. During semantic
/// analysis, the name is resolved to a built-in type, value type, entity
/// type, or interface type.
///
/// ## Examples
/// - `Integer` - Built-in 64-bit signed integer
/// - `String` - Built-in UTF-8 string type
/// - `Player` - User-defined entity type
/// - `Point` - User-defined value type
struct NamedType : TypeNode
{
    /// @brief The type name as written in source code.
    /// @details Must be resolved during semantic analysis to determine
    /// what type it actually refers to.
    std::string name;

    /// @brief Construct a named type reference.
    /// @param l Source location of the type name.
    /// @param n The type name (e.g., "Integer", "MyClass").
    NamedType(SourceLoc l, std::string n) : TypeNode(TypeKind::Named, l), name(std::move(n)) {}
};

/// @brief Parameterized generic type: `List[T]`, `Map[K, V]`.
/// @details Represents a generic type with one or more type arguments.
/// The base type name is resolved to a generic type definition, and the
/// type arguments are substituted for the type parameters.
///
/// ## Examples
/// - `List[Integer]` - List containing integers
/// - `Map[String, Integer]` - Map from strings to integers
/// - `Result[User]` - Result type with User as success type
struct GenericType : TypeNode
{
    /// @brief The generic type name (e.g., "List", "Map", "Result").
    std::string name;

    /// @brief The type arguments provided within brackets.
    /// @details For `Map[String, Integer]`, this would contain two elements:
    /// a NamedType("String") and a NamedType("Integer").
    std::vector<TypePtr> args;

    /// @brief Construct a generic type with type arguments.
    /// @param l Source location of the type.
    /// @param n The generic type name.
    /// @param a The type arguments.
    GenericType(SourceLoc l, std::string n, std::vector<TypePtr> a)
        : TypeNode(TypeKind::Generic, l), name(std::move(n)), args(std::move(a))
    {
    }
};

/// @brief Optional (nullable) type wrapper: `T?`.
/// @details Represents a type that can hold either a value of the inner
/// type or null. Optional types are fundamental for null-safety in ViperLang.
///
/// ## Examples
/// - `String?` - Optional string (may be null)
/// - `Integer?` - Optional integer (may be null)
/// - `List[User]?` - Optional list (the list itself may be null)
///
/// ## Semantic Behavior
/// Optional types enable:
/// - Explicit null handling with `??` (coalesce) operator
/// - Safe chaining with `?.` (optional chain) operator
/// - Pattern matching for null checks
struct OptionalType : TypeNode
{
    /// @brief The underlying type that is made optional.
    /// @details For `String?`, this points to a NamedType("String").
    TypePtr inner;

    /// @brief Construct an optional type wrapper.
    /// @param l Source location of the type.
    /// @param i The inner type being made optional.
    OptionalType(SourceLoc l, TypePtr i) : TypeNode(TypeKind::Optional, l), inner(std::move(i)) {}
};

/// @brief Function type with parameter and return types: `(A, B) -> C`.
/// @details Represents the type signature of a function, method, or lambda.
/// Function types are used for:
/// - Function reference parameters (higher-order functions)
/// - Lambda expression types
/// - Closure types captured from surrounding scope
///
/// ## Examples
/// - `(Integer) -> Boolean` - Function taking int, returning bool
/// - `(String, Integer) -> String` - Function taking string and int
/// - `() -> Unit` - Function taking nothing, returning unit (void-like)
struct FunctionType : TypeNode
{
    /// @brief The parameter types in order.
    /// @details Each element is the type of one parameter.
    std::vector<TypePtr> params;

    /// @brief The return type, or nullptr for void functions.
    /// @details A nullptr return type indicates the function returns nothing
    /// (void in C terminology). Use Unit type for functions that explicitly
    /// return the unit value.
    TypePtr returnType;

    /// @brief Construct a function type.
    /// @param l Source location of the type.
    /// @param p Parameter types.
    /// @param ret Return type (nullptr for void).
    FunctionType(SourceLoc l, std::vector<TypePtr> p, TypePtr ret)
        : TypeNode(TypeKind::Function, l), params(std::move(p)), returnType(std::move(ret))
    {
    }
};

/// @brief Tuple type grouping multiple types: `(A, B)`.
/// @details Represents an ordered collection of potentially different types.
/// Tuples are primarily used in pattern matching and multi-value returns.
///
/// ## Examples
/// - `(Integer, String)` - Pair of integer and string
/// - `(Boolean, Integer, String)` - Triple of three different types
struct TupleType : TypeNode
{
    /// @brief The element types in order.
    std::vector<TypePtr> elements;

    /// @brief Construct a tuple type.
    /// @param l Source location of the type.
    /// @param e Element types.
    TupleType(SourceLoc l, std::vector<TypePtr> e)
        : TypeNode(TypeKind::Tuple, l), elements(std::move(e))
    {
    }
};

/// @}

//===----------------------------------------------------------------------===//
/// @name Expression Nodes
/// @brief AST nodes representing expressions that compute values.
/// @details Expressions are the core of computation in ViperLang. They can
/// be nested arbitrarily deep and include operators, function calls, field
/// access, and control flow constructs that return values.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Enumerates all kinds of expression nodes.
/// @details Used for runtime type identification when processing expressions.
/// The enum is grouped by category for clarity:
/// - Literals: constant values
/// - Names: identifier references
/// - Operations: computations with operators
/// - Construction: creating new values/objects
/// - Control flow: expressions with branching
enum class ExprKind
{
    // =========================================================================
    /// @name Literal Expressions
    /// @brief Constant values embedded directly in source code.
    /// @{
    // =========================================================================

    /// @brief 64-bit signed integer literal: `42`, `0xFF`, `0b1010`.
    /// @see IntLiteralExpr
    IntLiteral,

    /// @brief 64-bit floating-point literal: `3.14`, `1e-5`.
    /// @see NumberLiteralExpr
    NumberLiteral,

    /// @brief String literal: `"hello"`, including interpolated strings.
    /// @see StringLiteralExpr
    StringLiteral,

    /// @brief Boolean literal: `true` or `false`.
    /// @see BoolLiteralExpr
    BoolLiteral,

    /// @brief Null literal: `null`.
    /// @see NullLiteralExpr
    NullLiteral,

    /// @brief Unit literal: `()` - the singleton unit value.
    /// @see UnitLiteralExpr
    UnitLiteral,

    /// @}
    // =========================================================================
    /// @name Name Expressions
    /// @brief References to named entities.
    /// @{
    // =========================================================================

    /// @brief Identifier reference: `foo`, `myVariable`.
    /// @see IdentExpr
    Ident,

    /// @brief Self reference within a method: `self`.
    /// @see SelfExpr
    SelfExpr,

    /// @brief Parent class reference: `super`.
    /// @see SuperExprNode
    SuperExpr,

    /// @}
    // =========================================================================
    /// @name Operator Expressions
    /// @brief Expressions involving operators.
    /// @{
    // =========================================================================

    /// @brief Binary operation: `a + b`, `x && y`, `i = 5`.
    /// @see BinaryExpr
    Binary,

    /// @brief Unary operation: `-a`, `!b`, `~c`.
    /// @see UnaryExpr
    Unary,

    /// @brief Ternary conditional: `a ? b : c`.
    /// @see TernaryExpr
    Ternary,

    /// @brief Function/method call: `f(x, y)`, `obj.method(arg)`.
    /// @see CallExpr
    Call,

    /// @brief Array/collection indexing: `arr[i]`, `map[key]`.
    /// @see IndexExpr
    Index,

    /// @brief Field access: `obj.field`.
    /// @see FieldExpr
    Field,

    /// @brief Safe optional chain: `obj?.field` - returns null if obj is null.
    /// @see OptionalChainExpr
    OptionalChain,

    /// @brief Null coalescing: `a ?? b` - returns b if a is null.
    /// @see CoalesceExpr
    Coalesce,

    /// @brief Type check: `x is T` - tests if x is of type T.
    /// @see IsExpr
    Is,

    /// @brief Type cast: `x as T` - casts x to type T.
    /// @see AsExpr
    As,

    /// @brief Range expression: `a..b` or `a..=b`.
    /// @see RangeExpr
    Range,

    /// @brief Try/propagate expression: `expr?` - propagates null/error.
    /// @details Used with Result and Optional types to short-circuit
    /// on error or null, returning early from the enclosing function.
    /// @see TryExpr
    Try,

    /// @}
    // =========================================================================
    /// @name Construction Expressions
    /// @brief Expressions that create new values or objects.
    /// @{
    // =========================================================================

    /// @brief Object instantiation: `new Foo(args)`.
    /// @see NewExpr
    New,

    /// @brief Anonymous function: `(x) => x + 1`.
    /// @see LambdaExpr
    Lambda,

    /// @brief List literal: `[1, 2, 3]`.
    /// @see ListLiteralExpr
    ListLiteral,

    /// @brief Map literal: `{"a": 1, "b": 2}`.
    /// @see MapLiteralExpr
    MapLiteral,

    /// @brief Set literal: `{1, 2, 3}` (when not a map).
    /// @see SetLiteralExpr
    SetLiteral,

    /// @brief Tuple literal: `(1, "hello", true)`.
    /// @see TupleExpr
    Tuple,

    /// @brief Tuple element access: `tuple.0`, `tuple.1`.
    /// @see TupleIndexExpr
    TupleIndex,

    /// @}
    // =========================================================================
    /// @name Control Flow Expressions
    /// @brief Expressions with branching that return values.
    /// @{
    // =========================================================================

    /// @brief Conditional expression: `if (c) a else b`.
    /// @details Unlike if-statements, if-expressions require an else branch
    /// and evaluate to a value.
    /// @see IfExpr
    If,

    /// @brief Pattern matching expression: `match x { ... }`.
    /// @see MatchExpr
    Match,

    /// @brief Block expression: `{ stmts; expr }`.
    /// @details A block with a trailing expression evaluates to that expression.
    /// @see BlockExpr
    Block,

    /// @}
};

/// @brief Base class for all expression nodes.
/// @details Expressions compute values and can be composed arbitrarily.
/// Each expression has a source location and a kind for identification.
///
/// ## Type Resolution
/// During semantic analysis, each expression is assigned a ViperType
/// indicating the type of value it produces. This is stored in the
/// Sema's expression type map, not in the AST node itself.
///
/// ## Subclass Categories
/// - Literals: IntLiteralExpr, NumberLiteralExpr, StringLiteralExpr, etc.
/// - Names: IdentExpr, SelfExpr, SuperExprNode
/// - Operators: BinaryExpr, UnaryExpr, TernaryExpr
/// - Access: FieldExpr, IndexExpr, CallExpr
/// - Construction: NewExpr, LambdaExpr, ListLiteralExpr
/// - Control: IfExpr, MatchExpr, BlockExpr
///
/// @invariant `kind` correctly identifies the concrete subclass type.
struct Expr
{
    /// @brief Identifies the concrete expression kind for downcasting.
    ExprKind kind;

    /// @brief Source location of this expression.
    SourceLoc loc;

    /// @brief Construct an expression with kind and location.
    /// @param k The specific expression kind.
    /// @param l Source location of the expression.
    Expr(ExprKind k, SourceLoc l) : kind(k), loc(l) {}

    /// @brief Virtual destructor for proper polymorphic cleanup.
    virtual ~Expr() = default;
};

/// @brief 64-bit signed integer literal: `42`, `0xFF`, `0b1010`.
/// @details Represents compile-time integer constants. The lexer handles
/// decimal, hexadecimal (0x), and binary (0b) formats.
///
/// ## Examples
/// - `42` - Decimal integer
/// - `0xFF` - Hexadecimal (255 in decimal)
/// - `0b1010` - Binary (10 in decimal)
/// - `-123` - Negative integer (actually a unary minus on 123)
struct IntLiteralExpr : Expr
{
    /// @brief The integer value.
    int64_t value;

    /// @brief Construct an integer literal.
    /// @param l Source location.
    /// @param v The integer value.
    IntLiteralExpr(SourceLoc l, int64_t v) : Expr(ExprKind::IntLiteral, l), value(v) {}
};

/// @brief 64-bit floating-point literal: `3.14`, `1e-5`.
/// @details Represents compile-time floating-point constants.
/// Scientific notation with optional exponent is supported.
///
/// ## Examples
/// - `3.14159` - Simple decimal
/// - `1e10` - Scientific notation (1 × 10^10)
/// - `2.5e-3` - Scientific with negative exponent (0.0025)
struct NumberLiteralExpr : Expr
{
    /// @brief The floating-point value.
    double value;

    /// @brief Construct a number literal.
    /// @param l Source location.
    /// @param v The floating-point value.
    NumberLiteralExpr(SourceLoc l, double v) : Expr(ExprKind::NumberLiteral, l), value(v) {}
};

/// @brief String literal: `"hello"`, with interpolation support.
/// @details Represents string constants. Strings support:
/// - Escape sequences: `\n`, `\t`, `\\`, `\"`, `\$`
/// - Interpolation: `"Hello ${name}!"` embeds expressions
///
/// ## String Interpolation
/// Interpolated strings are desugared during parsing into a series of
/// string concatenation operations. This node represents the final
/// resolved string value after interpolation processing.
struct StringLiteralExpr : Expr
{
    /// @brief The string value with escapes processed.
    std::string value;

    /// @brief Construct a string literal.
    /// @param l Source location.
    /// @param v The string value.
    StringLiteralExpr(SourceLoc l, std::string v)
        : Expr(ExprKind::StringLiteral, l), value(std::move(v))
    {
    }
};

/// @brief Boolean literal: `true` or `false`.
/// @details Represents the two boolean constant values.
struct BoolLiteralExpr : Expr
{
    /// @brief The boolean value.
    bool value;

    /// @brief Construct a boolean literal.
    /// @param l Source location.
    /// @param v The boolean value.
    BoolLiteralExpr(SourceLoc l, bool v) : Expr(ExprKind::BoolLiteral, l), value(v) {}
};

/// @brief Null literal: `null`.
/// @details Represents the absence of a value for optional types.
/// Only valid where an optional type is expected.
struct NullLiteralExpr : Expr
{
    /// @brief Construct a null literal.
    /// @param l Source location.
    NullLiteralExpr(SourceLoc l) : Expr(ExprKind::NullLiteral, l) {}
};

/// @brief Unit literal: `()`.
/// @details Represents the singleton unit value, similar to void but
/// with an actual value. Used with Result[Unit] for operations that
/// succeed but return no meaningful data.
struct UnitLiteralExpr : Expr
{
    /// @brief Construct a unit literal.
    /// @param l Source location.
    UnitLiteralExpr(SourceLoc l) : Expr(ExprKind::UnitLiteral, l) {}
};

/// @brief Identifier expression: `foo`, `myVariable`.
/// @details References a named entity: variable, parameter, function, or type.
/// The semantic analyzer resolves the name to its definition.
struct IdentExpr : Expr
{
    /// @brief The identifier name.
    std::string name;

    /// @brief Construct an identifier expression.
    /// @param l Source location.
    /// @param n The identifier name.
    IdentExpr(SourceLoc l, std::string n) : Expr(ExprKind::Ident, l), name(std::move(n)) {}
};

/// @brief Self reference within methods: `self`.
/// @details References the current object instance within a method.
/// Only valid inside method bodies of value or entity types.
struct SelfExpr : Expr
{
    /// @brief Construct a self expression.
    /// @param l Source location.
    SelfExpr(SourceLoc l) : Expr(ExprKind::SelfExpr, l) {}
};

/// @brief Parent class reference: `super`.
/// @details References the parent class for calling overridden methods
/// or accessing inherited members. Only valid in entity types that
/// extend another entity.
struct SuperExprNode : Expr
{
    /// @brief Construct a super expression.
    /// @param l Source location.
    SuperExprNode(SourceLoc l) : Expr(ExprKind::SuperExpr, l) {}
};

/// @brief Binary operators for BinaryExpr.
/// @details Organized by category: arithmetic, comparison, logical, bitwise.
enum class BinaryOp
{
    // Arithmetic operators
    Add, ///< Addition: `a + b`
    Sub, ///< Subtraction: `a - b`
    Mul, ///< Multiplication: `a * b`
    Div, ///< Division: `a / b`
    Mod, ///< Modulo: `a % b`

    // Comparison operators
    Eq, ///< Equality: `a == b`
    Ne, ///< Inequality: `a != b`
    Lt, ///< Less than: `a < b`
    Le, ///< Less or equal: `a <= b`
    Gt, ///< Greater than: `a > b`
    Ge, ///< Greater or equal: `a >= b`

    // Logical operators
    And, ///< Logical AND: `a && b` (short-circuiting)
    Or,  ///< Logical OR: `a || b` (short-circuiting)

    // Bitwise operators
    BitAnd, ///< Bitwise AND: `a & b`
    BitOr,  ///< Bitwise OR: `a | b`
    BitXor, ///< Bitwise XOR: `a ^ b`

    // Assignment
    Assign, ///< Assignment: `a = b`
};

/// @brief Binary operation expression: `a + b`, `x && y`, `i = 5`.
/// @details Represents operations with two operands. The operator determines
/// the semantics: arithmetic, comparison, logical, bitwise, or assignment.
///
/// ## Precedence
/// Binary expressions are parsed with precedence climbing:
/// 1. Multiplicative: `*`, `/`, `%`
/// 2. Additive: `+`, `-`
/// 3. Comparison: `<`, `>`, `<=`, `>=`
/// 4. Equality: `==`, `!=`
/// 5. Logical AND: `&&`
/// 6. Logical OR: `||`
///
/// ## Short-Circuit Evaluation
/// Logical AND and OR use short-circuit evaluation:
/// - `a && b`: b is only evaluated if a is true
/// - `a || b`: b is only evaluated if a is false
struct BinaryExpr : Expr
{
    /// @brief The binary operator.
    BinaryOp op;

    /// @brief The left operand.
    ExprPtr left;

    /// @brief The right operand.
    ExprPtr right;

    /// @brief Construct a binary expression.
    /// @param l Source location.
    /// @param o The operator.
    /// @param lhs Left operand.
    /// @param rhs Right operand.
    BinaryExpr(SourceLoc l, BinaryOp o, ExprPtr lhs, ExprPtr rhs)
        : Expr(ExprKind::Binary, l), op(o), left(std::move(lhs)), right(std::move(rhs))
    {
    }
};

/// @brief Unary operators for UnaryExpr.
enum class UnaryOp
{
    Neg,    ///< Arithmetic negation: `-a`
    Not,    ///< Logical NOT: `!a`
    BitNot, ///< Bitwise NOT: `~a`
};

/// @brief Unary operation expression: `-a`, `!b`, `~c`.
/// @details Represents operations with a single operand.
struct UnaryExpr : Expr
{
    /// @brief The unary operator.
    UnaryOp op;

    /// @brief The operand.
    ExprPtr operand;

    /// @brief Construct a unary expression.
    /// @param l Source location.
    /// @param o The operator.
    /// @param e The operand.
    UnaryExpr(SourceLoc l, UnaryOp o, ExprPtr e)
        : Expr(ExprKind::Unary, l), op(o), operand(std::move(e))
    {
    }
};

/// @brief Ternary conditional expression: `a ? b : c`.
/// @details Evaluates condition, then returns thenExpr if true, elseExpr if false.
/// Both branches must have compatible types.
struct TernaryExpr : Expr
{
    /// @brief The condition to test.
    ExprPtr condition;

    /// @brief Expression to evaluate if condition is true.
    ExprPtr thenExpr;

    /// @brief Expression to evaluate if condition is false.
    ExprPtr elseExpr;

    /// @brief Construct a ternary expression.
    /// @param l Source location.
    /// @param c Condition.
    /// @param t Then branch.
    /// @param e Else branch.
    TernaryExpr(SourceLoc l, ExprPtr c, ExprPtr t, ExprPtr e)
        : Expr(ExprKind::Ternary, l), condition(std::move(c)), thenExpr(std::move(t)),
          elseExpr(std::move(e))
    {
    }
};

/// @brief Named or positional argument in a function call.
/// @details ViperLang supports both positional and named arguments.
/// Named arguments improve readability for functions with many parameters.
///
/// ## Examples
/// - `f(1, 2)` - Two positional arguments
/// - `f(x: 1, y: 2)` - Two named arguments
/// - `f(1, y: 2)` - Mixed positional and named
struct CallArg
{
    /// @brief The argument name if using named syntax, nullopt for positional.
    std::optional<std::string> name;

    /// @brief The argument value expression.
    ExprPtr value;
};

/// @brief Function/method call expression: `f(x, y)`, `obj.method(arg)`.
/// @details Represents invocation of a callable with arguments.
/// The callee can be:
/// - An identifier (function name)
/// - A field expression (method call)
/// - Any expression evaluating to a callable type (lambda)
struct CallExpr : Expr
{
    /// @brief The expression being called.
    ExprPtr callee;

    /// @brief The arguments passed to the call.
    std::vector<CallArg> args;

    /// @brief Construct a call expression.
    /// @param l Source location.
    /// @param c The callee expression.
    /// @param a The argument list.
    CallExpr(SourceLoc l, ExprPtr c, std::vector<CallArg> a)
        : Expr(ExprKind::Call, l), callee(std::move(c)), args(std::move(a))
    {
    }
};

/// @brief Array/collection indexing expression: `arr[i]`, `map[key]`.
/// @details Accesses an element from a collection by index or key.
/// Works with List (integer index), Map (key lookup), and String (character).
struct IndexExpr : Expr
{
    /// @brief The collection being indexed.
    ExprPtr base;

    /// @brief The index or key expression.
    ExprPtr index;

    /// @brief Construct an index expression.
    /// @param l Source location.
    /// @param b The base collection.
    /// @param i The index/key.
    IndexExpr(SourceLoc l, ExprPtr b, ExprPtr i)
        : Expr(ExprKind::Index, l), base(std::move(b)), index(std::move(i))
    {
    }
};

/// @brief Field access expression: `obj.field`.
/// @details Accesses a field or property from a value or entity type.
/// Also used for accessing static members and module-level items.
struct FieldExpr : Expr
{
    /// @brief The object expression.
    ExprPtr base;

    /// @brief The field name being accessed.
    std::string field;

    /// @brief Construct a field expression.
    /// @param l Source location.
    /// @param b The base object.
    /// @param f The field name.
    FieldExpr(SourceLoc l, ExprPtr b, std::string f)
        : Expr(ExprKind::Field, l), base(std::move(b)), field(std::move(f))
    {
    }
};

/// @brief Safe optional chain expression: `obj?.field`.
/// @details Safely accesses a field from an optional type. If the base
/// is null, the entire expression evaluates to null instead of crashing.
///
/// ## Example
/// ```
/// var user: User? = getUser();
/// var name = user?.name;  // String? - null if user is null
/// ```
struct OptionalChainExpr : Expr
{
    /// @brief The optional object expression.
    ExprPtr base;

    /// @brief The field to access if base is not null.
    std::string field;

    /// @brief Construct an optional chain expression.
    /// @param l Source location.
    /// @param b The optional base.
    /// @param f The field name.
    OptionalChainExpr(SourceLoc l, ExprPtr b, std::string f)
        : Expr(ExprKind::OptionalChain, l), base(std::move(b)), field(std::move(f))
    {
    }
};

/// @brief Null coalescing expression: `a ?? b`.
/// @details Returns the left operand if it's not null, otherwise returns
/// the right operand. The right operand is only evaluated if needed.
///
/// ## Example
/// ```
/// var name = user?.name ?? "Anonymous";
/// ```
struct CoalesceExpr : Expr
{
    /// @brief The primary value (may be null).
    ExprPtr left;

    /// @brief The fallback value if left is null.
    ExprPtr right;

    /// @brief Construct a coalesce expression.
    /// @param l Source location.
    /// @param lhs Primary value.
    /// @param rhs Fallback value.
    CoalesceExpr(SourceLoc l, ExprPtr lhs, ExprPtr rhs)
        : Expr(ExprKind::Coalesce, l), left(std::move(lhs)), right(std::move(rhs))
    {
    }
};

/// @brief Type check expression: `x is T`.
/// @details Tests at runtime whether a value is of a specific type.
/// Returns true if x is of type T, false otherwise.
///
/// ## Usage
/// Used with entity types to check for subtypes before casting:
/// ```
/// if (animal is Dog) {
///     var dog = animal as Dog;
///     dog.bark();
/// }
/// ```
struct IsExpr : Expr
{
    /// @brief The value to check.
    ExprPtr value;

    /// @brief The type to test against.
    TypePtr type;

    /// @brief Construct an is-expression.
    /// @param l Source location.
    /// @param v The value to check.
    /// @param t The type to test.
    IsExpr(SourceLoc l, ExprPtr v, TypePtr t)
        : Expr(ExprKind::Is, l), value(std::move(v)), type(std::move(t))
    {
    }
};

/// @brief Type cast expression: `x as T`.
/// @details Casts a value to a specific type. The cast may be:
/// - Checked: For entity types, throws if the cast fails
/// - Unchecked: For value types, assumes the programmer knows the type
///
/// ## Example
/// ```
/// var dog = animal as Dog;  // Throws if not a Dog
/// ```
struct AsExpr : Expr
{
    /// @brief The value to cast.
    ExprPtr value;

    /// @brief The target type.
    TypePtr type;

    /// @brief Construct an as-expression.
    /// @param l Source location.
    /// @param v The value to cast.
    /// @param t The target type.
    AsExpr(SourceLoc l, ExprPtr v, TypePtr t)
        : Expr(ExprKind::As, l), value(std::move(v)), type(std::move(t))
    {
    }
};

/// @brief Range expression: `a..b` or `a..=b`.
/// @details Creates a range of values from start to end.
/// - `a..b`: Exclusive range [a, b)
/// - `a..=b`: Inclusive range [a, b]
///
/// ## Usage
/// Primarily used in for-in loops:
/// ```
/// for (i in 0..10) { ... }     // 0 to 9
/// for (i in 0..=10) { ... }    // 0 to 10
/// ```
struct RangeExpr : Expr
{
    /// @brief The start of the range.
    ExprPtr start;

    /// @brief The end of the range.
    ExprPtr end;

    /// @brief True for inclusive (`..=`), false for exclusive (`..`).
    bool inclusive;

    /// @brief Construct a range expression.
    /// @param l Source location.
    /// @param s Start of range.
    /// @param e End of range.
    /// @param incl True if inclusive.
    RangeExpr(SourceLoc l, ExprPtr s, ExprPtr e, bool incl)
        : Expr(ExprKind::Range, l), start(std::move(s)), end(std::move(e)), inclusive(incl)
    {
    }
};

/// @brief Try/propagate expression: `expr?`.
/// @details Used with Optional and Result types to propagate null/error
/// to the enclosing function. If the expression is null/error, the
/// enclosing function returns early with the same null/error.
///
/// ## Example
/// ```
/// func getUsername(): String? {
///     var user = getUser()?;  // Returns null if getUser() returns null
///     return user.name;
/// }
/// ```
struct TryExpr : Expr
{
    /// @brief The expression to try (must be Optional or Result type).
    ExprPtr operand;

    /// @brief Construct a try expression.
    /// @param l Source location.
    /// @param e The operand expression.
    TryExpr(SourceLoc l, ExprPtr e)
        : Expr(ExprKind::Try, l), operand(std::move(e))
    {
    }
};

/// @brief Object instantiation expression: `new Foo(args)`.
/// @details Creates a new instance of an entity type by invoking its
/// constructor. Entity types are reference types with identity.
///
/// ## Example
/// ```
/// var player = new Player("Alice", 100);
/// ```
struct NewExpr : Expr
{
    /// @brief The type to instantiate.
    TypePtr type;

    /// @brief Constructor arguments.
    std::vector<CallArg> args;

    /// @brief Construct a new expression.
    /// @param l Source location.
    /// @param t The type to create.
    /// @param a Constructor arguments.
    NewExpr(SourceLoc l, TypePtr t, std::vector<CallArg> a)
        : Expr(ExprKind::New, l), type(std::move(t)), args(std::move(a))
    {
    }
};

/// @brief Lambda parameter specification.
/// @details Represents one parameter of a lambda expression, with optional
/// type annotation. If the type is omitted, it's inferred from context.
struct LambdaParam
{
    /// @brief Parameter name.
    std::string name;

    /// @brief Parameter type (nullptr if inferred).
    TypePtr type;
};

/// @brief Captured variable in a closure.
/// @details Represents a variable captured from the enclosing scope.
struct CapturedVar
{
    /// @brief Variable name.
    std::string name;

    /// @brief Whether captured by reference (true) or value (false).
    bool byReference = false;
};

/// @brief Anonymous function expression: `(x) => x + 1`.
/// @details Creates a callable lambda that captures its environment.
/// Lambdas can have typed or untyped parameters and optional return type.
///
/// ## Examples
/// - `(x) => x + 1` - Single parameter, type inferred
/// - `(x: Integer) => x * 2` - Typed parameter
/// - `(a, b) => a + b` - Multiple parameters
/// - `() => 42` - No parameters
struct LambdaExpr : Expr
{
    /// @brief Lambda parameters.
    std::vector<LambdaParam> params;

    /// @brief Return type (nullptr if inferred).
    TypePtr returnType;

    /// @brief Lambda body expression.
    ExprPtr body;

    /// @brief Variables captured from enclosing scope (populated during sema).
    mutable std::vector<CapturedVar> captures;

    /// @brief Construct a lambda expression.
    /// @param l Source location.
    /// @param p Parameters.
    /// @param ret Return type (nullptr if inferred).
    /// @param b Body expression.
    LambdaExpr(SourceLoc l, std::vector<LambdaParam> p, TypePtr ret, ExprPtr b)
        : Expr(ExprKind::Lambda, l), params(std::move(p)), returnType(std::move(ret)),
          body(std::move(b))
    {
    }
};

/// @brief List literal expression: `[1, 2, 3]`.
/// @details Creates a new List containing the given elements.
/// Element type is inferred from the elements or context.
struct ListLiteralExpr : Expr
{
    /// @brief The list elements.
    std::vector<ExprPtr> elements;

    /// @brief Construct a list literal.
    /// @param l Source location.
    /// @param e The elements.
    ListLiteralExpr(SourceLoc l, std::vector<ExprPtr> e)
        : Expr(ExprKind::ListLiteral, l), elements(std::move(e))
    {
    }
};

/// @brief Key-value entry in a map literal.
struct MapEntry
{
    /// @brief The key expression.
    ExprPtr key;

    /// @brief The value expression.
    ExprPtr value;
};

/// @brief Map literal expression: `{"a": 1, "b": 2}`.
/// @details Creates a new Map with the given key-value pairs.
/// Key and value types are inferred from the entries or context.
struct MapLiteralExpr : Expr
{
    /// @brief The map entries.
    std::vector<MapEntry> entries;

    /// @brief Construct a map literal.
    /// @param l Source location.
    /// @param e The entries.
    MapLiteralExpr(SourceLoc l, std::vector<MapEntry> e)
        : Expr(ExprKind::MapLiteral, l), entries(std::move(e))
    {
    }
};

/// @brief Set literal expression: `{1, 2, 3}`.
/// @details Creates a new Set containing the given unique elements.
/// Distinguished from map literals by lacking key-value pairs.
struct SetLiteralExpr : Expr
{
    /// @brief The set elements.
    std::vector<ExprPtr> elements;

    /// @brief Construct a set literal.
    /// @param l Source location.
    /// @param e The elements.
    SetLiteralExpr(SourceLoc l, std::vector<ExprPtr> e)
        : Expr(ExprKind::SetLiteral, l), elements(std::move(e))
    {
    }
};

/// @brief Tuple literal expression: `(1, "hello", true)`.
/// @details Creates a tuple containing multiple values of potentially
/// different types. Tuples have fixed size and element types.
///
/// ## Examples
/// - `(1, 2)` - Pair of integers
/// - `(x, "name", true)` - Mixed types
/// - `(point.x, point.y)` - From field access
struct TupleExpr : Expr
{
    /// @brief The tuple elements.
    std::vector<ExprPtr> elements;

    /// @brief Construct a tuple literal.
    /// @param l Source location.
    /// @param e The elements.
    TupleExpr(SourceLoc l, std::vector<ExprPtr> e)
        : Expr(ExprKind::Tuple, l), elements(std::move(e))
    {
    }
};

/// @brief Tuple element access expression: `tuple.0`, `tuple.1`.
/// @details Accesses an element of a tuple by its index. The index
/// must be a compile-time constant within the tuple's bounds.
///
/// ## Examples
/// - `pair.0` - First element
/// - `pair.1` - Second element
/// - `triple.2` - Third element
struct TupleIndexExpr : Expr
{
    /// @brief The tuple being accessed.
    ExprPtr tuple;

    /// @brief The element index (0-based).
    size_t index;

    /// @brief Construct a tuple index expression.
    /// @param l Source location.
    /// @param t The tuple expression.
    /// @param i The element index.
    TupleIndexExpr(SourceLoc l, ExprPtr t, size_t i)
        : Expr(ExprKind::TupleIndex, l), tuple(std::move(t)), index(i)
    {
    }
};

// Forward declare BlockExpr (defined after statements)
struct BlockExpr;

/// @brief Conditional if-expression: `if (c) a else b`.
/// @details Unlike if-statements, if-expressions require an else branch
/// and evaluate to a value. Both branches must have compatible types.
///
/// ## Example
/// ```
/// var max = if (a > b) a else b;
/// ```
struct IfExpr : Expr
{
    /// @brief The condition to test.
    ExprPtr condition;

    /// @brief Expression evaluated if condition is true.
    ExprPtr thenBranch;

    /// @brief Expression evaluated if condition is false (required).
    ExprPtr elseBranch;

    /// @brief Construct an if-expression.
    /// @param l Source location.
    /// @param c Condition.
    /// @param t Then branch.
    /// @param e Else branch.
    IfExpr(SourceLoc l, ExprPtr c, ExprPtr t, ExprPtr e)
        : Expr(ExprKind::If, l), condition(std::move(c)), thenBranch(std::move(t)),
          elseBranch(std::move(e))
    {
    }
};

/// @brief Pattern matching arm: `Pattern => Expr`.
/// @details Represents one case in a match expression, with a pattern
/// to match against and an expression to evaluate if matched.
struct MatchArm
{
    /// @brief Pattern to match against the scrutinee.
    struct Pattern
    {
        /// @brief The kinds of patterns supported.
        enum class Kind
        {
            /// @brief Wildcard pattern: `_` matches anything.
            Wildcard,

            /// @brief Literal pattern: matches a specific value.
            Literal,

            /// @brief Binding pattern: binds a name to the matched value.
            Binding,

            /// @brief Constructor pattern: matches and destructs a type.
            Constructor,

            /// @brief Tuple pattern: matches tuple structure.
            Tuple
        };

        /// @brief The pattern kind.
        Kind kind;

        /// @brief Name for Binding patterns, type name for Constructor.
        std::string binding;

        /// @brief Nested patterns for Constructor and Tuple.
        std::vector<Pattern> subpatterns;

        /// @brief The literal value for Literal patterns.
        ExprPtr literal;

        /// @brief Optional guard condition that must be true to match.
        ExprPtr guard;
    };

    /// @brief The pattern to match.
    Pattern pattern;

    /// @brief The expression to evaluate if pattern matches.
    ExprPtr body;
};

/// @brief Pattern matching expression: `match value { ... }`.
/// @details Matches a value against multiple patterns and evaluates
/// the body of the first matching arm.
///
/// ## Example
/// ```
/// var desc = match status {
///     0 => "idle";
///     1 => "running";
///     _ => "unknown";
/// };
/// ```
struct MatchExpr : Expr
{
    /// @brief The value being matched against.
    ExprPtr scrutinee;

    /// @brief The match arms in order.
    std::vector<MatchArm> arms;

    /// @brief Construct a match expression.
    /// @param l Source location.
    /// @param s The scrutinee.
    /// @param a The match arms.
    MatchExpr(SourceLoc l, ExprPtr s, std::vector<MatchArm> a)
        : Expr(ExprKind::Match, l), scrutinee(std::move(s)), arms(std::move(a))
    {
    }
};

/// @}

//===----------------------------------------------------------------------===//
/// @name Statement Nodes
/// @brief AST nodes representing statements that perform actions.
/// @details Statements execute actions but don't produce values (unlike
/// expressions). They include control flow, declarations, and jumps.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Enumerates all kinds of statement nodes.
/// @details Used for runtime type identification when processing statements.
enum class StmtKind
{
    /// @brief Block of statements: `{ stmt1; stmt2; }`.
    /// @see BlockStmt
    Block,

    /// @brief Expression used as statement: `f();`.
    /// @see ExprStmt
    Expr,

    /// @brief Variable declaration: `var x = 1;`.
    /// @see VarStmt
    Var,

    /// @brief Conditional statement: `if (c) { ... }`.
    /// @see IfStmt
    If,

    /// @brief While loop: `while (c) { ... }`.
    /// @see WhileStmt
    While,

    /// @brief C-style for loop: `for (init; cond; update) { ... }`.
    /// @see ForStmt
    For,

    /// @brief For-in loop: `for (x in collection) { ... }`.
    /// @see ForInStmt
    ForIn,

    /// @brief Return from function: `return expr;`.
    /// @see ReturnStmt
    Return,

    /// @brief Break out of loop: `break;`.
    /// @see BreakStmt
    Break,

    /// @brief Continue to next iteration: `continue;`.
    /// @see ContinueStmt
    Continue,

    /// @brief Guard statement: `guard (c) else { return; }`.
    /// @see GuardStmt
    Guard,

    /// @brief Pattern matching statement: `match x { ... }`.
    /// @see MatchStmt
    Match,
};

/// @brief Base class for all statement nodes.
/// @details Statements perform actions and may contain nested statements
/// and expressions. Unlike expressions, statements don't produce values.
///
/// @invariant `kind` correctly identifies the concrete subclass type.
struct Stmt
{
    /// @brief Identifies the concrete statement kind for downcasting.
    StmtKind kind;

    /// @brief Source location of this statement.
    SourceLoc loc;

    /// @brief Construct a statement with kind and location.
    /// @param k The specific statement kind.
    /// @param l Source location.
    Stmt(StmtKind k, SourceLoc l) : kind(k), loc(l) {}

    /// @brief Virtual destructor for proper polymorphic cleanup.
    virtual ~Stmt() = default;
};

/// @brief Block statement: `{ stmt1; stmt2; }`.
/// @details Groups multiple statements into a single compound statement.
/// Creates a new scope for local variables.
struct BlockStmt : Stmt
{
    /// @brief The statements within this block.
    std::vector<StmtPtr> statements;

    /// @brief Construct a block statement.
    /// @param l Source location.
    /// @param s The statements.
    BlockStmt(SourceLoc l, std::vector<StmtPtr> s)
        : Stmt(StmtKind::Block, l), statements(std::move(s))
    {
    }
};

/// @brief Block expression: `{ stmts; expr }`.
/// @details A block that evaluates to a value. The last expression in
/// the block is the block's value. Creates a new scope.
///
/// ## Example
/// ```
/// var x = {
///     var temp = compute();
///     process(temp);
///     temp * 2  // This is the block's value
/// };
/// ```
struct BlockExpr : Expr
{
    /// @brief The statements executed before the final expression.
    std::vector<StmtPtr> statements;

    /// @brief The final expression whose value becomes the block's value.
    ExprPtr value;

    /// @brief Construct a block expression.
    /// @param l Source location.
    /// @param s The statements.
    /// @param v The final value expression.
    BlockExpr(SourceLoc l, std::vector<StmtPtr> s, ExprPtr v)
        : Expr(ExprKind::Block, l), statements(std::move(s)), value(std::move(v))
    {
    }
};

/// @brief Expression statement: `f();`, `x = 5;`.
/// @details Evaluates an expression for its side effects, discarding the value.
struct ExprStmt : Stmt
{
    /// @brief The expression to evaluate.
    ExprPtr expr;

    /// @brief Construct an expression statement.
    /// @param l Source location.
    /// @param e The expression.
    ExprStmt(SourceLoc l, ExprPtr e) : Stmt(StmtKind::Expr, l), expr(std::move(e)) {}
};

/// @brief Variable declaration statement: `var x = 1;` or `final x = 1;`.
/// @details Introduces a new local variable with optional type and initializer.
/// Variables declared with `final` cannot be reassigned after initialization.
///
/// ## Examples
/// - `var x = 1;` - Mutable integer (type inferred)
/// - `var x: Integer = 1;` - Mutable integer (explicit type)
/// - `final PI = 3.14159;` - Immutable constant
struct VarStmt : Stmt
{
    /// @brief The variable name.
    std::string name;

    /// @brief The declared type (nullptr = inferred from initializer).
    TypePtr type;

    /// @brief The initializer expression (nullptr = default value).
    ExprPtr initializer;

    /// @brief True if declared with `final` (immutable).
    bool isFinal;

    /// @brief Construct a variable declaration.
    /// @param l Source location.
    /// @param n Variable name.
    /// @param t Type annotation (nullptr if inferred).
    /// @param init Initializer (nullptr for default).
    /// @param final True if immutable.
    VarStmt(SourceLoc l, std::string n, TypePtr t, ExprPtr init, bool final)
        : Stmt(StmtKind::Var, l), name(std::move(n)), type(std::move(t)),
          initializer(std::move(init)), isFinal(final)
    {
    }
};

/// @brief Conditional if-statement: `if (c) { ... } else { ... }`.
/// @details Executes the then-branch if condition is true, else-branch otherwise.
/// Unlike if-expressions, the else-branch is optional.
struct IfStmt : Stmt
{
    /// @brief The condition to test.
    ExprPtr condition;

    /// @brief The then-branch (executed if true).
    StmtPtr thenBranch;

    /// @brief The else-branch (nullptr if no else).
    StmtPtr elseBranch;

    /// @brief Construct an if-statement.
    /// @param l Source location.
    /// @param c Condition.
    /// @param t Then branch.
    /// @param e Else branch (nullptr if none).
    IfStmt(SourceLoc l, ExprPtr c, StmtPtr t, StmtPtr e)
        : Stmt(StmtKind::If, l), condition(std::move(c)), thenBranch(std::move(t)),
          elseBranch(std::move(e))
    {
    }
};

/// @brief While loop statement: `while (c) { ... }`.
/// @details Repeatedly executes the body while condition is true.
struct WhileStmt : Stmt
{
    /// @brief The loop condition.
    ExprPtr condition;

    /// @brief The loop body.
    StmtPtr body;

    /// @brief Construct a while statement.
    /// @param l Source location.
    /// @param c Condition.
    /// @param b Body.
    WhileStmt(SourceLoc l, ExprPtr c, StmtPtr b)
        : Stmt(StmtKind::While, l), condition(std::move(c)), body(std::move(b))
    {
    }
};

/// @brief C-style for loop: `for (init; cond; update) { ... }`.
/// @details Traditional three-part for loop with initialization,
/// condition, and update expressions.
///
/// ## Example
/// ```
/// for (var i = 0; i < 10; i = i + 1) {
///     print(i);
/// }
/// ```
struct ForStmt : Stmt
{
    /// @brief Initialization (VarStmt or ExprStmt).
    StmtPtr init;

    /// @brief Loop condition.
    ExprPtr condition;

    /// @brief Update expression (executed after each iteration).
    ExprPtr update;

    /// @brief Loop body.
    StmtPtr body;

    /// @brief Construct a for statement.
    /// @param l Source location.
    /// @param i Initialization.
    /// @param c Condition.
    /// @param u Update.
    /// @param b Body.
    ForStmt(SourceLoc l, StmtPtr i, ExprPtr c, ExprPtr u, StmtPtr b)
        : Stmt(StmtKind::For, l), init(std::move(i)), condition(std::move(c)), update(std::move(u)),
          body(std::move(b))
    {
    }
};

/// @brief For-in loop statement: `for (x in collection) { ... }`.
/// @details Iterates over elements of a collection (List, Set, Range, etc.).
///
/// ## Examples
/// ```
/// for (item in myList) { ... }
/// for (i in 0..10) { ... }
/// for (key in myMap) { ... }
/// ```
struct ForInStmt : Stmt
{
    /// @brief The loop variable name (bound to each element).
    std::string variable;

    /// @brief The collection to iterate over.
    ExprPtr iterable;

    /// @brief The loop body.
    StmtPtr body;

    /// @brief Construct a for-in statement.
    /// @param l Source location.
    /// @param v Loop variable name.
    /// @param i The iterable expression.
    /// @param b Body.
    ForInStmt(SourceLoc l, std::string v, ExprPtr i, StmtPtr b)
        : Stmt(StmtKind::ForIn, l), variable(std::move(v)), iterable(std::move(i)),
          body(std::move(b))
    {
    }
};

/// @brief Return statement: `return expr;`.
/// @details Returns from the current function with an optional value.
/// The value type must match the function's return type.
struct ReturnStmt : Stmt
{
    /// @brief The return value (nullptr for void/unit return).
    ExprPtr value;

    /// @brief Construct a return statement.
    /// @param l Source location.
    /// @param v Return value (nullptr for void).
    ReturnStmt(SourceLoc l, ExprPtr v) : Stmt(StmtKind::Return, l), value(std::move(v)) {}
};

/// @brief Break statement: `break;`.
/// @details Exits the innermost enclosing loop.
struct BreakStmt : Stmt
{
    /// @brief Construct a break statement.
    /// @param l Source location.
    BreakStmt(SourceLoc l) : Stmt(StmtKind::Break, l) {}
};

/// @brief Continue statement: `continue;`.
/// @details Skips to the next iteration of the innermost enclosing loop.
struct ContinueStmt : Stmt
{
    /// @brief Construct a continue statement.
    /// @param l Source location.
    ContinueStmt(SourceLoc l) : Stmt(StmtKind::Continue, l) {}
};

/// @brief Guard statement: `guard (c) else { return; }`.
/// @details An early-exit pattern: if condition is false, executes the
/// else-block which must exit the scope (return, break, continue, throw).
///
/// ## Example
/// ```
/// guard (user != null) else {
///     return null;
/// }
/// // user is now known to be non-null
/// ```
struct GuardStmt : Stmt
{
    /// @brief The condition that must be true to continue.
    ExprPtr condition;

    /// @brief The else-block executed if condition is false (must exit scope).
    StmtPtr elseBlock;

    /// @brief Construct a guard statement.
    /// @param l Source location.
    /// @param c Condition.
    /// @param e Else block.
    GuardStmt(SourceLoc l, ExprPtr c, StmtPtr e)
        : Stmt(StmtKind::Guard, l), condition(std::move(c)), elseBlock(std::move(e))
    {
    }
};

/// @brief Pattern matching statement: `match x { ... }`.
/// @details Statement form of pattern matching. Unlike match expressions,
/// the arms don't need to return values.
///
/// ## Example
/// ```
/// match command {
///     "quit" => return;
///     "help" => showHelp();
///     _ => processCommand(command);
/// }
/// ```
struct MatchStmt : Stmt
{
    /// @brief The value being matched.
    ExprPtr scrutinee;

    /// @brief The match arms.
    std::vector<MatchArm> arms;

    /// @brief Construct a match statement.
    /// @param l Source location.
    /// @param s The scrutinee.
    /// @param a The arms.
    MatchStmt(SourceLoc l, ExprPtr s, std::vector<MatchArm> a)
        : Stmt(StmtKind::Match, l), scrutinee(std::move(s)), arms(std::move(a))
    {
    }
};

/// @}

//===----------------------------------------------------------------------===//
/// @name Declaration Nodes
/// @brief AST nodes representing declarations that introduce named entities.
/// @details Declarations define types, functions, fields, and modules.
/// They establish names that can be referenced from other parts of the code.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Enumerates all kinds of declaration nodes.
/// @details Used for runtime type identification when processing declarations.
enum class DeclKind
{
    /// @brief Module declaration: the compilation unit.
    /// @see ModuleDecl
    Module,

    /// @brief Import declaration: brings external modules into scope.
    /// @see ImportDecl
    Import,

    /// @brief Value type declaration: copy-semantics struct.
    /// @see ValueDecl
    Value,

    /// @brief Entity type declaration: reference-semantics class.
    /// @see EntityDecl
    Entity,

    /// @brief Interface declaration: abstract type contract.
    /// @see InterfaceDecl
    Interface,

    /// @brief Function declaration: global function.
    /// @see FunctionDecl
    Function,

    /// @brief Field declaration: member variable.
    /// @see FieldDecl
    Field,

    /// @brief Method declaration: member function.
    /// @see MethodDecl
    Method,

    /// @brief Constructor declaration: object initializer.
    /// @see ConstructorDecl
    Constructor,

    /// @brief Global variable declaration: module-level variable.
    /// @see GlobalVarDecl
    GlobalVar,
};

/// @brief Member visibility level.
/// @details Controls access to fields and methods from outside the type.
enum class Visibility
{
    /// @brief Private: only accessible within the type.
    /// @details Default for entity fields to encourage encapsulation.
    Private,

    /// @brief Public: accessible from anywhere.
    /// @details Default for value fields and exposed members.
    Public,
};

/// @brief Base class for all declaration nodes.
/// @details Declarations introduce named entities into the program.
///
/// @invariant `kind` correctly identifies the concrete subclass type.
struct Decl
{
    /// @brief Identifies the concrete declaration kind for downcasting.
    DeclKind kind;

    /// @brief Source location of this declaration.
    SourceLoc loc;

    /// @brief Construct a declaration with kind and location.
    /// @param k The specific declaration kind.
    /// @param l Source location.
    Decl(DeclKind k, SourceLoc l) : kind(k), loc(l) {}

    /// @brief Virtual destructor for proper polymorphic cleanup.
    virtual ~Decl() = default;
};

/// @brief Function parameter specification.
/// @details Represents one parameter in a function signature, with
/// name, type, and optional default value.
struct Param
{
    /// @brief Parameter name.
    std::string name;

    /// @brief Parameter type (required for function parameters).
    TypePtr type;

    /// @brief Default value expression (nullptr if required parameter).
    ExprPtr defaultValue;
};

/// @brief Global function declaration.
/// @details Defines a function at module level (not a method).
///
/// ## Example
/// ```
/// func add(a: Integer, b: Integer) -> Integer {
///     return a + b;
/// }
/// ```
struct FunctionDecl : Decl
{
    /// @brief Function name.
    std::string name;

    /// @brief Generic type parameter names (e.g., [T, U]).
    std::vector<std::string> genericParams;

    /// @brief Function parameters.
    std::vector<Param> params;

    /// @brief Return type (nullptr = void).
    TypePtr returnType;

    /// @brief Function body (nullptr for interface method signatures).
    StmtPtr body;

    /// @brief Function visibility.
    Visibility visibility = Visibility::Private;

    /// @brief True if this overrides a parent method.
    bool isOverride = false;

    /// @brief Construct a function declaration.
    /// @param l Source location.
    /// @param n Function name.
    FunctionDecl(SourceLoc l, std::string n) : Decl(DeclKind::Function, l), name(std::move(n)) {}
};

/// @brief Field declaration within a value or entity type.
/// @details Defines a member variable with type, visibility, and modifiers.
///
/// ## Modifiers
/// - `final`: Field cannot be reassigned after construction
/// - `weak`: For entity types, creates a weak reference (no ref counting)
/// - `expose`/`hide`: Controls visibility (public/private)
struct FieldDecl : Decl
{
    /// @brief Field name.
    std::string name;

    /// @brief Field type.
    TypePtr type;

    /// @brief Initial value expression (nullptr = default/required in constructor).
    ExprPtr initializer;

    /// @brief Field visibility.
    Visibility visibility = Visibility::Private;

    /// @brief True if field cannot be reassigned.
    bool isFinal = false;

    /// @brief True if this is a weak reference (entity types only).
    bool isWeak = false;

    /// @brief Construct a field declaration.
    /// @param l Source location.
    /// @param n Field name.
    FieldDecl(SourceLoc l, std::string n) : Decl(DeclKind::Field, l), name(std::move(n)) {}
};

/// @brief Method declaration within a value or entity type.
/// @details Defines a member function. Methods have access to `self`.
///
/// ## Example
/// ```
/// entity Player {
///     func heal(amount: Integer) {
///         self.health = self.health + amount;
///     }
/// }
/// ```
struct MethodDecl : Decl
{
    /// @brief Method name.
    std::string name;

    /// @brief Generic type parameter names.
    std::vector<std::string> genericParams;

    /// @brief Method parameters (does not include implicit `self`).
    std::vector<Param> params;

    /// @brief Return type (nullptr = void).
    TypePtr returnType;

    /// @brief Method body.
    StmtPtr body;

    /// @brief Method visibility.
    Visibility visibility = Visibility::Private;

    /// @brief True if this overrides a parent method.
    bool isOverride = false;

    /// @brief Construct a method declaration.
    /// @param l Source location.
    /// @param n Method name.
    MethodDecl(SourceLoc l, std::string n) : Decl(DeclKind::Method, l), name(std::move(n)) {}
};

/// @brief Constructor declaration for entity types.
/// @details Defines how to initialize a new instance of an entity type.
///
/// ## Example
/// ```
/// entity Player {
///     new(name: String, health: Integer) {
///         self.name = name;
///         self.health = health;
///     }
/// }
/// ```
struct ConstructorDecl : Decl
{
    /// @brief Constructor parameters.
    std::vector<Param> params;

    /// @brief Constructor body.
    StmtPtr body;

    /// @brief Constructor visibility.
    Visibility visibility = Visibility::Public;

    /// @brief Construct a constructor declaration.
    /// @param l Source location.
    ConstructorDecl(SourceLoc l) : Decl(DeclKind::Constructor, l) {}
};

/// @brief Module-level variable declaration.
/// @details Defines a global variable accessible throughout the module.
///
/// ## Example
/// ```
/// var globalCounter: Integer = 0;
/// final MAX_SIZE = 100;
/// ```
struct GlobalVarDecl : Decl
{
    /// @brief Variable name.
    std::string name;

    /// @brief Variable type (nullptr = inferred).
    TypePtr type;

    /// @brief Initializer expression (nullptr = default).
    ExprPtr initializer;

    /// @brief True if immutable.
    bool isFinal = false;

    /// @brief Construct a global variable declaration.
    /// @param l Source location.
    /// @param n Variable name.
    GlobalVarDecl(SourceLoc l, std::string n)
        : Decl(DeclKind::GlobalVar, l), name(std::move(n))
    {
    }
};

/// @brief Value type declaration (copy semantics).
/// @details Defines a value type with copy-on-assignment semantics.
/// Value types are passed by value and have no identity.
///
/// ## Example
/// ```
/// value Point {
///     expose x: Number;
///     expose y: Number;
///
///     func distance(other: Point) -> Number { ... }
/// }
/// ```
struct ValueDecl : Decl
{
    /// @brief Type name.
    std::string name;

    /// @brief Generic type parameter names.
    std::vector<std::string> genericParams;

    /// @brief Implemented interface names.
    std::vector<std::string> interfaces;

    /// @brief Member declarations (fields and methods).
    std::vector<DeclPtr> members;

    /// @brief Construct a value type declaration.
    /// @param l Source location.
    /// @param n Type name.
    ValueDecl(SourceLoc l, std::string n) : Decl(DeclKind::Value, l), name(std::move(n)) {}
};

/// @brief Entity type declaration (reference semantics).
/// @details Defines an entity type with reference semantics and identity.
/// Entity types are heap-allocated and passed by reference.
///
/// ## Example
/// ```
/// entity Player extends Character implements Moveable {
///     hide health: Integer;
///     expose name: String;
///
///     new(name: String) {
///         super.new();
///         self.name = name;
///         self.health = 100;
///     }
/// }
/// ```
struct EntityDecl : Decl
{
    /// @brief Type name.
    std::string name;

    /// @brief Generic type parameter names.
    std::vector<std::string> genericParams;

    /// @brief Parent entity name (empty = no inheritance).
    std::string baseClass;

    /// @brief Implemented interface names.
    std::vector<std::string> interfaces;

    /// @brief Member declarations (fields, methods, constructor).
    std::vector<DeclPtr> members;

    /// @brief Construct an entity type declaration.
    /// @param l Source location.
    /// @param n Type name.
    EntityDecl(SourceLoc l, std::string n) : Decl(DeclKind::Entity, l), name(std::move(n)) {}
};

/// @brief Interface declaration (abstract type contract).
/// @details Defines an interface that value and entity types can implement.
/// Interfaces declare method signatures without implementations.
///
/// ## Example
/// ```
/// interface Drawable {
///     func draw(canvas: Canvas);
///     func getBounds() -> Rect;
/// }
/// ```
struct InterfaceDecl : Decl
{
    /// @brief Interface name.
    std::string name;

    /// @brief Generic type parameter names.
    std::vector<std::string> genericParams;

    /// @brief Method signature declarations (body must be nullptr).
    std::vector<DeclPtr> members;

    /// @brief Construct an interface declaration.
    /// @param l Source location.
    /// @param n Interface name.
    InterfaceDecl(SourceLoc l, std::string n) : Decl(DeclKind::Interface, l), name(std::move(n)) {}
};

/// @brief Import declaration: brings external modules into scope.
/// @details Imports make types, functions, and values from other modules
/// available in the current module.
///
/// ## Examples
/// - `import Viper.IO.File;` - Import specific module
/// - `import Viper.Math as M;` - Import with alias
struct ImportDecl : Decl
{
    /// @brief The module path (e.g., "Viper.IO.File").
    std::string path;

    /// @brief Import alias (empty if no alias).
    std::string alias;

    /// @brief Construct an import declaration.
    /// @param l Source location.
    /// @param p The module path.
    ImportDecl(SourceLoc l, std::string p) : Decl(DeclKind::Import, l), path(std::move(p)) {}
};

/// @brief Module declaration: the top-level compilation unit.
/// @details Represents an entire source file as a module with a name,
/// imports, and top-level declarations.
///
/// ## Example
/// ```
/// module MyGame;
///
/// import Viper.Terminal;
///
/// entity Player { ... }
///
/// func main() { ... }
/// ```
struct ModuleDecl : Decl
{
    /// @brief Module name (from `module MyName;` declaration).
    std::string name;

    /// @brief Import declarations.
    std::vector<ImportDecl> imports;

    /// @brief Top-level declarations (types, functions, global vars).
    std::vector<DeclPtr> declarations;

    /// @brief Construct a module declaration.
    /// @param l Source location.
    /// @param n Module name.
    ModuleDecl(SourceLoc l, std::string n) : Decl(DeclKind::Module, l), name(std::move(n)) {}
};

/// @}

} // namespace il::frontends::viperlang
