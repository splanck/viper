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
#include "frontends/basic/lower/Emitter.hpp"

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
    emitter().emitBr(target);
}

void Lowerer::emitCBr(Value cond, BasicBlock *t, BasicBlock *f)
{
    emitter().emitCBr(cond, t, f);
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
    emitter().emitEhPush(handler);
}

void Lowerer::emitEhPop()
{
    emitter().emitEhPop();
}

void Lowerer::emitEhPopForReturn()
{
    emitter().emitEhPopForReturn();
}

void Lowerer::clearActiveErrorHandler()
{
    emitter().clearActiveErrorHandler();
}

BasicBlock *Lowerer::ensureErrorHandlerBlock(int targetLine)
{
    return emitter().ensureErrorHandlerBlock(targetLine);
}

void Lowerer::emitRet(Value v)
{
    emitter().emitRet(v);
}

void Lowerer::emitRetVoid()
{
    emitter().emitRetVoid();
}

void Lowerer::emitTrap()
{
    emitter().emitTrap();
}

void Lowerer::emitTrapFromErr(Value errCode)
{
    emitter().emitTrapFromErr(errCode);
}

} // namespace il::frontends::basic
