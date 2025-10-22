//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/LowerStmt_Control.cpp
// Purpose: Hosts jump-oriented control helpers and delegates structured control
//          to specialised lowering translation units.
// Key invariants: Helpers manipulate the active Lowerer context to preserve
//                 deterministic block graphs for GOTO/GOSUB constructs.
// Ownership/Lifetime: Operates on Lowerer without owning AST nodes or IL state.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Lower a BASIC GOSUB statement using the runtime-managed continuation stack.
///
/// @param stmt GOSUB statement providing the target line and source location.
void Lowerer::lowerGosub(const GosubStmt &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *current = ctx.current();
    if (!func || !current)
        return;

    ensureGosubStack();

    auto &gosubState = ctx.gosub();
    auto contIndex = gosubState.indexFor(&stmt);
    if (!contIndex)
        contIndex = gosubState.registerContinuation(&stmt, ctx.exitIndex());

    curLoc = stmt.loc;
    Value sp = emitLoad(Type(Type::Kind::I64), gosubState.spSlot());

    auto &lineBlocks = ctx.blockNames().lineBlocks();
    auto destIt = lineBlocks.find(stmt.targetLine);
    if (destIt == lineBlocks.end())
        return;

    BlockNamer *blockNamer = ctx.blockNames().namer();
    std::string overflowLbl = blockNamer ? blockNamer->generic("gosub_overflow")
                                         : mangler.block("gosub_overflow");
    std::string pushLbl = blockNamer ? blockNamer->generic("gosub_push")
                                     : mangler.block("gosub_push");

    size_t curIdx = static_cast<size_t>(current - &func->blocks[0]);
    size_t overflowIdx = func->blocks.size();
    builder->addBlock(*func, overflowLbl);
    size_t pushIdx = func->blocks.size();
    builder->addBlock(*func, pushLbl);

    func = ctx.function();
    BasicBlock *overflowBlk = &func->blocks[overflowIdx];
    BasicBlock *pushBlk = &func->blocks[pushIdx];
    current = &func->blocks[curIdx];
    ctx.setCurrent(current);

    Value limit = Value::constInt(kGosubStackDepth);
    Value overflow = emitBinary(Opcode::SCmpGE, ilBoolTy(), sp, limit);
    emitCBr(overflow, overflowBlk, pushBlk);

    ctx.setCurrent(overflowBlk);
    curLoc = stmt.loc;
    requireTrap();
    std::string overflowMsg = getStringLabel("gosub: stack overflow");
    Value overflowStr = emitConstStr(overflowMsg);
    emitCall("rt_trap", {overflowStr});
    emitTrap();

    ctx.setCurrent(pushBlk);
    curLoc = stmt.loc;

    Value offset = emitBinary(Opcode::IMulOvf, Type(Type::Kind::I64), sp, Value::constInt(4));
    Value slotPtr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), gosubState.stackSlot(), offset);
    emitStore(Type(Type::Kind::I32),
              slotPtr,
              Value::constInt(static_cast<long long>(*contIndex)));

    Value nextSp = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), sp, Value::constInt(1));
    emitStore(Type(Type::Kind::I64), gosubState.spSlot(), nextSp);

    BasicBlock *target = &func->blocks[destIt->second];
    emitBr(target);
}

/// @brief Lower an unconditional GOTO statement.
///
/// @param stmt GOTO statement pointing at a target line label.
void Lowerer::lowerGoto(const GotoStmt &stmt)
{
    auto &lineBlocks = context().blockNames().lineBlocks();
    auto it = lineBlocks.find(stmt.target);
    if (it != lineBlocks.end())
    {
        curLoc = stmt.loc;
        Function *func = context().function();
        assert(func && "lowerGoto requires an active function");
        emitBr(&func->blocks[it->second]);
    }
}

