//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/LowerStmt.cpp
// Purpose: Provides the core statement visitor and shared lowering utilities
//          used across BASIC statement lowering modules.
// Key invariants: Maintains deterministic dispatch and preserves the active
//                 Lowerer context for downstream helpers.
// Ownership/Lifetime: Operates on Lowerer without owning AST or IL modules.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <optional>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Visitor that lowers BASIC statements using the owning @ref Lowerer.
///
/// @details The visitor delegates each statement to specialised Lowerer methods
///          or inline logic while keeping the lowering context (current block,
///          location, and frame state) synchronised.  Empty overrides mark
///          statements handled by other lowering stages.
class LowererStmtVisitor final : public StmtVisitor
{
  public:
    /// @brief Construct a visitor bound to the lowering context.
    ///
    /// @param lowerer Lowerer providing access to lowering utilities.
    explicit LowererStmtVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Labels are handled elsewhere; no lowering is required here.
    void visit(const LabelStmt &) override {}

    /// @brief Lower a PRINT statement through the dedicated helper.
    ///
    /// @param stmt PRINT statement to lower.
    void visit(const PrintStmt &stmt) override { lowerer_.lowerPrint(stmt); }

    /// @brief Lower a PRINT# statement via the shared helper.
    ///
    /// @param stmt PRINT# statement to lower.
    void visit(const PrintChStmt &stmt) override { lowerer_.lowerPrintCh(stmt); }

    /// @brief Lower a CALL statement by delegating to expression lowering.
    ///
    /// @param stmt CALL statement to lower.
    void visit(const CallStmt &stmt) override { lowerer_.lowerCallStmt(stmt); }

