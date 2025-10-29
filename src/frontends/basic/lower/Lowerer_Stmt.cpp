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
/// @details Implements the generated @ref StmtVisitor interface and forwards
///          each concrete statement to the appropriate helper on
///          @ref Lowerer. The visitor borrows the lowering context so it can set
///          source locations and reuse shared facilities such as block
///          management and runtime call emission.
class LowererStmtVisitor final : public lower::AstVisitor, public StmtVisitor
{
  public:
    /// @brief Construct a visitor that operates on @p lowerer.
    /// @details The visitor caches a reference to the lowering context and does
    ///          not take ownership of any AST nodes.
    /// @param lowerer Lowering context that provides statement helper methods.
    explicit LowererStmtVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Ignore expression nodes encountered via the generic visitor.
    /// @details Statement lowering never processes expressions directly, so this
    ///          override is a no-op required by the @ref lower::AstVisitor base.
    void visitExpr(const Expr &) override {}

    /// @brief Dispatch a statement node to its specialised visitor.
    /// @details Invokes @ref Stmt::accept to trigger double dispatch and update
    ///          the visitor's lowering state accordingly.
    /// @param stmt Statement node to lower into IL.
    void visitStmt(const Stmt &stmt) override
    {
        stmt.accept(*this);
    }

    /// @brief Observe a label definition without emitting code.
    /// @details Labels are handled elsewhere when control-flow instructions bind
    ///          to them, so the visitor performs no action during the initial
    ///          walk.
    void visit(const LabelStmt &) override {}

    /// @brief Lower a PRINT statement.
    /// @details Forwards to @ref Lowerer::lowerPrint which handles argument
    ///          lowering and runtime dispatch.
    /// @param stmt PRINT statement node.
    void visit(const PrintStmt &stmt) override { lowerer_.lowerPrint(stmt); }

    /// @brief Lower a PRINT# (channel) statement.
    /// @details Delegates to @ref Lowerer::lowerPrintCh so channel-specific
    ///          semantics remain encapsulated in the helper.
    /// @param stmt PRINT# statement node.
    void visit(const PrintChStmt &stmt) override { lowerer_.lowerPrintCh(stmt); }

    /// @brief Lower a call statement that invokes a procedure.
    /// @details Defers to @ref Lowerer::lowerCallStmt to reuse the call lowering
    ///          logic shared with expression contexts.
    /// @param stmt CALL statement node.
    void visit(const CallStmt &stmt) override { lowerer_.lowerCallStmt(stmt); }

