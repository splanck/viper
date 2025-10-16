// File: src/frontends/basic/SemanticAnalyzer.Stmts.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements statement dispatch for the BASIC semantic analyzer,
//          forwarding nodes to themed helper implementations.
// Key invariants: Statement visitors propagate scope information and delegate to
//                 specialized analyzers per statement category.
// Ownership/Lifetime: Analyzer borrows DiagnosticEmitter; AST nodes remain
//                     owned externally.
// Links: docs/codemap.md

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

namespace il::frontends::basic
{

class SemanticAnalyzerStmtVisitor final : public MutStmtVisitor
{
  public:
    explicit SemanticAnalyzerStmtVisitor(SemanticAnalyzer &analyzer) noexcept
        : analyzer_(analyzer)
    {
    }

    void visit(LabelStmt &) override {}
    void visit(PrintStmt &stmt) override { analyzer_.analyzePrint(stmt); }
    void visit(PrintChStmt &stmt) override { analyzer_.analyzePrintCh(stmt); }
    void visit(CallStmt &stmt) override { analyzer_.analyzeCallStmt(stmt); }
    void visit(ClsStmt &stmt) override { analyzer_.analyzeCls(stmt); }
    void visit(ColorStmt &stmt) override { analyzer_.analyzeColor(stmt); }
    void visit(LocateStmt &stmt) override { analyzer_.analyzeLocate(stmt); }
    void visit(LetStmt &stmt) override { analyzer_.analyzeLet(stmt); }
    void visit(DimStmt &stmt) override { analyzer_.analyzeDim(stmt); }
    void visit(ReDimStmt &stmt) override { analyzer_.analyzeReDim(stmt); }
    void visit(RandomizeStmt &stmt) override { analyzer_.analyzeRandomize(stmt); }
    void visit(IfStmt &stmt) override { analyzer_.analyzeIf(stmt); }
    void visit(SelectCaseStmt &stmt) override { analyzer_.analyzeSelectCase(stmt); }
    void visit(WhileStmt &stmt) override { analyzer_.analyzeWhile(stmt); }
    void visit(DoStmt &stmt) override { analyzer_.analyzeDo(stmt); }
    void visit(ForStmt &stmt) override { analyzer_.analyzeFor(stmt); }
    void visit(NextStmt &stmt) override { analyzer_.analyzeNext(stmt); }
    void visit(ExitStmt &stmt) override { analyzer_.analyzeExit(stmt); }
    void visit(GotoStmt &stmt) override { analyzer_.analyzeGoto(stmt); }
    void visit(GosubStmt &stmt) override { analyzer_.analyzeGosub(stmt); }
    void visit(OpenStmt &stmt) override { analyzer_.analyzeOpen(stmt); }
    void visit(CloseStmt &stmt) override { analyzer_.analyzeClose(stmt); }
    void visit(SeekStmt &stmt) override { analyzer_.analyzeSeek(stmt); }
    void visit(OnErrorGoto &stmt) override { analyzer_.analyzeOnErrorGoto(stmt); }
    void visit(EndStmt &stmt) override { analyzer_.analyzeEnd(stmt); }
    void visit(InputStmt &stmt) override { analyzer_.analyzeInput(stmt); }
    void visit(InputChStmt &stmt) override { analyzer_.analyzeInputCh(stmt); }
    void visit(LineInputChStmt &stmt) override { analyzer_.analyzeLineInputCh(stmt); }
    void visit(Resume &stmt) override { analyzer_.analyzeResume(stmt); }
    void visit(ReturnStmt &stmt) override { analyzer_.analyzeReturn(stmt); }
    void visit(FunctionDecl &) override {}
    void visit(SubDecl &) override {}
    void visit(StmtList &stmt) override { analyzer_.analyzeStmtList(stmt); }

  private:
    SemanticAnalyzer &analyzer_;
};

void SemanticAnalyzer::visitStmt(Stmt &s)
{
    SemanticAnalyzerStmtVisitor visitor(*this);
    s.accept(visitor);
}

void SemanticAnalyzer::analyzeStmtList(const StmtList &lst)
{
    for (const auto &st : lst.stmts)
        if (st)
            visitStmt(*st);
}

} // namespace il::frontends::basic
