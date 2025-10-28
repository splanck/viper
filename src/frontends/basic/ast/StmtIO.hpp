// File: src/frontends/basic/ast/StmtIO.hpp
// Purpose: Defines BASIC input/output statement nodes.
// Key invariants: Print and I/O statements own their child expressions and track channel metadata.
// Ownership/Lifetime: Statements own expressions and child statements via std::unique_ptr containers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtBase.hpp"

#include <string>
#include <vector>

namespace il::frontends::basic
{

struct PrintItem
{
    enum class Kind
    {
        Expr,
        Comma,
        Semicolon,
    } kind{Kind::Expr};

    ExprPtr expr;
};

struct PrintStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Print;
    }

    std::vector<PrintItem> items;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct PrintChStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::PrintCh;
    }

    enum class Mode
    {
        Print,
        Write,
    } mode{Mode::Print};

    ExprPtr channelExpr;
    std::vector<ExprPtr> args;
    bool trailingNewline{true};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct OpenStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Open;
    }

    ExprPtr pathExpr;
    enum class Mode
    {
        Input,
        Output,
        Append,
        Binary,
        Random,
    } mode{Mode::Input};

    ExprPtr channelExpr;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct CloseStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Close;
    }

    ExprPtr channelExpr;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct SeekStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Seek;
    }

    ExprPtr channelExpr;
    ExprPtr positionExpr;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct NameRef
{
    Identifier name;
    il::support::SourceLoc loc{};
};

struct InputStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Input;
    }

    ExprPtr prompt;
    std::vector<std::string> vars;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct InputChStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::InputCh;
    }

    int channel{0};
    NameRef target;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct LineInputChStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::LineInputCh;
    }

    ExprPtr channelExpr;
    LValuePtr targetVar;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

} // namespace il::frontends::basic
