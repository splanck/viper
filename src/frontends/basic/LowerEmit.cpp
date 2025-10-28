//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
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
#include <cstdlib>
#include <unordered_set>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Predeclare procedures and gather main-body statement handles.
///
/// @details The pass first registers all procedure signatures so forward calls
///          resolve correctly, then lowers each declaration stub (functions and
///          subs) to seed the IR with symbol metadata.  Finally it snapshots the
///          main statement list into the emit context for later phases.
///
/// @param prog Parsed BASIC program.
/// @return Emission context populated with declaration state and main sequence.
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

/// @brief Create the `@main` function shell and associated basic blocks.
///
/// @details Steps performed:
///          1. Reset per-procedure book-keeping (virtual lines, temporary IDs).
///          2. Start a new function returning `I64` with the canonical `entry`
///             block.
///          3. Preallocate per-line blocks so statement lowering can jump
///             deterministically.
///          4. Append a dedicated `exit` block recorded in the context for
///             epilogue emission.
///
/// @param state Mutable emission context storing block references.
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
        b.addBlock(f,
                   mangler.block((vLine > 0 ? "L" : "UL") + std::to_string(std::abs(vLine))));
        lineBlocks[vLine] = blockIdx;
    }
    ctx.setExitIndex(f.blocks.size());
    b.addBlock(f, mangler.block("exit"));

    state.entry = &f.blocks.front();
}

/// @brief Discover locals referenced by the main statement sequence.
///
/// @details Clears symbol tracking and walks the recorded main statements to
///          mark every variable, array, and object used in the outer program
///          body so storage can be allocated before emission.
///
/// @param state Emission context containing the main statement list.
void Lowerer::collectMainVariables(ProgramEmitContext &state)
{
    resetSymbolState();
    collectVars(state.mainStmts);
}

/// @brief Assign storage slots for main-function locals and parameters.
///
/// @details Ensures the skeleton builder has produced an entry block, switches
///          the procedure context to that block, and invokes the slot allocator
///          with parameter inclusion enabled so `@main` mirrors BASIC calling
///          conventions.
///
/// @param state Emission context seeded by @ref buildMainFunctionSkeleton.
void Lowerer::allocateMainLocals(ProgramEmitContext &state)
{
    ProcedureContext &ctx = context();
    assert(state.entry && "buildMainFunctionSkeleton must run before allocateMainLocals");
    ctx.setCurrent(state.entry);
    allocateLocalSlots(std::unordered_set<std::string>(), /*includeParams=*/true);
}

/// @brief Lower the main statement list and append a terminating epilogue.
///
/// @details When no statements exist the routine emits a trivial `ret 0`.  For
///          non-empty programs it iterates the cached statement pointers,
///          lowering each while updating the current source location so
///          diagnostics remain accurate.  Afterward it moves to the exit block,
///          releases temporary runtime allocations, and emits the canonical
///          return instruction.
///
/// @param state Emission context describing the main function layout.
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

        // Ensure fallthrough to the exit block is explicit. The verifier
        // requires every basic block to end with a terminator.
        if (ctx.current() && !ctx.current()->terminated)
            emitBr(&state.function->blocks[ctx.exitIndex()]);
    }

    ctx.setCurrent(&state.function->blocks[ctx.exitIndex()]);
    curLoc = {};
    releaseObjectLocals(std::unordered_set<std::string>{});
    releaseArrayLocals(std::unordered_set<std::string>{});
    releaseArrayParams(std::unordered_set<std::string>{});
    curLoc = {};
    emitRet(Value::constInt(0));
}

/// @brief Execute the full lowering pipeline for a BASIC program.
///
/// @details Invokes the program-level passes in their required order:
///          declaration collection, skeleton creation, variable discovery,
///          local allocation, and final body emission.  Each stage operates on
///          the shared @ref ProgramEmitContext assembled in this function.
///
/// @param prog BASIC program to lower into IL.
void Lowerer::emitProgram(const Program &prog)
{
    ProgramEmitContext state = collectProgramDeclarations(prog);
    buildMainFunctionSkeleton(state);
    collectMainVariables(state);
    allocateMainLocals(state);
    emitMainBodyAndEpilogue(state);
}

} // namespace il::frontends::basic
