//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Proc_SSA.cpp
// Purpose: Provides helpers for establishing SSA-friendly procedure skeletons
//          including basic block scheduling and temporary management.
// Key invariants: Block namers remain deterministic per procedure and temporary
//                 identifiers grow monotonically.
// Ownership/Lifetime: Operates on the shared Lowerer state, mutating procedure
//                     context without taking ownership of IR nodes.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LineUtils.hpp"

#include "frontends/basic/lower/Emitter.hpp"

#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <memory>
#include <unordered_set>
#include <utility>

using namespace il::core;

namespace viper::basic::lower::ssa
{

void build(::il::frontends::basic::ProcedureLowering &lowering,
           ::il::frontends::basic::ProcedureLowering::LoweringContext &ctx)
{
    lowering.scheduleBlocks(ctx);
}

} // namespace viper::basic::lower::ssa

namespace il::frontends::basic
{

int Lowerer::virtualLine(const Stmt &s)
{
    auto it = stmtVirtualLines_.find(&s);
    if (it != stmtVirtualLines_.end())
        return it->second;

    const int userLine = s.line;
    if (hasUserLine(userLine))
    {
        stmtVirtualLines_[&s] = userLine;
        return userLine;
    }

    int synthLine = synthLineBase_ + (synthSeq_++);
    stmtVirtualLines_[&s] = synthLine;
    return synthLine;
}

void Lowerer::buildProcedureSkeleton(Function &f, const std::string &name, const ProcedureMetadata &metadata)
{
    ProcedureContext &ctx = context();
    ctx.blockNames().setNamer(std::make_unique<BlockNamer>(name));
    BlockNamer *blockNamer = ctx.blockNames().namer();

    auto &entry =
        builder->addBlock(f, blockNamer ? blockNamer->entry() : mangler.block("entry_" + name));
    entry.params = f.params;

#ifdef DEBUG
    std::vector<int> keys;
    keys.reserve(metadata.bodyStmts.size());
#endif

    auto &lineBlocks = ctx.blockNames().lineBlocks();
    for (const auto *stmt : metadata.bodyStmts)
    {
        int vLine = virtualLine(*stmt);
        if (lineBlocks.find(vLine) != lineBlocks.end())
            continue;
        size_t blockIdx = f.blocks.size();
        if (blockNamer)
            builder->addBlock(f, blockNamer->line(vLine));
        else
            builder->addBlock(f, mangler.block("L" + std::to_string(vLine) + "_" + name));
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
        builder->addBlock(f, blockNamer->ret());
    else
        builder->addBlock(f, mangler.block("ret_" + name));
}

void ProcedureLowering::scheduleBlocks(LoweringContext &ctx)
{
    const auto &config = ctx.config;
    assert(config.emitEmptyBody && "Missing empty body return handler");
    assert(config.emitFinalReturn && "Missing final return handler");
    if (!config.emitEmptyBody || !config.emitFinalReturn)
        return;

    auto metadata = ctx.metadata;
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

unsigned Lowerer::nextTempId()
{
    ProcedureContext &ctx = context();
    unsigned id = 0;
    if (builder)
    {
        id = builder->reserveTempId();
    }
    else
    {
        id = ctx.nextTemp();
        ctx.setNextTemp(id + 1);
    }
    if (Function *func = ctx.function())
    {
        if (func->valueNames.size() <= id)
            func->valueNames.resize(id + 1);
        if (func->valueNames[id].empty())
            func->valueNames[id] = "%t" + std::to_string(id);
    }
    if (ctx.nextTemp() <= id)
        ctx.setNextTemp(id + 1);
    return id;
}

} // namespace il::frontends::basic
