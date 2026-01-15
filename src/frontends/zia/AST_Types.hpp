//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file AST_Types.hpp
/// @brief Type annotation nodes for Zia AST.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/AST_Fwd.hpp"
#include <string>
#include <vector>

namespace il::frontends::zia
{
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
/// type or null. Optional types are fundamental for null-safety in Zia.
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

} // namespace il::frontends::zia
