//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Proc_SSA.cpp
// Purpose: Provides helpers that manage per-procedure lowering contexts,
//          metadata wiring, and temporary identifier allocation.
// Key invariants: Helpers keep the owning Lowerer context internally
//                 consistent across builder-driven stages.
// Ownership/Lifetime: Functions operate on borrowed Lowerer/ProcedureLowering
//                     instances and reuse shared buffers.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include "il/build/IRBuilder.hpp"

#include <cassert>
#include <memory>
#include <utility>

namespace viper::basic::lower::ssa
{

using il::frontends::basic::Lowerer;
using il::frontends::basic::ProcedureLowering;
using il::frontends::basic::Emit;
using il::frontends::basic::lower::Emitter;

struct Access
{
    static ProcedureLowering::LoweringContext makeContext(ProcedureLowering &self,
                                                          const std::string &name,
                                                          const std::vector<il::frontends::basic::Param> &params,
                                                          const std::vector<il::frontends::basic::StmtPtr> &body,
                                                          const Lowerer::ProcedureConfig &config);
    static void resetContext(ProcedureLowering &self, ProcedureLowering::LoweringContext &ctx);
    static void collectProcedureInfo(ProcedureLowering &self, ProcedureLowering::LoweringContext &ctx);
    static void resetLoweringState(Lowerer &lowerer);
    static Lowerer::ProcedureContext &context(Lowerer &lowerer);
    static const Lowerer::ProcedureContext &context(const Lowerer &lowerer);
    static Emit emitCommon(Lowerer &lowerer) noexcept;
    static Emit emitCommonAt(Lowerer &lowerer, il::support::SourceLoc loc) noexcept;
    static Emitter &emitter(Lowerer &lowerer) noexcept;
    static const Emitter &emitter(const Lowerer &lowerer) noexcept;
    static unsigned nextTempId(Lowerer &lowerer);
};

ProcedureLowering::LoweringContext Access::makeContext(ProcedureLowering &self,
                                                       const std::string &name,
                                                       const std::vector<il::frontends::basic::Param> &params,
                                                       const std::vector<il::frontends::basic::StmtPtr> &body,
                                                       const Lowerer::ProcedureConfig &config)
{
    assert(self.lowerer.builder && "makeContext requires an active IRBuilder");
    return ProcedureLowering::LoweringContext(
        self.lowerer, self.lowerer.symbols, *self.lowerer.builder, self.lowerer.emitter(), name, params, body, config);
}

void Access::resetContext(ProcedureLowering &self, ProcedureLowering::LoweringContext &ctx)
{
    (void)ctx;
    self.lowerer.resetLoweringState();
}

void Access::collectProcedureInfo(ProcedureLowering &self, ProcedureLowering::LoweringContext &ctx)
{
    auto metadata = std::make_shared<Lowerer::ProcedureMetadata>(
        self.lowerer.collectProcedureMetadata(ctx.params, ctx.body, ctx.config));
    ctx.metadata = metadata;
    ctx.paramCount = metadata->paramCount;
    ctx.bodyStmts = metadata->bodyStmts;
    ctx.paramNames = metadata->paramNames;
    ctx.irParams = metadata->irParams;
}

void Access::resetLoweringState(Lowerer &lowerer)
{
    lowerer.resetSymbolState();
    lowerer.context_.reset();
    lowerer.stmtVirtualLines_.clear();
    lowerer.synthSeq_ = 0;
}

Lowerer::ProcedureContext &Access::context(Lowerer &lowerer)
{
    return lowerer.context_;
}

const Lowerer::ProcedureContext &Access::context(const Lowerer &lowerer)
{
    return lowerer.context_;
}

Emit Access::emitCommon(Lowerer &lowerer) noexcept
{
    return Emit(lowerer);
}

Emit Access::emitCommonAt(Lowerer &lowerer, il::support::SourceLoc loc) noexcept
{
    Emit helper(lowerer);
    helper.at(loc);
    return helper;
}

Emitter &Access::emitter(Lowerer &lowerer) noexcept
{
    assert(lowerer.emitter_ && "emitter must be initialized");
    return *lowerer.emitter_;
}

const Emitter &Access::emitter(const Lowerer &lowerer) noexcept
{
    assert(lowerer.emitter_ && "emitter must be initialized");
    return *lowerer.emitter_;
}

unsigned Access::nextTempId(Lowerer &lowerer)
{
    auto &ctx = lowerer.context_;
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

} // namespace viper::basic::lower::ssa

namespace il::frontends::basic
{

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

ProcedureLowering::ProcedureLowering(Lowerer &lowerer) : lowerer(lowerer) {}

} // namespace il::frontends::basic
