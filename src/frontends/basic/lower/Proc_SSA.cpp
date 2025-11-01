//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Proc_SSA.cpp
// Purpose: Provide SSA preparation helpers for BASIC procedure lowering,
//          including metadata collection, virtual line assignment, and block
//          skeleton creation.
// Key invariants: Synthetic line numbers remain deterministic and block naming
//                 stays unique when debug assertions are enabled.
// Ownership/Lifetime: Operates on the owning Lowerer instance without taking
//                     ownership of AST nodes or IR functions.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include "frontends/basic/LoweringPipeline.hpp"
#include "frontends/basic/LineUtils.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <memory>
#include <utility>
#include <vector>

#ifdef DEBUG
#include <unordered_set>
#endif

using namespace il::core;

namespace viper::basic::lower::ssa
{

using il::frontends::basic::Lowerer;
using il::frontends::basic::Param;
using il::frontends::basic::ProcedureLowering;
using il::frontends::basic::Stmt;
using il::frontends::basic::StmtPtr;
using il::frontends::basic::hasUserLine;

struct API
{
    static ProcedureLowering::LoweringContext makeContext(ProcedureLowering &lowering,
                                                          const std::string &name,
                                                          const std::vector<Param> &params,
                                                          const std::vector<StmtPtr> &body,
                                                          const Lowerer::ProcedureConfig &config)
    {
        assert(lowering.lowerer.builder && "makeContext requires an active IRBuilder");
        return ProcedureLowering::LoweringContext(
            lowering.lowerer, lowering.lowerer.symbols, *lowering.lowerer.builder, lowering.lowerer.emitter(),
            name, params, body, config);
    }

    static void collectProcedureInfo(ProcedureLowering &lowering, ProcedureLowering::LoweringContext &ctx)
    {
        auto metadata = std::make_shared<Lowerer::ProcedureMetadata>(
            lowering.lowerer.collectProcedureMetadata(ctx.params, ctx.body, ctx.config));
        ctx.metadata = metadata;
        ctx.paramCount = metadata->paramCount;
        ctx.bodyStmts = metadata->bodyStmts;
        ctx.paramNames = metadata->paramNames;
        ctx.irParams = metadata->irParams;
    }

    static int virtualLine(Lowerer &lowerer, const Stmt &s)
    {
        auto it = lowerer.stmtVirtualLines_.find(&s);
        if (it != lowerer.stmtVirtualLines_.end())
            return it->second;

        const int userLine = s.line;
        if (hasUserLine(userLine))
        {
            lowerer.stmtVirtualLines_[&s] = userLine;
            return userLine;
        }

        int synthLine = lowerer.synthLineBase_ + (lowerer.synthSeq_++);
        lowerer.stmtVirtualLines_[&s] = synthLine;
        return synthLine;
    }

    static void buildProcedureSkeleton(Lowerer &lowerer,
                                       Function &f,
                                       const std::string &name,
                                       const Lowerer::ProcedureMetadata &metadata)
    {
        auto &ctx = lowerer.context();
        ctx.blockNames().setNamer(std::make_unique<Lowerer::BlockNamer>(name));
        auto *blockNamer = ctx.blockNames().namer();

        auto &entry =
            lowerer.builder->addBlock(f, blockNamer ? blockNamer->entry() : lowerer.mangler.block("entry_" + name));
        entry.params = f.params;

#ifdef DEBUG
        std::vector<int> keys;
        keys.reserve(metadata.bodyStmts.size());
#endif

        auto &lineBlocks = ctx.blockNames().lineBlocks();
        for (const auto *stmt : metadata.bodyStmts)
        {
            int vLine = virtualLine(lowerer, *stmt);
            if (lineBlocks.find(vLine) != lineBlocks.end())
                continue;
            size_t blockIdx = f.blocks.size();
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

    static unsigned nextTempId(Lowerer &lowerer)
    {
        auto &ctx = lowerer.context();
        unsigned id = 0;
        if (lowerer.builder)
        {
            id = lowerer.builder->reserveTempId();
        }
        else
        {
            id = ctx.nextTemp();
            ctx.setNextTemp(id + 1);
        }
        if (auto *func = ctx.function())
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

    static std::string nextFallbackBlockLabel(Lowerer &lowerer)
    {
        return lowerer.mangler.block("bb_" + std::to_string(lowerer.nextFallbackBlockId++));
    }
};

} // namespace viper::basic::lower::ssa

namespace il::frontends::basic
{

ProcedureLowering::ProcedureLowering(Lowerer &lowerer) : lowerer(lowerer) {}

ProcedureLowering::LoweringContext::LoweringContext(Lowerer &lowerer,
                                                     std::unordered_map<std::string, Lowerer::SymbolInfo> &symbols,
                                                     il::build::IRBuilder &builder,
                                                     lower::Emitter &emitter,
                                                     std::string name,
                                                     const std::vector<Param> &params,
                                                     const std::vector<StmtPtr> &body,
                                                     const Lowerer::ProcedureConfig &config)
    : lowerer(lowerer), symbols(symbols), builder(builder), emitter(emitter), name(std::move(name)), params(params),
      body(body), config(config)
{
}

ProcedureLowering::LoweringContext ProcedureLowering::makeContext(const std::string &name,
                                                                  const std::vector<Param> &params,
                                                                  const std::vector<StmtPtr> &body,
                                                                  const Lowerer::ProcedureConfig &config)
{
    return viper::basic::lower::ssa::API::makeContext(*this, name, params, body, config);
}

void ProcedureLowering::collectProcedureInfo(LoweringContext &ctx)
{
    viper::basic::lower::ssa::API::collectProcedureInfo(*this, ctx);
}

int Lowerer::virtualLine(const Stmt &s)
{
    return viper::basic::lower::ssa::API::virtualLine(*this, s);
}

void Lowerer::buildProcedureSkeleton(Function &f, const std::string &name, const ProcedureMetadata &metadata)
{
    viper::basic::lower::ssa::API::buildProcedureSkeleton(*this, f, name, metadata);
}

unsigned Lowerer::nextTempId()
{
    return viper::basic::lower::ssa::API::nextTempId(*this);
}

std::string Lowerer::nextFallbackBlockLabel()
{
    return viper::basic::lower::ssa::API::nextFallbackBlockLabel(*this);
}

} // namespace il::frontends::basic

