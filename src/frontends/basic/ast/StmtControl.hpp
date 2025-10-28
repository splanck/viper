// File: src/frontends/basic/ast/StmtControl.hpp
// Purpose: Defines BASIC control-flow statements including branches and loops.
// Key invariants: Control statements own their child statements and preserve evaluation order.
// Ownership/Lifetime: Statements own nested blocks via std::unique_ptr containers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtBase.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

struct IfStmt : Stmt
{
    struct ElseIf
    {
        ExprPtr cond;
        StmtPtr then_branch;
    };

    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::If;
    }

    ExprPtr cond;
    StmtPtr then_branch;
    std::vector<ElseIf> elseifs;
    StmtPtr else_branch;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct CaseArm
{
    struct CaseRel
    {
        enum Op
        {
            LT,
            LE,
            EQ,
            GE,
            GT
        };

        Op op{EQ};
        std::int64_t rhs{0};
    };

    std::vector<std::int64_t> labels;
    std::vector<std::string> str_labels;
    std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
    std::vector<CaseRel> rels;
    std::vector<StmtPtr> body;
    il::support::SourceRange range{};
    uint32_t caseKeywordLength = 0;
};

struct SelectCaseStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::SelectCase;
    }

    ExprPtr selector;
    std::vector<CaseArm> arms;
    std::vector<StmtPtr> elseBody;
    il::support::SourceRange range{};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct WhileStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::While;
    }

    ExprPtr cond;
    std::vector<StmtPtr> body;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct DoStmt : Stmt
{
    enum class CondKind
    {
        None,
        While,
        Until,
    } condKind{CondKind::None};

    enum class TestPos
    {
        Pre,
        Post,
    } testPos{TestPos::Pre};

    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Do;
    }

    ExprPtr cond;
    std::vector<StmtPtr> body;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct ForStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::For;
    }

    std::string var;
    ExprPtr start;
    ExprPtr end;
    ExprPtr step;
    std::vector<StmtPtr> body;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct NextStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Next;
    }

    std::string var;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct ExitStmt : Stmt
{
    enum class LoopKind
    {
        For,
        While,
        Do,
    } kind{LoopKind::While};

    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Exit;
    }

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct GotoStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Goto;
    }

    int target{0};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct GosubStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Gosub;
    }

    int targetLine{0};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct OnErrorGoto : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::OnErrorGoto;
    }

    int target{0};
    bool toZero{false};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct Resume : Stmt
{
    enum class Mode
    {
        Same,
        Next,
        Label,
    } mode{Mode::Same};

    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Resume;
    }

    int target{0};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

struct EndStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::End;
    }

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

} // namespace il::frontends::basic