    /// @brief Lower the CLS statement.
    /// @details Invokes the generic @ref Lowerer::visit helper which dispatches
    ///          to the runtime bridge for screen clearing.
    /// @param stmt CLS statement node.
    void visit(const ClsStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower the COLOR statement.
    /// @details Delegates to @ref Lowerer::visit so colour changes go through the
    ///          shared runtime helper path.
    /// @param stmt COLOR statement node.
    void visit(const ColorStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower the LOCATE statement.
    /// @details Uses @ref Lowerer::visit to emit runtime calls for cursor
    ///          positioning.
    /// @param stmt LOCATE statement node.
    void visit(const LocateStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower a LET assignment statement.
    /// @details Delegates to @ref Lowerer::lowerLet which manages target storage
    ///          resolution and expression coercion.
    /// @param stmt LET statement node.
    void visit(const LetStmt &stmt) override { lowerer_.lowerLet(stmt); }

    /// @brief Lower a DIM statement.
    /// @details Only array declarations require code generation; scalar DIM
    ///          statements are compile-time declarations and therefore skipped.
    /// @param stmt DIM statement node.
    void visit(const DimStmt &stmt) override
    {
        if (stmt.isArray)
            lowerer_.lowerDim(stmt);
    }
    /// @brief Lower a REDIM statement.
    /// @details Delegates to @ref Lowerer::lowerReDim to emit runtime reallocation logic.
    /// @param stmt REDIM statement node.
    void visit(const ReDimStmt &stmt) override { lowerer_.lowerReDim(stmt); }

    /// @brief Lower a RANDOMIZE statement.
    /// @details Forwards to @ref Lowerer::lowerRandomize so seeding semantics
    ///          remain centralised.
    /// @param stmt RANDOMIZE statement node.
    void visit(const RandomizeStmt &stmt) override { lowerer_.lowerRandomize(stmt); }

    /// @brief Lower an IF statement.
    /// @details Delegates to @ref Lowerer::lowerIf which constructs the control
    ///          flow graph for conditional execution.
    /// @param stmt IF statement node.
    void visit(const IfStmt &stmt) override { lowerer_.lowerIf(stmt); }

    /// @brief Lower a SELECT CASE statement.
    /// @details Uses @ref Lowerer::lowerSelectCase to build the dispatch blocks
    ///          for each case branch.
    /// @param stmt SELECT CASE statement node.
    void visit(const SelectCaseStmt &stmt) override { lowerer_.lowerSelectCase(stmt); }

    /// @brief Lower a WHILE loop.
    /// @details Delegates to @ref Lowerer::lowerWhile which wires up loop entry
    ///          and exit blocks.
    /// @param stmt WHILE statement node.
    void visit(const WhileStmt &stmt) override { lowerer_.lowerWhile(stmt); }

    /// @brief Lower a DO loop.
    /// @details Forwards to @ref Lowerer::lowerDo to cover DO WHILE and DO UNTIL
    ///          semantics.
    /// @param stmt DO statement node.
    void visit(const DoStmt &stmt) override { lowerer_.lowerDo(stmt); }

    /// @brief Lower a FOR loop.
    /// @details Delegates to @ref Lowerer::lowerFor which emits induction setup
    ///          and loop body blocks.
    /// @param stmt FOR statement node.
    void visit(const ForStmt &stmt) override { lowerer_.lowerFor(stmt); }

    /// @brief Lower a NEXT statement.
    /// @details Uses @ref Lowerer::lowerNext to advance the active FOR loop's
    ///          induction variable and evaluate continuation conditions.
    /// @param stmt NEXT statement node.
    void visit(const NextStmt &stmt) override { lowerer_.lowerNext(stmt); }

    /// @brief Lower an EXIT statement.
    /// @details Delegates to @ref Lowerer::lowerExit so the correct exit target
    ///          (loop or procedure) is selected.
    /// @param stmt EXIT statement node.
    void visit(const ExitStmt &stmt) override { lowerer_.lowerExit(stmt); }

    /// @brief Lower a GOTO statement.
    /// @details Forwards to @ref Lowerer::lowerGoto which resolves labels and
    ///          emits branch instructions.
    /// @param stmt GOTO statement node.
    void visit(const GotoStmt &stmt) override { lowerer_.lowerGoto(stmt); }

    /// @brief Lower a GOSUB statement.
    /// @details Uses @ref Lowerer::lowerGosub to emit call/return bookkeeping for
    ///          subroutine invocations.
    /// @param stmt GOSUB statement node.
    void visit(const GosubStmt &stmt) override { lowerer_.lowerGosub(stmt); }

    /// @brief Lower an OPEN statement for file handles.
    /// @details Delegates to @ref Lowerer::lowerOpen to generate runtime calls
    ///          for channel creation.
    /// @param stmt OPEN statement node.
    void visit(const OpenStmt &stmt) override { lowerer_.lowerOpen(stmt); }

    /// @brief Lower a CLOSE statement.
    /// @details Invokes @ref Lowerer::lowerClose so channel shutdown logic is
    ///          reused across the compiler.
    /// @param stmt CLOSE statement node.
    void visit(const CloseStmt &stmt) override { lowerer_.lowerClose(stmt); }

    /// @brief Lower a SEEK statement.
    /// @details Delegates to @ref Lowerer::lowerSeek which emits the runtime
    ///          reposition call.
    /// @param stmt SEEK statement node.
    void visit(const SeekStmt &stmt) override { lowerer_.lowerSeek(stmt); }

    /// @brief Lower an ON ERROR GOTO handler.
    /// @details Forwards to @ref Lowerer::lowerOnErrorGoto to wire the error
    ///          handling metadata.
    /// @param stmt ON ERROR GOTO statement node.
    void visit(const OnErrorGoto &stmt) override { lowerer_.lowerOnErrorGoto(stmt); }

    /// @brief Lower a RESUME statement.
    /// @details Delegates to @ref Lowerer::lowerResume so the runtime resumes the
    ///          saved error state appropriately.
    /// @param stmt RESUME statement node.
    void visit(const Resume &stmt) override { lowerer_.lowerResume(stmt); }

    /// @brief Lower an END statement.
    /// @details Forwards to @ref Lowerer::lowerEnd which emits program
    ///          termination code.
    /// @param stmt END statement node.
    void visit(const EndStmt &stmt) override { lowerer_.lowerEnd(stmt); }

    /// @brief Lower an INPUT statement.
    /// @details Delegates to @ref Lowerer::lowerInput to emit runtime prompts and
    ///          assignments.
    /// @param stmt INPUT statement node.
    void visit(const InputStmt &stmt) override { lowerer_.lowerInput(stmt); }

    /// @brief Lower an INPUT# statement.
    /// @details Uses @ref Lowerer::lowerInputCh to process channel-based input.
    /// @param stmt INPUT# statement node.
    void visit(const InputChStmt &stmt) override { lowerer_.lowerInputCh(stmt); }

    /// @brief Lower a LINE INPUT# statement.
    /// @details Delegates to @ref Lowerer::lowerLineInputCh for buffered channel
    ///          reads.
    /// @param stmt LINE INPUT# statement node.
    void visit(const LineInputChStmt &stmt) override { lowerer_.lowerLineInputCh(stmt); }

    /// @brief Lower a RETURN statement.
    /// @details Forwards to @ref Lowerer::lowerReturn which distinguishes normal
    ///          procedure returns from GOSUB returns.
    /// @param stmt RETURN statement node.
    void visit(const ReturnStmt &stmt) override { lowerer_.lowerReturn(stmt); }

    /// @brief Ignore function declarations encountered during statement lowering.
    /// @details Declarations are handled by dedicated compilation phases so the
    ///          visitor performs no action.
    void visit(const FunctionDecl &) override {}

    /// @brief Ignore subroutine declarations.
    /// @details Lowering of procedure bodies occurs elsewhere; the visitor keeps
    ///          traversal stable by doing nothing.
    void visit(const SubDecl &) override {}

    /// @brief Lower a nested statement list.
    /// @details Delegates to @ref Lowerer::lowerStmtList so child statements are
    ///          processed sequentially.
    /// @param stmt Statement list node.
    void visit(const StmtList &stmt) override { lowerer_.lowerStmtList(stmt); }

    /// @brief Lower a DELETE statement.
    /// @details Uses @ref Lowerer::lowerDelete to emit runtime calls for removing
    ///          files.
    /// @param stmt DELETE statement node.
    void visit(const DeleteStmt &stmt) override { lowerer_.lowerDelete(stmt); }

    /// @brief Ignore constructor declarations encountered during lowering.
    /// @details Constructors are compiled via dedicated passes, so this visitor
    ///          leaves them untouched.
    void visit(const ConstructorDecl &) override {}

    /// @brief Ignore destructor declarations.
    /// @details Destructor lowering is handled elsewhere, so the visitor takes
    ///          no action.
    void visit(const DestructorDecl &) override {}

    /// @brief Ignore method declarations not currently being lowered.
    /// @details Method bodies are lowered via separate entry points, so the
    ///          visitor skips declaration nodes.
    void visit(const MethodDecl &) override {}

    /// @brief Ignore class declarations.
    /// @details Structural declarations do not produce IL during statement
    ///          lowering and are therefore skipped.
    void visit(const ClassDecl &) override {}

    /// @brief Ignore type declarations.
    /// @details The type system compiler processes these nodes separately, so no
    ///          action is required here.
    void visit(const TypeDecl &) override {}

  private:
    Lowerer &lowerer_;
};

/// @brief Lower a single BASIC statement into IL.
/// @details Updates the active source location for diagnostics, instantiates a
///          @ref LowererStmtVisitor, and lets it process the statement node.
/// @param stmt Statement to lower.
void Lowerer::lowerStmt(const Stmt &stmt)
{
    curLoc = stmt.loc;
    LowererStmtVisitor visitor(*this);
    visitor.visitStmt(stmt);
}

/// @brief Lower a sequence of BASIC statements.
/// @details Iterates over child statements while respecting block termination
///          so unreachable statements are skipped. Each child is lowered via
///          @ref lowerStmt.
/// @param stmt Statement list to lower.
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

/// @brief Lower a statement-level procedure invocation.
/// @details Ensures the call expression exists, updates the source location, and
///          defers to @ref lowerExpr so argument coercions are reused.
/// @param stmt CALL statement node containing the procedure invocation.
void Lowerer::lowerCallStmt(const CallStmt &stmt)
{
    if (!stmt.call)
        return;
    curLoc = stmt.loc;
    lowerExpr(*stmt.call);
}

/// @brief Lower a RETURN statement.
/// @details Distinguishes between GOSUB returns—which route through
///          @ref lowerGosubReturn—and normal procedure returns. When the
///          statement includes a value it is lowered and emitted via @ref emitRet;
///          otherwise a void return is generated.
/// @param stmt RETURN statement node.
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
