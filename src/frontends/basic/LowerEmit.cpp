// File: src/frontends/basic/LowerEmit.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements program-level emission orchestration for BASIC lowering.
// Key invariants: Block labels are deterministic via BlockNamer or mangler.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"

#include <cassert>
#include <unordered_set>

using namespace il::core;

namespace il::frontends::basic
{

Lowerer::ProgramEmitContext Lowerer::collectProgramDeclarations(const Program &prog)
{
    collectProcedureSignatures(prog);
    for (const auto &s : prog.procs)
    {
        if (auto *fn = dynamic_cast<const FunctionDecl *>(s.get()))
            lowerFunctionDecl(*fn);
        else if (auto *sub = dynamic_cast<const SubDecl *>(s.get()))
            lowerSubDecl(*sub);
    }

    ProgramEmitContext state;
    state.mainStmts.reserve(prog.main.size());
    for (const auto &stmt : prog.main)
        state.mainStmts.push_back(stmt.get());
    return state;
}

void Lowerer::buildMainFunctionSkeleton(ProgramEmitContext &state)
{
    build::IRBuilder &b = *builder;
    ProcedureContext &ctx = context();

    stmtVirtualLines_.clear();
    synthSeq_ = 0;
    ctx.blockNames().lineBlocks().clear();

    Function &f = b.startFunction("main", Type(Type::Kind::I64), {});
    state.function = &f;
    ctx.setFunction(&f);
    ctx.setNextTemp(f.valueNames.size());

    b.addBlock(f, "entry");

    auto &lineBlocks = ctx.blockNames().lineBlocks();
    for (const auto *stmt : state.mainStmts)
    {
        int vLine = virtualLine(*stmt);
        if (lineBlocks.find(vLine) != lineBlocks.end())
            continue;
        size_t blockIdx = f.blocks.size();
        b.addBlock(f, mangler.block("L" + std::to_string(vLine)));
        lineBlocks[vLine] = blockIdx;
    }
    ctx.setExitIndex(f.blocks.size());
    b.addBlock(f, mangler.block("exit"));

    state.entry = &f.blocks.front();
}

void Lowerer::collectMainVariables(ProgramEmitContext &state)
{
    resetSymbolState();
    collectVars(state.mainStmts);
}

void Lowerer::allocateMainLocals(ProgramEmitContext &state)
{
    ProcedureContext &ctx = context();
    assert(state.entry && "buildMainFunctionSkeleton must run before allocateMainLocals");
    ctx.setCurrent(state.entry);
    allocateLocalSlots(std::unordered_set<std::string>(), /*includeParams=*/true);
}

void Lowerer::emitMainBodyAndEpilogue(ProgramEmitContext &state)
{
    ProcedureContext &ctx = context();
    assert(state.function && "buildMainFunctionSkeleton must populate function");

    if (state.mainStmts.empty())
    {
        curLoc = {};
        emitRet(Value::constInt(0));
    }
    else
    {
        ctx.setCurrent(state.entry);
        lowerStatementSequence(state.mainStmts,
                               /*stopOnTerminated=*/false,
                               [&](const Stmt &stmt) { curLoc = stmt.loc; });
    }

    ctx.setCurrent(&state.function->blocks[ctx.exitIndex()]);
    curLoc = {};
    releaseObjectLocals(std::unordered_set<std::string>{});
    releaseArrayLocals(std::unordered_set<std::string>{});
    releaseArrayParams(std::unordered_set<std::string>{});
    curLoc = {};
    emitRet(Value::constInt(0));
}

void Lowerer::emitProgram(const Program &prog)
{
    ProgramEmitContext state = collectProgramDeclarations(prog);
    buildMainFunctionSkeleton(state);
    collectMainVariables(state);
    allocateMainLocals(state);
    emitMainBodyAndEpilogue(state);
}

} // namespace il::frontends::basic
