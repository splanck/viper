// File: src/frontends/basic/ast/StmtBase.hpp
// Purpose: Declares the shared BASIC statement base classes and visitor interfaces.
// Key invariants: Statement kinds stay in sync with concrete node definitions.
// Ownership/Lifetime: Statements are owned via std::unique_ptr managed by callers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"
#include "support/source_location.hpp"

namespace il::frontends::basic
{

/// @brief Visitor interface for BASIC statements.
struct StmtVisitor
{
    virtual ~StmtVisitor() = default;
    virtual void visit(const LabelStmt &) = 0;
    virtual void visit(const PrintStmt &) = 0;
    virtual void visit(const PrintChStmt &) = 0;
    virtual void visit(const CallStmt &) = 0;
    virtual void visit(const ClsStmt &) = 0;
    virtual void visit(const ColorStmt &) = 0;
    virtual void visit(const LocateStmt &) = 0;
    virtual void visit(const LetStmt &) = 0;
    virtual void visit(const DimStmt &) = 0;
    virtual void visit(const ReDimStmt &) = 0;
    virtual void visit(const RandomizeStmt &) = 0;
    virtual void visit(const IfStmt &) = 0;
    virtual void visit(const SelectCaseStmt &) = 0;
    virtual void visit(const WhileStmt &) = 0;
    virtual void visit(const DoStmt &) = 0;
    virtual void visit(const ForStmt &) = 0;
    virtual void visit(const NextStmt &) = 0;
    virtual void visit(const ExitStmt &) = 0;
    virtual void visit(const GotoStmt &) = 0;
    virtual void visit(const GosubStmt &) = 0;
    virtual void visit(const OpenStmt &) = 0;
    virtual void visit(const CloseStmt &) = 0;
    virtual void visit(const SeekStmt &) = 0;
    virtual void visit(const OnErrorGoto &) = 0;
    virtual void visit(const Resume &) = 0;
    virtual void visit(const EndStmt &) = 0;
    virtual void visit(const InputStmt &) = 0;
    virtual void visit(const InputChStmt &) = 0;
    virtual void visit(const LineInputChStmt &) = 0;
    virtual void visit(const ReturnStmt &) = 0;
    virtual void visit(const FunctionDecl &) = 0;
    virtual void visit(const SubDecl &) = 0;
    virtual void visit(const StmtList &) = 0;
    virtual void visit(const DeleteStmt &) = 0;
    virtual void visit(const ConstructorDecl &) = 0;
    virtual void visit(const DestructorDecl &) = 0;
    virtual void visit(const MethodDecl &) = 0;
    virtual void visit(const ClassDecl &) = 0;
    virtual void visit(const TypeDecl &) = 0;
};

/// @brief Visitor interface for mutable BASIC statements.
struct MutStmtVisitor
{
    virtual ~MutStmtVisitor() = default;
    virtual void visit(LabelStmt &) = 0;
    virtual void visit(PrintStmt &) = 0;
    virtual void visit(PrintChStmt &) = 0;
    virtual void visit(CallStmt &) = 0;
    virtual void visit(ClsStmt &) = 0;
    virtual void visit(ColorStmt &) = 0;
    virtual void visit(LocateStmt &) = 0;
    virtual void visit(LetStmt &) = 0;
    virtual void visit(DimStmt &) = 0;
    virtual void visit(ReDimStmt &) = 0;
    virtual void visit(RandomizeStmt &) = 0;
    virtual void visit(IfStmt &) = 0;
    virtual void visit(SelectCaseStmt &) = 0;
    virtual void visit(WhileStmt &) = 0;
    virtual void visit(DoStmt &) = 0;
    virtual void visit(ForStmt &) = 0;
    virtual void visit(NextStmt &) = 0;
    virtual void visit(ExitStmt &) = 0;
    virtual void visit(GotoStmt &) = 0;
    virtual void visit(GosubStmt &) = 0;
    virtual void visit(OpenStmt &) = 0;
    virtual void visit(CloseStmt &) = 0;
    virtual void visit(SeekStmt &) = 0;
    virtual void visit(OnErrorGoto &) = 0;
    virtual void visit(Resume &) = 0;
    virtual void visit(EndStmt &) = 0;
    virtual void visit(InputStmt &) = 0;
    virtual void visit(InputChStmt &) = 0;
    virtual void visit(LineInputChStmt &) = 0;
    virtual void visit(ReturnStmt &) = 0;
    virtual void visit(FunctionDecl &) = 0;
    virtual void visit(SubDecl &) = 0;
    virtual void visit(StmtList &) = 0;
    virtual void visit(DeleteStmt &) = 0;
    virtual void visit(ConstructorDecl &) = 0;
    virtual void visit(DestructorDecl &) = 0;
    virtual void visit(MethodDecl &) = 0;
    virtual void visit(ClassDecl &) = 0;
    virtual void visit(TypeDecl &) = 0;
};

/// @brief Base class for all BASIC statements.
struct Stmt
{
    /// @brief Discriminator identifying the concrete statement subclass.
    enum class Kind
    {
        Label,
        Print,
        PrintCh,
        Call,
        Cls,
        Color,
        Locate,
        Let,
        Dim,
        ReDim,
        Randomize,
        If,
        SelectCase,
        While,
        Do,
        For,
        Next,
        Exit,
        Goto,
        Gosub,
        Open,
        Close,
        Seek,
        OnErrorGoto,
        Resume,
        End,
        Input,
        InputCh,
        LineInputCh,
        Return,
        FunctionDecl,
        SubDecl,
        StmtList,
        Delete,
        ConstructorDecl,
        DestructorDecl,
        MethodDecl,
        ClassDecl,
        TypeDecl,
    };

    /// BASIC line number associated with this statement.
    int line{0};

    /// Source location of the first token in the statement.
    il::support::SourceLoc loc{};

    virtual ~Stmt() = default;
    /// @brief Retrieve the discriminator for this statement.
    [[nodiscard]] virtual Kind stmtKind() const = 0;
    /// @brief Accept a visitor to process this statement.
    virtual void accept(StmtVisitor &visitor) const = 0;
    /// @brief Accept a mutable visitor to process this statement.
    virtual void accept(MutStmtVisitor &visitor) = 0;
};

} // namespace il::frontends::basic

