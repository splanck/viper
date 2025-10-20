//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/LowerEmit.cpp
// Purpose: Implements program-level emission orchestration for BASIC lowering.
// Key invariants: Block labels are deterministic via BlockNamer or mangler.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"

#include <cassert>
#include <unordered_set>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Materialise IL declarations for all BASIC program elements.
/// @details Traverses procedures and subs to register their signatures prior to
///          body emission.  The collected context captures top-level statements
///          so later stages can allocate locals and emit control flow in a
///          deterministic order.
/// @param prog Parsed BASIC program containing declarations and main statements.
/// @return Populated context describing the program layout.
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

/// @brief Create the main function and per-line basic blocks.
/// @details Instantiates the `main` procedure, primes the lowering context, and
///          provisions a block for each numbered BASIC line referenced in the
///          program.  This guarantees stable block indices for later lowering
///          phases and ensures the exit block exists before control edges are
///          emitted.
/// @param state Mutable context describing program emission progress.
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

/// @brief Determine all variables referenced by the main program body.
/// @details Resets symbol tracking and walks the stored statement sequence so
///          that locals can be declared before storage slots are allocated.
/// @param state Program emission context holding the main statement list.
void Lowerer::collectMainVariables(ProgramEmitContext &state)
{
    resetSymbolState();
    collectVars(state.mainStmts);
}

/// @brief Allocate storage for main-program locals and parameters.
/// @details Sets the current block to the entry block prepared earlier and
///          requests slots for all discovered locals, including synthetic ones
///          required by the runtime.  Parameter names can be supplied to avoid
///          double allocation, though the main program typically has none.
/// @param state Emission context providing the entry block pointer.
void Lowerer::allocateMainLocals(ProgramEmitContext &state)
{
    ProcedureContext &ctx = context();
    assert(state.entry && "buildMainFunctionSkeleton must run before allocateMainLocals");
    ctx.setCurrent(state.entry);
    allocateLocalSlots(std::unordered_set<std::string>(), /*includeParams=*/true);
}

/// @brief Emit the IL for the main statement list and attach the epilogue.
/// @details Lowers each stored statement into the prepared blocks, falling back
///          to emitting a constant return when the program body is empty.  Once
///          statements finish, the epilogue releases any outstanding object or
///          array references before returning zero to the runtime.
/// @param state Emission context describing the main function and entry block.
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

/// @brief Lower an entire BASIC program into IL.
/// @details Orchestrates the staged emission pipeline: declarations are
///          collected, the main function skeleton is built, locals are
///          allocated, and the body plus epilogue are emitted.  Each step
///          operates on the shared context to keep state transitions explicit.
/// @param prog Parsed BASIC program to lower.
void Lowerer::emitProgram(const Program &prog)
{
    ProgramEmitContext state = collectProgramDeclarations(prog);
    buildMainFunctionSkeleton(state);
    collectMainVariables(state);
    allocateMainLocals(state);
    emitMainBodyAndEpilogue(state);
}

} // namespace il::frontends::basic
