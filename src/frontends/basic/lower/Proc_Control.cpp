//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Proc_Control.cpp
// Purpose: Houses control-flow oriented helpers for BASIC procedure lowering,
//          including block finalisation and runtime stack setup.
// Key invariants: Procedure contexts remain in sync with emitted IL blocks and
//                 gosub stacks are lazily materialised once per procedure.
// Ownership/Lifetime: Operates on Lowerer-managed contexts and IR; no persistent
//                     state beyond block naming counters.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

using namespace il::core;

namespace viper::basic::lower::control
{

void emit(::il::frontends::basic::ProcedureLowering &lowering,
          ::il::frontends::basic::ProcedureLowering::LoweringContext &ctx)
{
    lowering.emitProcedureIL(ctx);
}

} // namespace viper::basic::lower::control

namespace il::frontends::basic
{

void ProcedureLowering::emitProcedureIL(LoweringContext &ctx)
{
    const auto &config = ctx.config;
    if (!config.emitEmptyBody || !config.emitFinalReturn || !ctx.function)
        return;

    auto &procCtx = lowerer.context();

    if (ctx.bodyStmts.empty())
    {
        lowerer.curLoc = {};
        config.emitEmptyBody();
        procCtx.blockNames().resetNamer();
        return;
    }

    lowerer.lowerStatementSequence(ctx.bodyStmts, /*stopOnTerminated=*/true);

    procCtx.setCurrent(&ctx.function->blocks[procCtx.exitIndex()]);
    lowerer.curLoc = {};
    lowerer.releaseObjectLocals(ctx.paramNames);
    lowerer.releaseObjectParams(ctx.paramNames);
    lowerer.releaseArrayLocals(ctx.paramNames);
    lowerer.releaseArrayParams(ctx.paramNames);
    lowerer.curLoc = {};
    config.emitFinalReturn();

    procCtx.blockNames().resetNamer();
}

void Lowerer::ensureGosubStack()
{
    ProcedureContext &ctx = context();
    auto &state = ctx.gosub();
    if (state.hasPrologue())
        return;

    Function *func = ctx.function();
    if (!func)
        return;

    BasicBlock *savedBlock = ctx.current();
    BasicBlock *entry = &func->blocks.front();
    ctx.setCurrent(entry);

    auto savedLoc = curLoc;
    curLoc = {};

    Value spSlot = emitAlloca(8);
    Value stackSlot = emitAlloca(kGosubStackDepth * 4);
    emitStore(Type(Type::Kind::I64), spSlot, Value::constInt(0));
    state.setPrologue(spSlot, stackSlot);

    curLoc = savedLoc;
    ctx.setCurrent(savedBlock);
}

std::string Lowerer::nextFallbackBlockLabel()
{
    return mangler.block("bb_" + std::to_string(nextFallbackBlockId++));
}

} // namespace il::frontends::basic
