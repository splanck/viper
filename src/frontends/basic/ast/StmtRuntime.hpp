// File: src/frontends/basic/ast/StmtRuntime.hpp
// Purpose: Defines BASIC runtime and expression-oriented statements.
// Key invariants: Child expression pointers are owned and remain non-null when documented.
// Ownership/Lifetime: Statements own child expressions via std::unique_ptr.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtBase.hpp"

#include <memory>
#include <string>

namespace il::frontends::basic
{

struct CallStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Call;
    }

    std::unique_ptr<CallExpr> call;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct ClsStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Cls;
    }

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct ColorStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Color;
    }

    ExprPtr fg;
    ExprPtr bg;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct LocateStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Locate;
    }

    ExprPtr row;
    ExprPtr col;
    ExprPtr cursor;
    ExprPtr start;
    ExprPtr stop;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct LetStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Let;
    }

    LValuePtr target;
    ExprPtr expr;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct DimStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Dim;
    }

    std::string name;
    ExprPtr size;
    Type type{Type::I64};
    bool isArray{true};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct ReDimStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::ReDim;
    }

    std::string name;
    ExprPtr size;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct RandomizeStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Randomize;
    }

    ExprPtr seed;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct ReturnStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Return;
    }

    ExprPtr value;
    bool isGosubReturn{false};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct DeleteStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Delete;
    }

    ExprPtr target;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

} // namespace il::frontends::basic
