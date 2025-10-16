// File: src/frontends/basic/LowerStmt.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Provides the core statement visitor and shared lowering utilities
//          used across BASIC statement lowering modules.
// Key invariants: Maintains deterministic dispatch and preserves the active
//                 Lowerer context for downstream helpers.
// Ownership/Lifetime: Operates on Lowerer without owning AST or IL modules.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <optional>

using namespace il::core;

namespace il::frontends::basic
{

class LowererStmtVisitor final : public StmtVisitor
{
  public:
    explicit LowererStmtVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

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

  private:
    Lowerer &lowerer_;
};

void Lowerer::lowerStmt(const Stmt &stmt)
{
    curLoc = stmt.loc;
    LowererStmtVisitor visitor(*this);
    stmt.accept(visitor);
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
    Value isFail = emitBinary(Opcode::ICmpNe, ilBoolTy(), err, Value::constInt(0));
    emitCBr(isFail, failBlk, contBlk);

    ctx.setCurrent(failBlk);
    curLoc = loc;
    onFailure(err);

    ctx.setCurrent(contBlk);
}

} // namespace il::frontends::basic
