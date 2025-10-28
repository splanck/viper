// File: src/frontends/basic/ast/StmtExpr.hpp
// Purpose: Defines BASIC statements manipulating expressions, variables, and runtime state.
// Key invariants: Expression pointers are owned and non-null where required by semantics.
// Ownership/Lifetime: Follows AST ownership using std::unique_ptr managed by callers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtBase.hpp"

#include <string>
#include <vector>

namespace il::frontends::basic
{

/// @brief CALL statement invoking a user-defined SUB.
struct CallStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Call;
    }

    /// Call expression representing the invoked SUB.
    std::unique_ptr<CallExpr> call;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief LET statement assigning to an lvalue.
struct LetStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Let;
    }

    /// Left-hand side receiving the assignment; owned and non-null.
    LValuePtr target;

    /// Right-hand side expression; owned and non-null.
    ExprPtr expr;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief DIM statement declaring a variable or array.
struct DimStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Dim;
    }

    /// Array name being declared.
    std::string name;

    /// Number of elements to allocate when @ref isArray is true; may be null for scalars.
    ExprPtr size;

    /// Declared BASIC type for this DIM.
    Type type{Type::I64};

    /// True when DIM declares an array; false for scalar declarations.
    bool isArray{true};
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief REDIM statement resizing an existing array.
struct ReDimStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::ReDim;
    }

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
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Randomize;
    }

    /// Numeric seed expression, truncated to i64; owned and non-null.
    ExprPtr seed;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief RETURN statement optionally yielding a value.
struct ReturnStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Return;
    }

    /// Expression whose value is returned; null when no expression is provided.
    ExprPtr value;

    /// True when this RETURN exits a GOSUB (top-level RETURN without a value).
    bool isGosubReturn{false};
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief DELETE statement releasing an object reference.
struct DeleteStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Delete;
    }

    /// Expression evaluating to the instance to delete.
    ExprPtr target;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

} // namespace il::frontends::basic