/// @brief Lower RETURN statements that exit from a GOSUB invocation.
///
/// @param stmt RETURN statement appearing in GOSUB contexts.
void Lowerer::lowerGosubReturn(const ReturnStmt &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *current = ctx.current();
    if (!func || !current)
        return;

    ensureGosubStack();

    auto &gosubState = ctx.gosub();

    curLoc = stmt.loc;
    Value sp = emitLoad(Type(Type::Kind::I64), gosubState.spSlot());

    BlockNamer *blockNamer = ctx.blockNames().namer();
    std::string emptyLbl = blockNamer ? blockNamer->generic("gosub_ret_empty")
                                      : mangler.block("gosub_ret_empty");
    std::string contLbl = blockNamer ? blockNamer->generic("gosub_ret_cont")
                                     : mangler.block("gosub_ret_cont");

    size_t curIdx = static_cast<size_t>(current - &func->blocks[0]);
    size_t emptyIdx = func->blocks.size();
    builder->addBlock(*func, emptyLbl);
    size_t contIdx = func->blocks.size();
    builder->addBlock(*func, contLbl);

    func = ctx.function();
    BasicBlock *emptyBlk = &func->blocks[emptyIdx];
    BasicBlock *contBlk = &func->blocks[contIdx];
    current = &func->blocks[curIdx];
    ctx.setCurrent(current);

    Value isEmpty = emitBinary(Opcode::ICmpEq, ilBoolTy(), sp, Value::constInt(0));
    emitCBr(isEmpty, emptyBlk, contBlk);

    ctx.setCurrent(emptyBlk);
    curLoc = stmt.loc;
    requireTrap();
    std::string emptyMsg = getStringLabel("gosub: empty return stack");
    Value emptyStr = emitConstStr(emptyMsg);
    emitCall("rt_trap", {emptyStr});
    emitTrap();

    ctx.setCurrent(contBlk);
    curLoc = stmt.loc;

    Value nextSp = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), sp, Value::constInt(1));
    emitStore(Type(Type::Kind::I64), gosubState.spSlot(), nextSp);

    Value offset = emitBinary(Opcode::IMulOvf, Type(Type::Kind::I64), nextSp, Value::constInt(4));
    Value slotPtr = emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), gosubState.stackSlot(), offset);
    Value idxVal = emitLoad(Type(Type::Kind::I32), slotPtr);

    std::string invalidLbl = blockNamer ? blockNamer->generic("gosub_ret_invalid")
                                        : mangler.block("gosub_ret_invalid");
    size_t invalidIdx = func->blocks.size();
    builder->addBlock(*func, invalidLbl);
    func = ctx.function();
    BasicBlock *invalidBlk = &func->blocks[invalidIdx];

    Instr sw;
    sw.op = Opcode::SwitchI32;
    sw.type = Type(Type::Kind::Void);
    sw.operands.push_back(idxVal);
    if (invalidBlk->label.empty())
        invalidBlk->label = nextFallbackBlockLabel();
    sw.labels.push_back(invalidBlk->label);
    sw.brArgs.push_back({});

    const auto &continuations = gosubState.continuations();
    for (unsigned i = 0; i < continuations.size(); ++i)
    {
        sw.operands.push_back(Value::constInt(static_cast<long long>(i)));
        BasicBlock *target = &func->blocks[continuations[i]];
        if (target->label.empty())
            target->label = nextFallbackBlockLabel();
        sw.labels.push_back(target->label);
        sw.brArgs.emplace_back();
    }
    sw.loc = stmt.loc;
    contBlk->instructions.push_back(std::move(sw));
    contBlk->terminated = true;

    ctx.setCurrent(invalidBlk);
    curLoc = stmt.loc;
    emitTrap();
}

/// @brief Lower the END statement, terminating program execution.
///
/// @param stmt END statement providing the source location.
void Lowerer::lowerEnd(const EndStmt &stmt)
{
    curLoc = stmt.loc;
    emitRet(Value::constInt(0));
}

} // namespace il::frontends::basic
