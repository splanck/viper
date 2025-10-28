// File: src/frontends/basic/ast/StmtExpr.hpp
// Purpose: Defines BASIC expression-oriented statement nodes (assignment, calls, runtime).
// Key invariants: Nodes own expression operands driving evaluation side-effects.
// Ownership/Lifetime: Operands managed through std::unique_ptr.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtBase.hpp"

#include <string>

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

/// @brief CLS statement clearing the screen.
struct ClsStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Cls;
    }

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief COLOR statement changing the palette.
struct ColorStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Color;
    }

    /// Foreground color expression; may be null when omitted.
    ExprPtr fg;

    /// Background color expression; may be null when omitted.
    ExprPtr bg;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief LOCATE statement moving the cursor.
struct LocateStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Locate;
    }

    /// Row expression (1-based); owned and non-null.
    ExprPtr row;

    /// Column expression (1-based); owned and non-null.
    ExprPtr col;

    /// Optional cursor visibility expression.
    ExprPtr cursor;

    /// Optional start scan line expression.
    ExprPtr start;

    /// Optional stop scan line expression.
    ExprPtr stop;

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

