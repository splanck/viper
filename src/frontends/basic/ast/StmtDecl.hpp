//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ast/StmtDecl.hpp
// Purpose: Defines BASIC statement nodes representing declarations and statement lists. 
// Key invariants: Declaration bodies own their child statements and record source
// Ownership/Lifetime: Nodes own nested statements via StmtPtr containers and store
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/BasicTypes.hpp"
#include "frontends/basic/ast/StmtBase.hpp"

#include <optional>
#include <string>
#include <vector>

namespace il::frontends::basic
{

/// @brief Parameter in FUNCTION or SUB declaration.
struct Param
{
    /// Parameter name including optional suffix.
    Identifier name;

    /// Resolved type from suffix.
    Type type{Type::I64};

    /// True if parameter declared with ().
    bool is_array{false};

    /// Source location of the parameter name.
    il::support::SourceLoc loc{};

    /// BUG-060 fix: Class name for object-typed parameters; empty for primitives.
    std::string objectClass;
};

/// @brief FUNCTION declaration with optional parameters and return type.
struct FunctionDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::FunctionDecl;
    }

    /// Function name including suffix.
    Identifier name;

    /// Qualified namespace path segments for this procedure.
    /// Example: for A.B.C.Foo, path = {"A","B","C"}.
    std::vector<std::string> namespacePath;

    /// Canonical, fully-qualified name for this procedure (dot-joined).
    /// Example: "a.b.c.foo" (lowercased for case-insensitive language).
    std::string qualifiedName;

    /// Return type derived from name suffix.
    Type ret{Type::I64};

    /// Optional explicit return type from "AS <TYPE>".
    /// For SUB, keep at BasicType::Void.
    /// For FUNCTION without AS, keep at BasicType::Unknown.
    BasicType explicitRetType{BasicType::Unknown};

    /// Optional explicit class return type from "AS <Class>".
    /// Stored as a qualified, canonical lowercase name when present.
    std::vector<std::string> explicitClassRetQname;

    /// Ordered parameter list.
    std::vector<Param> params;

    /// Function body statements.
    std::vector<StmtPtr> body;

    /// Location of trailing END FUNCTION keyword.
    il::support::SourceLoc endLoc{};
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief SUB declaration representing a void procedure.
struct SubDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::SubDecl;
    }

    /// Subroutine name including suffix.
    Identifier name;

    /// Qualified namespace path segments for this procedure.
    /// Example: for A.B.C.Bar, path = {"A","B","C"}.
    std::vector<std::string> namespacePath;

    /// Canonical, fully-qualified name for this procedure (dot-joined).
    /// Example: "a.b.c.bar" (lowercased for case-insensitive language).
    std::string qualifiedName;

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
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::StmtList;
    }

    /// Ordered statements sharing the same line.
    std::vector<StmtPtr> stmts;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief Constructor declaration for a CLASS.
struct ConstructorDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::ConstructorDecl;
    }

    /// Access specifier (PUBLIC/PRIVATE); defaults to PUBLIC.
    Access access{Access::Public};

    /// Static flag: true for a static constructor (type initializer).
    /// Ignored for instance constructors; used later for static ctor (D4).
    bool isStatic{false};

    /// Ordered parameters for the constructor.
    std::vector<Param> params;

    /// Statements forming the constructor body.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief Destructor declaration for a CLASS.
struct DestructorDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::DestructorDecl;
    }

    /// Access specifier (PUBLIC/PRIVATE); defaults to PUBLIC.
    Access access{Access::Public};

    /// Statements forming the destructor body.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief Method declaration inside a CLASS.
struct MethodDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::MethodDecl;
    }

    /// Method name.
    std::string name;

    /// Access specifier (PUBLIC/PRIVATE); defaults to PUBLIC.
    Access access{Access::Public};

    /// True if declared STATIC; static methods do not receive an implicit ME receiver.
    bool isStatic{false};

    /// Ordered parameters for the method.
    std::vector<Param> params;

    /// Optional return type when method yields a value.
    std::optional<Type> ret;

    /// Optional explicit class return type from "AS <Class>".
    /// Stored as a qualified, canonical lowercase name when present.
    /// Empty vector indicates primitive or no explicit return type.
    std::vector<std::string> explicitClassRetQname;

    /// OOP modifiers
    /// @note Constructors cannot be virtual/override/abstract/final; only methods may carry these.
    bool isVirtual{false};
    bool isOverride{false};
    bool isAbstract{false};
    bool isFinal{false};

    /// Statements forming the method body.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief CLASS declaration grouping fields and members.
