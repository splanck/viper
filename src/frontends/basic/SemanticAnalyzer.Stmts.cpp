//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the visitor that dispatches BASIC AST statements to the specialised
// semantic-analysis routines.  Concentrating the forwarding logic in this
// translation unit keeps the public headers slim while documenting how the
// analyser walks statement sequences.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Statement dispatch helpers for the BASIC semantic analyser.
/// @details Defines the visitor class that translates generic AST visits into
///          calls on @ref SemanticAnalyzer.  The functions here manage scope
///          propagation and ensure every statement node is analysed under the
///          correct helper.

#include "frontends/basic/SemanticAnalyzer.Internal.hpp"

namespace il::frontends::basic
{

/// @brief Visitor that forwards AST statements to the owning analyser.
/// @details Each override delegates to the corresponding `SemanticAnalyzer::analyze*`
///          routine so the heavy semantic logic remains in the dedicated
///          compilation units.  The visitor itself owns no resources beyond a
///          reference to the active analyser.
class SemanticAnalyzerStmtVisitor final : public MutStmtVisitor
{
  public:
    /// @brief Construct the visitor with a reference to the active analyser.
    /// @param analyzer Semantic analyser that will process visited statements.
    explicit SemanticAnalyzerStmtVisitor(SemanticAnalyzer &analyzer) noexcept : analyzer_(analyzer)
    {
    }

    /// @brief Labels require no semantic work; ignore them.
    void visit(LabelStmt &) override {}

    /// @brief Delegate PRINT statements to @ref SemanticAnalyzer::analyzePrint.
    void visit(PrintStmt &stmt) override
    {
        analyzer_.analyzePrint(stmt);
    }

    /// @brief Delegate PRINT# channel statements to the analyser.
    void visit(PrintChStmt &stmt) override
    {
        analyzer_.analyzePrintCh(stmt);
    }

    /// @brief BEEP statements require no semantic validation.
    void visit(BeepStmt &) override {}

    /// @brief Resolve subroutine invocations via @ref SemanticAnalyzer::analyzeCallStmt.
    void visit(CallStmt &stmt) override
    {
        analyzer_.analyzeCallStmt(stmt);
    }

    /// @brief Validate CLS statements through the analyser.
    void visit(ClsStmt &stmt) override
    {
        analyzer_.visit(stmt);
    }

    /// @brief Validate CURSOR statements through the analyser.
    void visit(CursorStmt &stmt) override
    {
        analyzer_.visit(stmt);
    }

    /// @brief ALTSCREEN statements require no semantic validation.
    void visit(AltScreenStmt &) override {}

    /// @brief Forward COLOR statements for palette validation.
    void visit(ColorStmt &stmt) override
    {
        analyzer_.visit(stmt);
    }

    /// @brief Forward SLEEP statements for duration validation.
    void visit(SleepStmt &stmt) override
    {
        analyzer_.visit(stmt);
    }

    /// @brief Ensure LOCATE statements honour cursor bounds.
    void visit(LocateStmt &stmt) override
    {
        analyzer_.visit(stmt);
    }

    /// @brief Defer LET assignment analysis to the analyser.
    void visit(LetStmt &stmt) override
    {
        analyzer_.analyzeLet(stmt);
    }

    /// @brief Validate DIM declarations through the analyser.
    void visit(ConstStmt &stmt) override
    {
        analyzer_.analyzeConst(stmt);
    }

    void visit(DimStmt &stmt) override
    {
        analyzer_.analyzeDim(stmt);
    }

    /// @brief STATIC statements declare procedure-local persistent variables.
    void visit(StaticStmt &stmt) override
    {
        analyzer_.analyzeStatic(stmt);
    }

    /// @brief SHARED statements list names that should refer to module-level bindings.
    void visit(SharedStmt &stmt) override
    {
        analyzer_.analyzeShared(stmt);
    }

    /// @brief Re-DIM statements re-use the analyser's array checks.
    void visit(ReDimStmt &stmt) override
    {
        analyzer_.analyzeReDim(stmt);
    }

    /// @brief SWAP statements validate that both operands are valid lvalues.
    void visit(SwapStmt &stmt) override
    {
        analyzer_.analyzeSwap(stmt);
    }

    /// @brief RANDOMIZE statements evaluate seed expressions via the analyser.
    void visit(RandomizeStmt &stmt) override
    {
        analyzer_.analyzeRandomize(stmt);
    }

    /// @brief Forward IF statements for branch analysis.
    void visit(IfStmt &stmt) override
    {
        analyzer_.analyzeIf(stmt);
    }

    /// @brief Delegate SELECT CASE semantics.
    void visit(SelectCaseStmt &stmt) override
    {
        analyzer_.analyzeSelectCase(stmt);
    }

    /// @brief Loop constructs are analysed by dedicated helpers.
    void visit(WhileStmt &stmt) override
    {
        analyzer_.analyzeWhile(stmt);
    }

    /// @brief Defer DO loop validation to the analyser.
    void visit(DoStmt &stmt) override
    {
        analyzer_.analyzeDo(stmt);
    }

