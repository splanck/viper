//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements control-flow emission helpers for the BASIC lowerer.
/// @details Routines in this translation unit manipulate the active block,
/// append terminators, and manage exception-handling stacks. Callers must
/// establish the desired current block before invoking these helpers; each
/// helper ensures terminators are emitted exactly once and that the lowerer
/// preserves ownership of temporary values managed by the ProcedureContext.

#include "frontends/basic/Lowerer.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

void Lowerer::emitForStep(Value slot, Value step)
{
    Value load = emitLoad(Type(Type::Kind::I64), slot);
    Value add = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), load, step);
    emitStore(Type(Type::Kind::I64), slot, add);
}

void Lowerer::emitBr(BasicBlock *target)
{
    BasicBlock *block = context().current();
    assert(block && "emitBr requires an active block");

    if (block == target)
    {
        return;
    }

    Instr in;
    in.op = Opcode::Br;
    in.type = Type(Type::Kind::Void);
    if (target->label.empty())
        target->label = nextFallbackBlockLabel();
    in.labels.push_back(target->label);
    in.loc = curLoc;
    block->instructions.push_back(in);
    block->terminated = true;
}

void Lowerer::emitCBr(Value cond, BasicBlock *t, BasicBlock *f)
{
    Instr in;
    in.op = Opcode::CBr;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(cond);
    in.labels.push_back(t->label);
    in.labels.push_back(f->label);
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitCBr requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

void Lowerer::lowerCondBranch(const Expr &expr,
                              BasicBlock *trueBlk,
                              BasicBlock *falseBlk,
                              il::support::SourceLoc loc)
{
    if (const auto *bin = dynamic_cast<const BinaryExpr *>(&expr))
    {
        const bool isAnd = bin->op == BinaryExpr::Op::LogicalAnd;
        const bool isOr = bin->op == BinaryExpr::Op::LogicalOr;
        if (isAnd || isOr)
        {
            ProcedureContext &ctx = context();
            Function *func = ctx.function();
            assert(func && ctx.current());

            auto indexOf = [&](BasicBlock *bb)
            {
                assert(bb && "lowerCondBranch requires non-null block");
                return static_cast<size_t>(bb - &func->blocks[0]);
            };

            size_t curIdx = indexOf(ctx.current());
            size_t trueIdx = indexOf(trueBlk);
            size_t falseIdx = indexOf(falseBlk);

            BlockNamer *blockNamer = ctx.blockNames().namer();
            std::string hint = isAnd ? "and_rhs" : "or_rhs";
            std::string midLbl = blockNamer ? blockNamer->generic(hint) : mangler.block(hint);

            size_t midIdx = func->blocks.size();
            builder->addBlock(*func, midLbl);

            func = ctx.function();
            BasicBlock *mid = &func->blocks[midIdx];
            BasicBlock *cur = &func->blocks[curIdx];
            BasicBlock *trueTarget = &func->blocks[trueIdx];
            BasicBlock *falseTarget = &func->blocks[falseIdx];
            ctx.setCurrent(cur);

            if (isAnd)
                lowerCondBranch(*bin->lhs, mid, falseTarget, loc);
            else
                lowerCondBranch(*bin->lhs, trueTarget, mid, loc);

            func = ctx.function();
            mid = &func->blocks[midIdx];
            trueTarget = &func->blocks[trueIdx];
            falseTarget = &func->blocks[falseIdx];
            ctx.setCurrent(mid);

            lowerCondBranch(*bin->rhs, trueTarget, falseTarget, loc);
            return;
        }
    }

    RVal cond = lowerExpr(expr);
    cond = coerceToBool(std::move(cond), loc);
    emitCBr(cond.value, trueBlk, falseBlk);
}

void Lowerer::emitEhPush(BasicBlock *handler)
{
    assert(handler && "emitEhPush requires a handler block");
    Instr in;
    in.op = Opcode::EhPush;
    in.type = Type(Type::Kind::Void);
    in.labels.push_back(handler->label);
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitEhPush requires an active block");
    block->instructions.push_back(in);
}

void Lowerer::emitEhPop()
{
    Instr in;
    in.op = Opcode::EhPop;
    in.type = Type(Type::Kind::Void);
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitEhPop requires an active block");
    block->instructions.push_back(in);
}

void Lowerer::emitEhPopForReturn()
{
    if (!context().errorHandlers().active())
        return;
    emitEhPop();
}

void Lowerer::clearActiveErrorHandler()
{
    ProcedureContext &ctx = context();
    if (ctx.errorHandlers().active())
        emitEhPop();
    ctx.errorHandlers().setActive(false);
    ctx.errorHandlers().setActiveIndex(std::nullopt);
    ctx.errorHandlers().setActiveLine(std::nullopt);
}

BasicBlock *Lowerer::ensureErrorHandlerBlock(int targetLine)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && "ensureErrorHandlerBlock requires an active function");

    auto &handlers = ctx.errorHandlers().blocks();
    auto it = handlers.find(targetLine);
    if (it != handlers.end())
        return &func->blocks[it->second];

    std::string base = "handler_L" + std::to_string(targetLine);
    std::string label;
    if (BlockNamer *blockNamer = ctx.blockNames().namer())
        label = blockNamer->tag(base);
    else
        label = mangler.block(base);

    std::vector<il::core::Param> params = {{"err", Type(Type::Kind::Error)},
                                           {"tok", Type(Type::Kind::ResumeTok)}};
    BasicBlock &bb = builder->createBlock(*func, label, params);

    Instr entry;
    entry.op = Opcode::EhEntry;
    entry.type = Type(Type::Kind::Void);
    entry.loc = {};
    bb.instructions.push_back(entry);

    size_t idx = static_cast<size_t>(&bb - &func->blocks[0]);
    handlers[targetLine] = idx;
    ctx.errorHandlers().handlerTargets()[idx] = targetLine;
    return &bb;
}

void Lowerer::emitRet(Value v)
{
    emitEhPopForReturn();
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.operands.push_back(v);
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitRet requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

void Lowerer::emitRetVoid()
{
    emitEhPopForReturn();
    Instr in;
    in.op = Opcode::Ret;
    in.type = Type(Type::Kind::Void);
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitRetVoid requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

void Lowerer::emitTrap()
{
    Instr in;
    in.op = Opcode::Trap;
    in.type = Type(Type::Kind::Void);
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitTrap requires an active block");
    block->instructions.push_back(in);
    block->terminated = true;
}

void Lowerer::emitTrapFromErr(Value errCode)
{
    Instr in;
    in.op = Opcode::TrapFromErr;
    in.type = Type(Type::Kind::I32);
    in.operands.push_back(errCode);
    in.loc = curLoc;
    BasicBlock *block = context().current();
    assert(block && "emitTrapFromErr requires an active block");
    block->instructions.push_back(std::move(in));
    block->terminated = true;
}

} // namespace il::frontends::basic
