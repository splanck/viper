// File: src/frontends/basic/ast/DeclNodes.hpp
// Purpose: Defines BASIC declaration AST nodes for procedures, classes, and programs.
// Key invariants: Declaration nodes own child statements and parameters for lifetime safety.
// Ownership/Lifetime: Declarations are owned via StmtPtr/ProcDecl handles managed by callers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtNodes.hpp"

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
    Type type = Type::I64;

    /// True if parameter declared with ().
    bool is_array = false;

    /// Source location of the parameter name.
    il::support::SourceLoc loc;
};

/// @brief FUNCTION declaration with optional parameters and return type.
struct FunctionDecl : Stmt
{
    [[nodiscard]] Kind stmtKind() const override { return Kind::FunctionDecl; }
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
    [[nodiscard]] Kind stmtKind() const override { return Kind::SubDecl; }
    /// Subroutine name including suffix.
    Identifier name;

    /// Ordered parameter list.
    std::vector<Param> params;

    /// Body statements.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief Constructor declaration for a CLASS.
struct ConstructorDecl : Stmt
{
    [[nodiscard]] Kind stmtKind() const override { return Kind::ConstructorDecl; }
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
    [[nodiscard]] Kind stmtKind() const override { return Kind::DestructorDecl; }
    /// Statements forming the destructor body.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief Method declaration inside a CLASS.
struct MethodDecl : Stmt
{
    [[nodiscard]] Kind stmtKind() const override { return Kind::MethodDecl; }
    /// Method name.
    std::string name;

    /// Ordered parameters for the method.
    std::vector<Param> params;

    /// Optional return type when method yields a value.
    std::optional<Type> ret;

    /// Statements forming the method body.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief CLASS declaration grouping fields and members.
struct ClassDecl : Stmt
{
    [[nodiscard]] Kind stmtKind() const override { return Kind::ClassDecl; }
    /// Class name.
    std::string name;

    /// Field definition within the class.
    struct Field
    {
        std::string name;
        Type type;
    };

    /// Ordered fields declared on the class.
    std::vector<Field> fields;

    /// Members declared within the class (constructors, destructors, methods).
    std::vector<StmtPtr> members;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief TYPE declaration defining a structured record type.
struct TypeDecl : Stmt
{
    [[nodiscard]] Kind stmtKind() const override { return Kind::TypeDecl; }
    /// Type name.
    std::string name;

    /// Field definition within the type.
    struct Field
    {
        std::string name;
        Type type;
    };

    /// Ordered fields declared on the type.
    std::vector<Field> fields;
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