struct ClassDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::ClassDecl;
    }

    /// Class name.
    std::string name;

    /// Qualified namespace path segments for this type.
    /// Example: for A.B.C.Point, path = {"A","B","C"}.
    std::vector<std::string> namespacePath;

    /// Canonical, fully-qualified name for this type (dot-joined).
    /// Example: "a.b.c.point" (lowercased for case-insensitive language).
    std::string qualifiedName;

    /// Optional base class name (bare or qualified). Resolution happens in semantic analysis.
    std::optional<std::string> baseName;

    /// Class-level modifiers.
    bool isAbstract{false};
    bool isFinal{false};

    /// Field definition within the class.
    struct Field
    {
        std::string name;
        Type type{Type::I64};
        /// Access specifier (PUBLIC/PRIVATE); defaults to PUBLIC.
        Access access{Access::Public};
        /// True if field is declared STATIC.
        bool isStatic{false};
        /// Whether this field is an array (BUG-056 fix).
        bool isArray{false};
        /// Array dimension extents if isArray is true (BUG-056 fix).
        std::vector<long long> arrayExtents;
        /// BUG-082 fix: Class name for object-typed fields; empty for primitives.
        std::string objectClassName;
    };

    /// Ordered fields declared on the class.
    std::vector<Field> fields;

    /// Members declared within the class (constructors, destructors, methods).
    std::vector<StmtPtr> members;

    /// Interfaces implemented by this class, each as dotted qualified segments.
    /// Example: implements A.B.I -> { {"A","B","I"} }
    std::vector<std::vector<std::string>> implementsQualifiedNames;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief PROPERTY declaration inside a CLASS, with optional GET/SET bodies.
struct PropertyDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::PropertyDecl;
    }

    /// Filled by the lowerer: fully-qualified class path segments.
    std::vector<std::string> qualifiedClass;

    /// Property simple name.
    std::string name;

    /// Declared type of the property.
    Type type{Type::I64};

    /// True if declared STATIC; static properties have no receiver.
    bool isStatic{false};

    /// Overall visibility of the property head.
    Access access{Access::Public};

    struct Getter
    {
        Access access{Access::Public};
        std::vector<StmtPtr> body;
        bool present{false};
    } get;

    struct Setter
    {
        Access access{Access::Public};
        std::string paramName{"value"};
        std::vector<StmtPtr> body;
        bool present{false};
    } set;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief TYPE declaration defining a structured record type.
struct TypeDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::TypeDecl;
    }

    /// Type name.
    std::string name;

    /// Field definition within the type.
    struct Field
    {
        std::string name;
        Type type{Type::I64};
        /// Access specifier (PUBLIC/PRIVATE); defaults to PUBLIC.
        Access access{Access::Public};
    };

    /// Ordered fields declared on the type.
    std::vector<Field> fields;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief NAMESPACE declaration grouping declarations under a qualified path.
struct NamespaceDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::NamespaceDecl;
    }

    /// Qualified namespace path segments in declaration order.
    std::vector<std::string> path;

    /// Declarations/body within the namespace.
    std::vector<StmtPtr> body;

    /// Acceptors inline to avoid extra TU edits.
    void accept(StmtVisitor &visitor) const override
    {
        visitor.visit(*this);
    }

    void accept(MutStmtVisitor &visitor) override
    {
        visitor.visit(*this);
    }
};

/// @brief INTERFACE declaration grouping abstract member signatures.
struct InterfaceDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::InterfaceDecl;
    }

    /// Qualified interface name segments, e.g. ["A","B","I"].
    std::vector<std::string> qualifiedName;

    /// Abstract members (method signatures only) declared inside the interface.
    std::vector<StmtPtr> members;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief USING directive importing a namespace at file scope.
struct UsingDecl : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::UsingDecl;
    }

    /// Namespace path segments, e.g. ["Foo","Bar","Baz"] for USING Foo.Bar.Baz.
    std::vector<std::string> namespacePath;

    /// Optional alias for the imported namespace; empty if no AS clause present.
    std::string alias;

    void accept(StmtVisitor &visitor) const override
    {
        visitor.visit(*this);
    }

    void accept(MutStmtVisitor &visitor) override
    {
        visitor.visit(*this);
    }
};

} // namespace il::frontends::basic
