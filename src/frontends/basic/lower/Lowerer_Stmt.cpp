//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Lowerer_Stmt.cpp
// Purpose: Provides the BASIC statement visitor wiring that forwards AST nodes
//          into the shared Lowerer helpers.
// Key invariants: Statement visitors honour the active Lowerer context and never
//                 mutate AST ownership.
// Ownership/Lifetime: Operates on a borrowed Lowerer instance while the caller
//                     retains AST ownership.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/AstVisitor.hpp"

#include "il/core/BasicBlock.hpp"

namespace il::frontends::basic
{

/// @brief Visitor that lowers BASIC statements through the shared Lowerer.
class LowererStmtVisitor final : public lower::AstVisitor, public StmtVisitor
{
  public:
    explicit LowererStmtVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void visitExpr(const Expr &) override {}

    void visitStmt(const Stmt &stmt) override
    {
        stmt.accept(*this);
    }

    void visit(const LabelStmt &) override {}
    void visit(const PrintStmt &stmt) override { lowerer_.lowerPrint(stmt); }
    void visit(const PrintChStmt &stmt) override { lowerer_.lowerPrintCh(stmt); }
    void visit(const CallStmt &stmt) override { lowerer_.lowerCallStmt(stmt); }
    void visit(const ClsStmt &stmt) override { lowerer_.visit(stmt); }
    void visit(const ColorStmt &stmt) override { lowerer_.visit(stmt); }
    void visit(const LocateStmt &stmt) override { lowerer_.visit(stmt); }
    void visit(const LetStmt &stmt) override { lowerer_.lowerLet(stmt); }
    void visit(const DimStmt &stmt) override
    {
        if (stmt.isArray)
            lowerer_.lowerDim(stmt);
    }
    void visit(const ReDimStmt &stmt) override { lowerer_.lowerReDim(stmt); }
    void visit(const RandomizeStmt &stmt) override { lowerer_.lowerRandomize(stmt); }
    void visit(const IfStmt &stmt) override { lowerer_.lowerIf(stmt); }
    void visit(const SelectCaseStmt &stmt) override { lowerer_.lowerSelectCase(stmt); }
    void visit(const WhileStmt &stmt) override { lowerer_.lowerWhile(stmt); }
    void visit(const DoStmt &stmt) override { lowerer_.lowerDo(stmt); }
    void visit(const ForStmt &stmt) override { lowerer_.lowerFor(stmt); }
    void visit(const NextStmt &stmt) override { lowerer_.lowerNext(stmt); }
    void visit(const ExitStmt &stmt) override { lowerer_.lowerExit(stmt); }
    void visit(const GotoStmt &stmt) override { lowerer_.lowerGoto(stmt); }
    void visit(const GosubStmt &stmt) override { lowerer_.lowerGosub(stmt); }
    void visit(const OpenStmt &stmt) override { lowerer_.lowerOpen(stmt); }
    void visit(const CloseStmt &stmt) override { lowerer_.lowerClose(stmt); }
    void visit(const SeekStmt &stmt) override { lowerer_.lowerSeek(stmt); }
    void visit(const OnErrorGoto &stmt) override { lowerer_.lowerOnErrorGoto(stmt); }
    void visit(const Resume &stmt) override { lowerer_.lowerResume(stmt); }
    void visit(const EndStmt &stmt) override { lowerer_.lowerEnd(stmt); }
    void visit(const InputStmt &stmt) override { lowerer_.lowerInput(stmt); }
    void visit(const InputChStmt &stmt) override { lowerer_.lowerInputCh(stmt); }
    void visit(const LineInputChStmt &stmt) override { lowerer_.lowerLineInputCh(stmt); }
    void visit(const ReturnStmt &stmt) override { lowerer_.lowerReturn(stmt); }
    void visit(const FunctionDecl &) override {}
    void visit(const SubDecl &) override {}
    void visit(const StmtList &stmt) override { lowerer_.lowerStmtList(stmt); }
    void visit(const DeleteStmt &stmt) override { lowerer_.lowerDelete(stmt); }
    void visit(const ConstructorDecl &) override {}
    void visit(const DestructorDecl &) override {}
    void visit(const MethodDecl &) override {}
    void visit(const ClassDecl &) override {}
    void visit(const TypeDecl &) override {}

  private:
    Lowerer &lowerer_;
};

void Lowerer::lowerStmt(const Stmt &stmt)
{
    curLoc = stmt.loc;
    LowererStmtVisitor visitor(*this);
    visitor.visitStmt(stmt);
}

void Lowerer::lowerStmtList(const StmtList &stmt)
{
    for (const auto &child : stmt.stmts)
    {
        if (!child)
            continue;
        BasicBlock *current = context().current();
        if (current && current->terminated)
            break;
        lowerStmt(*child);
    }
}

void Lowerer::lowerCallStmt(const CallStmt &stmt)
{
    if (!stmt.call)
        return;
    curLoc = stmt.loc;
    lowerExpr(*stmt.call);
}

void Lowerer::lowerReturn(const ReturnStmt &stmt)
{
    if (stmt.isGosubReturn)
    {
        lowerGosubReturn(stmt);
        return;
    }

    if (stmt.value)
    {
        RVal v = lowerExpr(*stmt.value);
        emitRet(v.value);
    }
    else
    {
        emitRetVoid();
    }
}

} // namespace il::frontends::basic
