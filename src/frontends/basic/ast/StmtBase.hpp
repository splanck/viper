//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ast/StmtBase.hpp
// Purpose: Declares the common BASIC statement base class and visitor interfaces.
// Key invariants: Visitors enumerate every concrete statement kind and statements
// Ownership/Lifetime: Statements are owned via std::unique_ptr managed by callers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"

#include "support/source_location.hpp"

namespace il::frontends::basic
{
// Forward declare to allow visitor signatures without including full definition.
struct NamespaceDecl;
struct UsingDecl;

/// @brief Visitor interface for BASIC statements.
struct StmtVisitor
{
    virtual ~StmtVisitor() = default;
    virtual void visit(const LabelStmt &) = 0;
    virtual void visit(const PrintStmt &) = 0;
    virtual void visit(const PrintChStmt &) = 0;
    virtual void visit(const BeepStmt &) = 0;
    virtual void visit(const CallStmt &) = 0;
    virtual void visit(const ClsStmt &) = 0;
    virtual void visit(const ColorStmt &) = 0;
    virtual void visit(const SleepStmt &) = 0;
    virtual void visit(const LocateStmt &) = 0;
    virtual void visit(const CursorStmt &) = 0;
    virtual void visit(const AltScreenStmt &) = 0;
    virtual void visit(const LetStmt &) = 0;
    virtual void visit(const ConstStmt &) = 0;
    virtual void visit(const DimStmt &) = 0;
    virtual void visit(const StaticStmt &) = 0;
    virtual void visit(const SharedStmt &) = 0;
    virtual void visit(const ReDimStmt &) = 0;
    virtual void visit(const SwapStmt &) = 0;
    virtual void visit(const RandomizeStmt &) = 0;
    virtual void visit(const IfStmt &) = 0;
    virtual void visit(const SelectCaseStmt &) = 0;

    virtual void visit(const TryCatchStmt &) {}

    virtual void visit(const WhileStmt &) = 0;
    virtual void visit(const DoStmt &) = 0;
    virtual void visit(const ForStmt &) = 0;
    virtual void visit(const ForEachStmt &) = 0;
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

    virtual void visit(const PropertyDecl &) {}

    virtual void visit(const ClassDecl &) = 0;
    virtual void visit(const TypeDecl &) = 0;
    virtual void visit(const InterfaceDecl &) = 0;

    virtual void visit(const NamespaceDecl &) {}

    virtual void visit(const UsingDecl &) = 0;
};

/// @brief Visitor interface for mutable BASIC statements.
struct MutStmtVisitor
{
    virtual ~MutStmtVisitor() = default;
    virtual void visit(LabelStmt &) = 0;
    virtual void visit(PrintStmt &) = 0;
    virtual void visit(PrintChStmt &) = 0;
    virtual void visit(BeepStmt &) = 0;
    virtual void visit(CallStmt &) = 0;
    virtual void visit(ClsStmt &) = 0;
    virtual void visit(ColorStmt &) = 0;
    virtual void visit(SleepStmt &) = 0;
    virtual void visit(LocateStmt &) = 0;
    virtual void visit(CursorStmt &) = 0;
    virtual void visit(AltScreenStmt &) = 0;
    virtual void visit(LetStmt &) = 0;
    virtual void visit(ConstStmt &) = 0;
    virtual void visit(DimStmt &) = 0;
    virtual void visit(StaticStmt &) = 0;
    virtual void visit(SharedStmt &) = 0;
    virtual void visit(ReDimStmt &) = 0;
    virtual void visit(SwapStmt &) = 0;
    virtual void visit(RandomizeStmt &) = 0;
    virtual void visit(IfStmt &) = 0;
    virtual void visit(SelectCaseStmt &) = 0;

    virtual void visit(TryCatchStmt &) {}

    virtual void visit(WhileStmt &) = 0;
    virtual void visit(DoStmt &) = 0;
    virtual void visit(ForStmt &) = 0;
    virtual void visit(ForEachStmt &) = 0;
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

    virtual void visit(PropertyDecl &) {}

    virtual void visit(ClassDecl &) = 0;
    virtual void visit(TypeDecl &) = 0;
    virtual void visit(InterfaceDecl &) = 0;

    virtual void visit(NamespaceDecl &) {}

    virtual void visit(UsingDecl &) = 0;
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
        Beep,
        Call,
        Cls,
        Color,
        Sleep,
        Locate,
        Cursor,
        AltScreen,
        Let,
        Const,
        Dim,
        Static,
        Shared,
        ReDim,
        Swap,
        Randomize,
        If,
        SelectCase,
        TryCatch,
        While,
        Do,
        For,
        ForEach,
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
        PropertyDecl,
        ClassDecl,
        TypeDecl,
        InterfaceDecl,
        NamespaceDecl,
        UsingDecl,
    };

    /// BASIC line number associated with this statement.
    int line{0};

    /// Source location of the first token in the statement.
    il::support::SourceLoc loc{};

    virtual ~Stmt() = default;
    /// @brief Retrieve the discriminator for this statement.
    [[nodiscard]] virtual Kind stmtKind() const noexcept = 0;
    /// @brief Accept a visitor to process this statement.
    virtual void accept(StmtVisitor &visitor) const = 0;
    /// @brief Accept a mutable visitor to process this statement.
    virtual void accept(MutStmtVisitor &visitor) = 0;
};

} // namespace il::frontends::basic
