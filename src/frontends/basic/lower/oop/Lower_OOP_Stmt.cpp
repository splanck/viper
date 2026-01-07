//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/oop/Lower_OOP_Stmt.cpp
// Purpose: Translate high-level BASIC object-oriented statements into the IR
//          sequence consumed by the runtime helpers.  At present the file houses
//          the lowering logic for DELETE statements.
// Key invariants: DELETE evaluates operands exactly once, honours the runtime
//                 reference-count check before invoking the destructor, and
//                 ensures the free helper executes regardless of whether a
//                 destructor ran.
// Ownership/Lifetime: Operates on the Lowerer state machine, emitting IR into
//                     the caller-provided module without taking ownership of AST
//                     nodes or runtime resources.
// Links: docs/codemap.md, docs/architecture.md#basic-lowering
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Lowering routines for BASIC object-oriented statements.
/// @details Currently provides @ref Lowerer::lowerDelete, which expands the
///          DELETE statement into calls to the runtime reference-count and free
///          helpers while threading destructor invocation when a class is known.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/OopLoweringContext.hpp"

#include <string>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Lower a BASIC @c DELETE statement into runtime helper calls.
/// @details Lowering proceeds through four steps:
///          1. Evaluate the target expression once to avoid double-free bugs and
///             cache the resulting IL value.
///          2. Request the runtime helpers that will be needed (`rt_obj_release_check0`
///             and `rt_obj_free`) so the linker pulls in their definitions when
///             required.
///          3. Emit a conditional branch that inspects the reference-count check
///             result.  The lowering creates dedicated "destroy" and "continue"
///             blocks, wiring them into the surrounding procedure to keep SSA
///             form intact.
///          4. Populate the destroy block with a destructor call when the
///             object's class is known and always follow it with the `rt_obj_free`
///             helper so storage is reclaimed even when no destructor executes.
///          The continue block simply rejoins the original control flow.  All IR
///          building updates are scoped to the current @ref ProcedureContext so
///          nested lowerings can interleave safely.
/// @param stmt AST node describing the @c DELETE statement to lower.
void Lowerer::lowerDelete(const DeleteStmt &stmt)
{
    // Create a temporary OopLoweringContext for backward compatibility.
    // Callers that already have a context should use the overload below.
    OopLoweringContext oopCtx(*this, oopIndex_);
    lowerDelete(stmt, oopCtx);
}

/// @brief Lower a BASIC @c DELETE statement using an existing OopLoweringContext.
/// @details This overload uses the provided context for class lookups and destructor
///          name resolution, enabling caching across multiple OOP operations.
/// @param stmt AST node describing the @c DELETE statement to lower.
/// @param oopCtx OOP lowering context for metadata lookups and caching.
void Lowerer::lowerDelete(const DeleteStmt &stmt, OopLoweringContext &oopCtx)
{
    if (!stmt.target)
        return;

    curLoc = stmt.loc;
    RVal target = lowerExpr(*stmt.target);

    requestHelper(RuntimeFeature::ObjReleaseChk0);
    requestHelper(RuntimeFeature::ObjFree);

    Value shouldDestroy = emitCallRet(ilBoolTy(), "rt_obj_release_check0", {target.value});

    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *origin = ctx.current();
    if (!func || !origin)
        return;

    std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
    BlockNamer *blockNamer = ctx.blockNames().namer();

    std::string destroyLbl =
        blockNamer ? blockNamer->generic("delete_dtor") : mangler.block("delete_dtor");
    std::string contLbl =
        blockNamer ? blockNamer->generic("delete_cont") : mangler.block("delete_cont");

    std::size_t destroyIdx = func->blocks.size();
    builder->addBlock(*func, destroyLbl);
    std::size_t contIdx = func->blocks.size();
    builder->addBlock(*func, contLbl);

    BasicBlock *destroyBlk = &func->blocks[destroyIdx];
    BasicBlock *contBlk = &func->blocks[contIdx];

    ctx.setCurrent(&func->blocks[originIdx]);
    curLoc = stmt.loc;
    emitCBr(shouldDestroy, destroyBlk, contBlk);

    ctx.setCurrent(destroyBlk);
    curLoc = stmt.loc;

    // Use OopLoweringContext for class resolution and destructor name mangling
    std::string className = oopCtx.resolveObjectClass(*stmt.target);
    if (!className.empty())
    {
        std::string dtorName = oopCtx.getDestructorName(className);
        emitCall(dtorName, {target.value});
    }
    emitCall("rt_obj_free", {target.value});
    emitBr(contBlk);

    ctx.setCurrent(contBlk);
    curLoc = stmt.loc;
}

} // namespace il::frontends::basic