    /// @brief Lower CLS via the generic visit hook (terminal helpers).
    void visit(const ClsStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower COLOR via the generic visit hook.
    void visit(const ColorStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower LOCATE via the generic visit hook.
    void visit(const LocateStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower LET statements through the dedicated helper.
    ///
    /// @param stmt LET statement to lower.
    void visit(const LetStmt &stmt) override { lowerer_.lowerLet(stmt); }

    /// @brief Lower DIM statements, delegating to array handling when needed.
    ///
    /// @param stmt DIM statement to lower.
    void visit(const DimStmt &stmt) override
    {
        if (stmt.isArray)
            lowerer_.lowerDim(stmt);
    }

    /// @brief Lower a REDIM statement.
    void visit(const ReDimStmt &stmt) override { lowerer_.lowerReDim(stmt); }

    /// @brief Lower a RANDOMIZE statement.
    void visit(const RandomizeStmt &stmt) override { lowerer_.lowerRandomize(stmt); }

    /// @brief Lower an IF statement.
    void visit(const IfStmt &stmt) override { lowerer_.lowerIf(stmt); }

    /// @brief Lower a SELECT CASE statement.
    void visit(const SelectCaseStmt &stmt) override { lowerer_.lowerSelectCase(stmt); }

    /// @brief Lower a WHILE loop.
    void visit(const WhileStmt &stmt) override { lowerer_.lowerWhile(stmt); }

    /// @brief Lower a DO loop.
    void visit(const DoStmt &stmt) override { lowerer_.lowerDo(stmt); }

    /// @brief Lower a FOR loop.
    void visit(const ForStmt &stmt) override { lowerer_.lowerFor(stmt); }

    /// @brief Lower a NEXT statement that advances loop iterators.
    void visit(const NextStmt &stmt) override { lowerer_.lowerNext(stmt); }

    /// @brief Lower EXIT statements.
    void visit(const ExitStmt &stmt) override { lowerer_.lowerExit(stmt); }

    /// @brief Lower GOTO statements.
    void visit(const GotoStmt &stmt) override { lowerer_.lowerGoto(stmt); }

    /// @brief Lower GOSUB statements.
    void visit(const GosubStmt &stmt) override { lowerer_.lowerGosub(stmt); }

    /// @brief Lower OPEN statements.
    void visit(const OpenStmt &stmt) override { lowerer_.lowerOpen(stmt); }

    /// @brief Lower CLOSE statements.
    void visit(const CloseStmt &stmt) override { lowerer_.lowerClose(stmt); }

    /// @brief Lower SEEK statements.
    void visit(const SeekStmt &stmt) override { lowerer_.lowerSeek(stmt); }

    /// @brief Lower ON ERROR GOTO handlers.
    void visit(const OnErrorGoto &stmt) override { lowerer_.lowerOnErrorGoto(stmt); }

    /// @brief Lower RESUME statements.
    void visit(const Resume &stmt) override { lowerer_.lowerResume(stmt); }

    /// @brief Lower END statements.
    void visit(const EndStmt &stmt) override { lowerer_.lowerEnd(stmt); }

    /// @brief Lower INPUT statements.
    void visit(const InputStmt &stmt) override { lowerer_.lowerInput(stmt); }

    /// @brief Lower INPUT# statements.
    void visit(const InputChStmt &stmt) override { lowerer_.lowerInputCh(stmt); }

    /// @brief Lower LINE INPUT# statements.
    void visit(const LineInputChStmt &stmt) override { lowerer_.lowerLineInputCh(stmt); }

    /// @brief Lower RETURN statements.
    void visit(const ReturnStmt &stmt) override { lowerer_.lowerReturn(stmt); }

    /// @brief Function declarations are lowered by dedicated modules.
    void visit(const FunctionDecl &) override {}

    /// @brief Subroutine declarations are lowered elsewhere.
    void visit(const SubDecl &) override {}

    /// @brief Lower nested statement lists recursively.
    ///
    /// @param stmt Statement list to lower.
    void visit(const StmtList &stmt) override { lowerer_.lowerStmtList(stmt); }

    /// @brief Lower DELETE statements.
    void visit(const DeleteStmt &stmt) override { lowerer_.lowerDelete(stmt); }

    /// @brief Class constructor declarations are handled elsewhere.
    void visit(const ConstructorDecl &) override {}

    /// @brief Class destructor declarations are handled elsewhere.
    void visit(const DestructorDecl &) override {}

    /// @brief Method declarations are handled elsewhere.
    void visit(const MethodDecl &) override {}

    /// @brief Class declarations are handled elsewhere.
    void visit(const ClassDecl &) override {}

    /// @brief Type declarations are handled elsewhere.
    void visit(const TypeDecl &) override {}

  private:
    Lowerer &lowerer_;
};

/// @brief Lower a single BASIC statement into IL.
///
/// @param stmt Statement node to lower.
void Lowerer::lowerStmt(const Stmt &stmt)
{
    curLoc = stmt.loc;
    LowererStmtVisitor visitor(*this);
    stmt.accept(visitor);
}

/// @brief Lower an ordered list of statements, stopping at terminators.
///
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

/// @brief Lower a CALL statement by evaluating its callee expression.
///
/// @param stmt CALL statement referencing a procedure invocation.
void Lowerer::lowerCallStmt(const CallStmt &stmt)
{
    if (!stmt.call)
        return;
    curLoc = stmt.loc;
    lowerExpr(*stmt.call);
}

/// @brief Lower a RETURN statement, handling GOSUB and function returns.
///
/// @param stmt RETURN statement describing exit semantics.
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

/// @brief Coerce a channel expression to a 32-bit integer with range checks.
///
/// @param channel Evaluated channel expression.
/// @param loc Source location for diagnostics.
/// @return Normalised channel value suitable for runtime calls.
Lowerer::RVal Lowerer::normalizeChannelToI32(RVal channel, il::support::SourceLoc loc)
{
    if (channel.type.kind == Type::Kind::I32)
        return channel;

    channel = ensureI64(std::move(channel), loc);
    curLoc = loc;
    channel.value = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), channel.value);
    channel.type = Type(Type::Kind::I32);
    return channel;
}

/// @brief Emit a runtime error check that branches to a failure handler.
///
/// @param err Value produced by a runtime call where zero indicates success.
/// @param loc Source location associated with the check.
/// @param labelStem Stem used to generate diagnostic block labels.
/// @param onFailure Callback invoked to emit failure handling code.
void Lowerer::emitRuntimeErrCheck(Value err,
                                  il::support::SourceLoc loc,
                                  std::string_view labelStem,
                                  const std::function<void(Value)> &onFailure)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *original = ctx.current();
    if (!func || !original)
        return;

    size_t curIdx = static_cast<size_t>(original - &func->blocks[0]);
    BlockNamer *blockNamer = ctx.blockNames().namer();
    std::string stem(labelStem);
    std::string failLbl = blockNamer ? blockNamer->generic(stem + "_fail")
                                     : mangler.block(stem + "_fail");
    std::string contLbl = blockNamer ? blockNamer->generic(stem + "_cont")
                                     : mangler.block(stem + "_cont");

    size_t failIdx = func->blocks.size();
    builder->addBlock(*func, failLbl);
    size_t contIdx = func->blocks.size();
    builder->addBlock(*func, contLbl);

    BasicBlock *failBlk = &func->blocks[failIdx];
    BasicBlock *contBlk = &func->blocks[contIdx];

    ctx.setCurrent(&func->blocks[curIdx]);
    curLoc = loc;
    // Runtime helpers surface 32-bit error codes; widen to i64 so the compare
    // uses operands compatible with ICmpNe's 64-bit expectation.
    RVal errCoerced{err, Type(Type::Kind::I32)};
    errCoerced = ensureI64(std::move(errCoerced), loc);
    Value isFail =
        emitBinary(Opcode::ICmpNe, ilBoolTy(), errCoerced.value, Value::constInt(0));
    emitCBr(isFail, failBlk, contBlk);

    ctx.setCurrent(failBlk);
    curLoc = loc;
    onFailure(err);

    ctx.setCurrent(contBlk);
}

} // namespace il::frontends::basic
