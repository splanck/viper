// File: src/frontends/basic/LoweringPipeline.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements helper structs that orchestrate BASIC lowering stages.
// Key invariants: Helpers mutate Lowerer state in isolation per stage invocation.
// Ownership/Lifetime: Helpers borrow Lowerer references owned by callers.
// Links: docs/codemap.md

#include "frontends/basic/AstWalker.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"
#include "frontends/basic/TypeSuffix.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include <cassert>

namespace il::frontends::basic
{

namespace pipeline_detail
{
il::core::Type coreTypeForAstType(::il::frontends::basic::Type ty)
{
    using il::core::Type;
    switch (ty)
    {
        case ::il::frontends::basic::Type::I64:
            return Type(Type::Kind::I64);
        case ::il::frontends::basic::Type::F64:
            return Type(Type::Kind::F64);
        case ::il::frontends::basic::Type::Str:
            return Type(Type::Kind::Str);
        case ::il::frontends::basic::Type::Bool:
            return Type(Type::Kind::I1);
    }
    return Type(Type::Kind::I64);
}
} // namespace pipeline_detail

using pipeline_detail::coreTypeForAstType;

namespace
{

class VarCollectWalker final : public BasicAstWalker<VarCollectWalker>
{
  public:
    explicit VarCollectWalker(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    void after(const VarExpr &expr)
    {
        if (!expr.name.empty())
            lowerer_.markSymbolReferenced(expr.name);
    }

    void after(const ArrayExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    void after(const LBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    void after(const UBoundExpr &expr)
    {
        if (!expr.name.empty())
        {
            lowerer_.markSymbolReferenced(expr.name);
            lowerer_.markArray(expr.name);
        }
    }

    void before(const DimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.setSymbolType(stmt.name, stmt.type);
        lowerer_.markSymbolReferenced(stmt.name);
        if (stmt.isArray)
            lowerer_.markArray(stmt.name);
    }

    void before(const ReDimStmt &stmt)
    {
        if (stmt.name.empty())
            return;
        lowerer_.markSymbolReferenced(stmt.name);
        lowerer_.markArray(stmt.name);
    }

    void before(const ForStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    void before(const NextStmt &stmt)
    {
        if (!stmt.var.empty())
            lowerer_.markSymbolReferenced(stmt.var);
    }

    void before(const InputStmt &stmt)
    {
        for (const auto &name : stmt.vars)
        {
            if (!name.empty())
                lowerer_.markSymbolReferenced(name);
        }
    }

  private:
    Lowerer &lowerer_;
};

} // namespace

ProgramLowering::ProgramLowering(Lowerer &lowerer) : lowerer(lowerer) {}

void ProgramLowering::run(const Program &prog, il::core::Module &module)
{
    lowerer.mod = &module;
    build::IRBuilder builder(module);
    lowerer.builder = &builder;

    lowerer.mangler = NameMangler();
    auto &ctx = lowerer.context();
    ctx.reset();
    lowerer.symbols.clear();
    lowerer.nextStringId = 0;
    lowerer.procSignatures.clear();

    lowerer.runtimeTracker.reset();
    lowerer.resetManualHelpers();

    lowerer.scanProgram(prog);
    lowerer.declareRequiredRuntime(builder);
    lowerer.emitProgram(prog);

    lowerer.builder = nullptr;
    lowerer.mod = nullptr;
}

ProcedureLowering::ProcedureLowering(Lowerer &lowerer) : lowerer(lowerer) {}

void ProcedureLowering::collectProcedureSignatures(const Program &prog)
{
    lowerer.procSignatures.clear();
    for (const auto &decl : prog.procs)
    {
        if (auto *fn = dynamic_cast<const FunctionDecl *>(decl.get()))
        {
            Lowerer::ProcedureSignature sig;
            sig.retType = coreTypeForAstType(fn->ret);
            sig.paramTypes.reserve(fn->params.size());
            for (const auto &p : fn->params)
            {
                il::core::Type ty = p.is_array
                                        ? il::core::Type(il::core::Type::Kind::Ptr)
                                        : coreTypeForAstType(p.type);
                sig.paramTypes.push_back(ty);
            }
            lowerer.procSignatures.emplace(fn->name, std::move(sig));
        }
        else if (auto *sub = dynamic_cast<const SubDecl *>(decl.get()))
        {
            Lowerer::ProcedureSignature sig;
            sig.retType = il::core::Type(il::core::Type::Kind::Void);
            sig.paramTypes.reserve(sub->params.size());
            for (const auto &p : sub->params)
            {
                il::core::Type ty = p.is_array
                                        ? il::core::Type(il::core::Type::Kind::Ptr)
                                        : coreTypeForAstType(p.type);
                sig.paramTypes.push_back(ty);
            }
            lowerer.procSignatures.emplace(sub->name, std::move(sig));
        }
    }
}

void ProcedureLowering::collectVars(const std::vector<const Stmt *> &stmts)
{
    VarCollectWalker walker(lowerer);
    for (const auto *stmt : stmts)
        if (stmt)
            walker.walkStmt(*stmt);
}

void ProcedureLowering::collectVars(const Program &prog)
{
    std::vector<const Stmt *> ptrs;
    for (const auto &s : prog.procs)
        ptrs.push_back(s.get());
    for (const auto &s : prog.main)
        ptrs.push_back(s.get());
    collectVars(ptrs);
}

void ProcedureLowering::emit(const std::string &name,
                             const std::vector<Param> &params,
                             const std::vector<StmtPtr> &body,
                             const Lowerer::ProcedureConfig &config)
{
    lowerer.resetLoweringState();
    auto &ctx = lowerer.context();

    Lowerer::ProcedureMetadata metadata =
        lowerer.collectProcedureMetadata(params, body, config);

    assert(config.emitEmptyBody && "Missing empty body return handler");
    assert(config.emitFinalReturn && "Missing final return handler");
    if (!config.emitEmptyBody || !config.emitFinalReturn)
        return;

    il::core::Function &f =
        lowerer.builder->startFunction(name, config.retType, metadata.irParams);
    ctx.setFunction(&f);
    ctx.setNextTemp(f.valueNames.size());

    lowerer.buildProcedureSkeleton(f, name, metadata);

    ctx.setCurrent(&f.blocks.front());
    lowerer.materializeParams(params);
    lowerer.allocateLocalSlots(metadata.paramNames, /*includeParams=*/false);

    if (metadata.bodyStmts.empty())
    {
        lowerer.curLoc = {};
        config.emitEmptyBody();
        ctx.blockNames().resetNamer();
        return;
    }

    lowerer.lowerStatementSequence(metadata.bodyStmts, /*stopOnTerminated=*/true);

    ctx.setCurrent(&f.blocks[ctx.exitIndex()]);
    lowerer.curLoc = {};
    lowerer.releaseArrayLocals(metadata.paramNames);
    lowerer.releaseArrayParams(metadata.paramNames);
    lowerer.curLoc = {};
    config.emitFinalReturn();

    ctx.blockNames().resetNamer();
}

StatementLowering::StatementLowering(Lowerer &lowerer) : lowerer(lowerer) {}

void StatementLowering::lowerSequence(
    const std::vector<const Stmt *> &stmts,
    bool stopOnTerminated,
    const std::function<void(const Stmt &)> &beforeBranch)
{
    if (stmts.empty())
        return;

    lowerer.curLoc = {};
    auto &ctx = lowerer.context();
    auto *func = ctx.function();
    assert(func && "lowerSequence requires an active function");
    auto &lineBlocks = ctx.blockNames().lineBlocks();

    ctx.gosub().clearContinuations();
    bool hasGosub = false;
    for (size_t i = 0; i < stmts.size(); ++i)
    {
        const auto *gosubStmt = dynamic_cast<const GosubStmt *>(stmts[i]);
        if (!gosubStmt)
            continue;

        hasGosub = true;
        size_t contIdx = ctx.exitIndex();
        if (i + 1 < stmts.size())
        {
            int nextLine = lowerer.virtualLine(*stmts[i + 1]);
            contIdx = lineBlocks[nextLine];
        }
        ctx.gosub().registerContinuation(gosubStmt, contIdx);
    }

    if (hasGosub)
        lowerer.ensureGosubStack();
    int firstLine = lowerer.virtualLine(*stmts.front());
    lowerer.emitBr(&func->blocks[lineBlocks[firstLine]]);

    for (size_t i = 0; i < stmts.size(); ++i)
    {
        const Stmt &stmt = *stmts[i];
        int vLine = lowerer.virtualLine(stmt);
        ctx.setCurrent(&func->blocks[lineBlocks[vLine]]);
        lowerer.lowerStmt(stmt);
        auto *current = ctx.current();
        if (current && current->terminated)
        {
            if (stopOnTerminated)
                break;
            continue;
        }
        auto *next =
            (i + 1 < stmts.size())
                ? &func->blocks[lineBlocks[lowerer.virtualLine(*stmts[i + 1])]]
                : &func->blocks[ctx.exitIndex()];
        if (beforeBranch)
            beforeBranch(stmt);
        lowerer.emitBr(next);
    }
}

} // namespace il::frontends::basic
