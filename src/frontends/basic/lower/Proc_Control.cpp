//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Proc_Control.cpp
// Purpose: Implement control-flow orchestration helpers for BASIC procedure
//          lowering including block scheduling and epilogue emission.
// Key invariants: Required callbacks are validated prior to emission and
//                 procedure contexts are reset between runs.
// Ownership/Lifetime: Operates on the owning Lowerer instance and does not take
//                     ownership of AST nodes or runtime handles.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include "frontends/basic/lower/Emitter.hpp"

#include <cassert>
#include <utility>

using namespace il::core;

namespace viper::basic::lower::control
{

using il::frontends::basic::Lowerer;
using il::frontends::basic::ProcedureLowering;
using il::frontends::basic::SubDecl;

struct API
{
    static void resetContext(ProcedureLowering &lowering, ProcedureLowering::LoweringContext &ctx)
    {
        lowering.lowerer.resetLoweringState();
        (void)ctx;
    }

    static void scheduleBlocks(ProcedureLowering &lowering, ProcedureLowering::LoweringContext &ctx)
    {
        const auto &config = ctx.config;
        assert(config.emitEmptyBody && "Missing empty body return handler");
        assert(config.emitFinalReturn && "Missing final return handler");
        if (!config.emitEmptyBody || !config.emitFinalReturn)
            return;

        auto metadata = ctx.metadata;
        auto &procCtx = lowering.lowerer.context();
        Function &f = ctx.builder.startFunction(ctx.name, config.retType, ctx.irParams);
        ctx.function = &f;
        procCtx.setFunction(&f);
        procCtx.setNextTemp(f.valueNames.size());

        lowering.lowerer.buildProcedureSkeleton(f, ctx.name, *metadata);

        if (!f.blocks.empty())
            procCtx.setCurrent(&f.blocks.front());

        lowering.lowerer.materializeParams(ctx.params);
        lowering.lowerer.allocateLocalSlots(ctx.paramNames, /*includeParams=*/false);
    }

    static void emitProcedureIL(ProcedureLowering &lowering, ProcedureLowering::LoweringContext &ctx)
    {
        const auto &config = ctx.config;
        if (!config.emitEmptyBody || !config.emitFinalReturn || !ctx.function)
            return;

        auto &lowerer = lowering.lowerer;
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

    static void lowerSubDecl(Lowerer &lowerer, const SubDecl &decl)
    {
        Lowerer::ProcedureConfig config;
        config.retType = Type(Type::Kind::Void);
        config.emitEmptyBody = [&]() { lowerer.emitRetVoid(); };
        config.emitFinalReturn = [&]() { lowerer.emitRetVoid(); };

        lowerer.lowerProcedure(decl.name, decl.params, decl.body, config);
    }
};

} // namespace viper::basic::lower::control

namespace il::frontends::basic
{

void ProcedureLowering::resetContext(LoweringContext &ctx)
{
    viper::basic::lower::control::API::resetContext(*this, ctx);
}

void ProcedureLowering::scheduleBlocks(LoweringContext &ctx)
{
    viper::basic::lower::control::API::scheduleBlocks(*this, ctx);
}

void ProcedureLowering::emitProcedureIL(LoweringContext &ctx)
{
    viper::basic::lower::control::API::emitProcedureIL(*this, ctx);
}

void Lowerer::lowerSubDecl(const SubDecl &decl)
{
    viper::basic::lower::control::API::lowerSubDecl(*this, decl);
}

void Lowerer::resetLoweringState()
{
    resetSymbolState();
    context().reset();
    stmtVirtualLines_.clear();
    synthSeq_ = 0;
}

Lowerer::ProcedureContext &Lowerer::context() noexcept
{
    return context_;
}

const Lowerer::ProcedureContext &Lowerer::context() const noexcept
{
    return context_;
}

Emit Lowerer::emitCommon() noexcept
{
    return Emit(*this);
}

Emit Lowerer::emitCommon(il::support::SourceLoc loc) noexcept
{
    Emit helper(*this);
    helper.at(loc);
    return helper;
}

lower::Emitter &Lowerer::emitter() noexcept
{
    assert(emitter_ && "emitter must be initialized");
    return *emitter_;
}

const lower::Emitter &Lowerer::emitter() const noexcept
{
    assert(emitter_ && "emitter must be initialized");
    return *emitter_;
}

} // namespace il::frontends::basic

