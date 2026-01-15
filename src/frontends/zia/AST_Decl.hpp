//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file AST_Decl.hpp
/// @brief Declaration nodes for Zia AST.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/AST_Stmt.hpp"
#include <string>
#include <vector>

namespace il::frontends::zia
{
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

    /// @brief Namespace declaration: groups declarations under a qualified name.
    /// @see NamespaceDecl
    Namespace,
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
    GlobalVarDecl(SourceLoc l, std::string n) : Decl(DeclKind::GlobalVar, l), name(std::move(n)) {}
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

/// @brief Namespace declaration: groups declarations under a qualified name.
/// @details Namespaces provide hierarchical organization and prevent name collisions.
/// Declarations inside a namespace are accessed via qualified names (e.g., `MyLib.Foo`).
/// Namespaces can be nested and can span multiple files via imports.
///
/// The built-in `Viper.*` namespaces (Viper.Terminal, Viper.Math, etc.) use the
/// same mechanism as user-defined namespaces - there is no special casing.
///
/// ## Example
/// ```
/// namespace MyLib {
///     entity Parser { ... }
///     func parse(s: String) -> Result { ... }
/// }
///
/// // Nested namespaces
/// namespace MyLib.Internal {
///     func helper() { ... }
/// }
/// ```
///
/// ## Access
/// ```
/// var p = new MyLib.Parser();
/// var r = MyLib.parse("input");
/// ```
struct NamespaceDecl : Decl
{
    /// @brief Namespace name (can be dotted, e.g., "MyLib.Internal").
    std::string name;

    /// @brief Declarations within this namespace.
    std::vector<DeclPtr> declarations;

    /// @brief Construct a namespace declaration.
    /// @param l Source location.
    /// @param n Namespace name.
    NamespaceDecl(SourceLoc l, std::string n) : Decl(DeclKind::Namespace, l), name(std::move(n)) {}
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

} // namespace il::frontends::zia
