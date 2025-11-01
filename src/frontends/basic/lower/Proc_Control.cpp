//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Proc_Control.cpp
// Purpose: Implements control-flow oriented helpers for procedure lowering,
//          including block scheduling, virtual line mapping, and gosub support.
// Key invariants: Block labels remain deterministic and entry/exit blocks are
//                 always materialised for each lowered procedure.
// Ownership/Lifetime: Helpers mutate shared Lowerer state but do not own
//                     allocations or IR objects beyond builder emissions.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LowererContext.hpp"
#include "frontends/basic/LoweringPipeline.hpp"
#include "frontends/basic/LineUtils.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"

#include <cassert>
#include <memory>
#include <unordered_set>
#include <vector>

namespace viper::basic::lower::control
{

using il::frontends::basic::Lowerer;
using il::frontends::basic::ProcedureLowering;
using il::frontends::basic::Stmt;
using il::frontends::basic::hasUserLine;

struct Access
{
    static void scheduleBlocks(ProcedureLowering &self, ProcedureLowering::LoweringContext &ctx);
    static void emitProcedureIL(ProcedureLowering &self, ProcedureLowering::LoweringContext &ctx);
    static int virtualLine(Lowerer &lowerer, const Stmt &stmt);
    static void buildProcedureSkeleton(Lowerer &lowerer,
                                       il::core::Function &f,
                                       const std::string &name,
                                       const Lowerer::ProcedureMetadata &metadata);
    static void ensureGosubStack(Lowerer &lowerer);
    static std::string nextFallbackBlockLabel(Lowerer &lowerer);
};

void Access::scheduleBlocks(ProcedureLowering &self, ProcedureLowering::LoweringContext &ctx)
{
    const auto &config = ctx.config;
    assert(config.emitEmptyBody && "Missing empty body return handler");
    assert(config.emitFinalReturn && "Missing final return handler");
    if (!config.emitEmptyBody || !config.emitFinalReturn)
        return;

    auto metadata = ctx.metadata;
    auto &lowerer = self.lowerer;
    auto &procCtx = lowerer.context();
    il::core::Function &f = ctx.builder.startFunction(ctx.name, config.retType, ctx.irParams);
    ctx.function = &f;
    procCtx.setFunction(&f);
    procCtx.setNextTemp(f.valueNames.size());

    lowerer.buildProcedureSkeleton(f, ctx.name, *metadata);

    if (!f.blocks.empty())
        procCtx.setCurrent(&f.blocks.front());

    lowerer.materializeParams(ctx.params);
    lowerer.allocateLocalSlots(ctx.paramNames, /*includeParams=*/false);
}

void Access::emitProcedureIL(ProcedureLowering &self, ProcedureLowering::LoweringContext &ctx)
{
    const auto &config = ctx.config;
    if (!config.emitEmptyBody || !config.emitFinalReturn || !ctx.function)
        return;

    auto &lowerer = self.lowerer;
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

int Access::virtualLine(Lowerer &lowerer, const Stmt &stmt)
{
    auto it = lowerer.stmtVirtualLines_.find(&stmt);
    if (it != lowerer.stmtVirtualLines_.end())
        return it->second;

    const int userLine = stmt.line;
    if (hasUserLine(userLine))
    {
        lowerer.stmtVirtualLines_[&stmt] = userLine;
        return userLine;
    }

    int synthLine = lowerer.synthLineBase_ + (lowerer.synthSeq_++);
    lowerer.stmtVirtualLines_[&stmt] = synthLine;
    return synthLine;
}

void Access::buildProcedureSkeleton(Lowerer &lowerer,
                                    il::core::Function &f,
                                    const std::string &name,
                                    const Lowerer::ProcedureMetadata &metadata)
{
    auto &ctx = lowerer.context();
    ctx.blockNames().setNamer(std::make_unique<Lowerer::BlockNamer>(name));
    Lowerer::BlockNamer *blockNamer = ctx.blockNames().namer();

    auto &entry = lowerer.builder->addBlock(
        f, blockNamer ? blockNamer->entry() : lowerer.mangler.block("entry_" + name));
    entry.params = f.params;

#ifdef DEBUG
    std::vector<int> keys;
    keys.reserve(metadata.bodyStmts.size());
#endif

    auto &lineBlocks = ctx.blockNames().lineBlocks();
    for (const auto *stmt : metadata.bodyStmts)
    {
        int vLine = Access::virtualLine(lowerer, *stmt);
        if (lineBlocks.find(vLine) != lineBlocks.end())
            continue;
        std::size_t blockIdx = f.blocks.size();
        if (blockNamer)
            lowerer.builder->addBlock(f, blockNamer->line(vLine));
        else
            lowerer.builder->addBlock(f, lowerer.mangler.block("L" + std::to_string(vLine) + "_" + name));
        lineBlocks[vLine] = blockIdx;
#ifdef DEBUG
        keys.push_back(vLine);
#endif
    }

#ifdef DEBUG
    {
        std::unordered_set<int> seen;
        for (int k : keys)
        {
            assert(seen.insert(k).second &&
                   "Duplicate block key; unlabeled statements must have unique synthetic keys");
        }
    }
#endif

    ctx.setExitIndex(f.blocks.size());
    if (blockNamer)
        lowerer.builder->addBlock(f, blockNamer->ret());
    else
        lowerer.builder->addBlock(f, lowerer.mangler.block("ret_" + name));
}

void Access::ensureGosubStack(Lowerer &lowerer)
{
    auto &ctx = lowerer.context();
    auto &state = ctx.gosub();
    if (state.hasPrologue())
        return;

    auto *func = ctx.function();
    if (!func)
        return;

    auto *savedBlock = ctx.current();
    auto *entry = &func->blocks.front();
    ctx.setCurrent(entry);

    auto savedLoc = lowerer.curLoc;
    lowerer.curLoc = {};

    il::core::Value spSlot = lowerer.emitAlloca(8);
    il::core::Value stackSlot = lowerer.emitAlloca(Lowerer::kGosubStackDepth * 4);
    lowerer.emitStore(il::core::Type(il::core::Type::Kind::I64), spSlot, il::core::Value::constInt(0));
    state.setPrologue(spSlot, stackSlot);

    lowerer.curLoc = savedLoc;
    ctx.setCurrent(savedBlock);
}

std::string Access::nextFallbackBlockLabel(Lowerer &lowerer)
{
    return lowerer.mangler.block("bb_" + std::to_string(lowerer.nextFallbackBlockId++));
}

} // namespace viper::basic::lower::control