    /// @brief Ensure FOR loops are type consistent.
    void visit(ForStmt &stmt) override
    {
        analyzer_.analyzeFor(stmt);
    }

    /// @brief Route NEXT statements to the matching FOR-loop checker.
    void visit(NextStmt &stmt) override
    {
        analyzer_.analyzeNext(stmt);
    }

    /// @brief EXIT statements are validated for legal context.
    void visit(ExitStmt &stmt) override
    {
        analyzer_.analyzeExit(stmt);
    }

    /// @brief Forward GOTO statements for label resolution.
    void visit(GotoStmt &stmt) override
    {
        analyzer_.analyzeGoto(stmt);
    }

    /// @brief Defer GOSUB statements for continuation tracking.
    void visit(GosubStmt &stmt) override
    {
        analyzer_.analyzeGosub(stmt);
    }

    /// @brief OPEN statements perform channel validation.
    void visit(OpenStmt &stmt) override
    {
        analyzer_.analyzeOpen(stmt);
    }

    /// @brief CLOSE statements ensure matching handles.
    void visit(CloseStmt &stmt) override
    {
        analyzer_.analyzeClose(stmt);
    }

    /// @brief SEEK statements validate position operands.
    void visit(SeekStmt &stmt) override
    {
        analyzer_.analyzeSeek(stmt);
    }

    /// @brief ON ERROR GOTO statements delegate to exception analysis.
    void visit(OnErrorGoto &stmt) override
    {
        analyzer_.analyzeOnErrorGoto(stmt);
    }

    /// @brief END statements simply check program termination rules.
    void visit(EndStmt &stmt) override
    {
        analyzer_.analyzeEnd(stmt);
    }

    /// @brief Standard INPUT statements check prompt and target expressions.
    void visit(InputStmt &stmt) override
    {
        analyzer_.analyzeInput(stmt);
    }

    /// @brief Channel-based INPUT statements share analyser logic.
    void visit(InputChStmt &stmt) override
    {
        analyzer_.analyzeInputCh(stmt);
    }

    /// @brief LINE INPUT# statements reuse string-specific validation.
    void visit(LineInputChStmt &stmt) override
    {
        analyzer_.analyzeLineInputCh(stmt);
    }

    /// @brief RESUME statements are validated for legal EH state.
    void visit(Resume &stmt) override
    {
        analyzer_.analyzeResume(stmt);
    }

    /// @brief RETURN statements are handed to the analyser for stack checks.
    void visit(ReturnStmt &stmt) override
    {
        analyzer_.analyzeReturn(stmt);
    }

    /// @brief Function declarations are handled elsewhere; no action needed.
    void visit(FunctionDecl &) override {}

    /// @brief Subroutine declarations are processed in a different pass.
    void visit(SubDecl &) override {}

    /// @brief Nested statement lists are analysed recursively.
    void visit(StmtList &stmt) override
    {
        analyzer_.analyzeStmtList(stmt);
    }

    /// @brief OOP-specific statements are handled by dedicated passes.
    void visit(DeleteStmt &) override {}

    void visit(ConstructorDecl &) override {}

    void visit(DestructorDecl &) override {}

    void visit(MethodDecl &) override {}

    void visit(NamespaceDecl &decl) override
    {
        analyzer_.analyzeNamespaceDecl(decl);
    }

    void visit(TryCatchStmt &stmt) override
    {
        analyzer_.visit(stmt);
    }

    void visit(ClassDecl &decl) override
    {
        analyzer_.analyzeClassDecl(decl);
    }

    void visit(TypeDecl &) override {}

    /// @brief Interface declarations are analysed in dedicated OOP passes.
    void visit(InterfaceDecl &decl) override
    {
        analyzer_.analyzeInterfaceDecl(decl);
    }

    /// @brief USING directives will be processed by namespace semantic pass.
    void visit(UsingDecl &decl) override
    {
        analyzer_.analyzeUsingDecl(decl);
    }

  private:
    SemanticAnalyzer &analyzer_;
};

/// @brief Dispatch a statement node through the semantic analyser visitor.
/// @details Instantiates a fresh @ref SemanticAnalyzerStmtVisitor for each call
///          so that recursive analysis reuses the most up-to-date analyser
///          state.  The node is then asked to accept the visitor, triggering the
///          appropriate override above.
/// @param s Statement node to analyse.
void SemanticAnalyzer::visitStmt(Stmt &s)
{
    SemanticAnalyzerStmtVisitor visitor(*this);
    s.accept(visitor);
}

/// @brief Visit every statement contained within a list.
/// @details Iterates through the list in program order and forwards each entry
///          to @ref visitStmt.  Null entries are ignored, preserving any
///          optional AST placeholders produced during earlier stages.
/// @param lst Sequence of statements to analyse.
void SemanticAnalyzer::analyzeStmtList(const StmtList &lst)
{
    for (const auto &st : lst.stmts)
        if (st)
            visitStmt(*st);
}

} // namespace il::frontends::basic
