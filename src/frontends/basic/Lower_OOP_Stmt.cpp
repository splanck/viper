//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lower_OOP_Stmt.cpp
// Purpose: Lower BASIC OOP statements into runtime-managed object operations.
// Key invariants: DELETE evaluates operands once and performs destructor/free
//                 sequencing after reference-count checks.
// Ownership/Lifetime: Operates on Lowerer state without owning AST nodes or
//                     IL modules.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include <string>

using namespace il::core;

namespace il::frontends::basic
{

void Lowerer::lowerDelete(const DeleteStmt &stmt)
{
    if (!stmt.target)
        return;

    curLoc = stmt.loc;
    RVal target = lowerExpr(*stmt.target);

    requestHelper(RuntimeFeature::ObjReleaseChk0);
    requestHelper(RuntimeFeature::ObjFree);

    Value shouldDestroy =
        emitCallRet(ilBoolTy(), "rt_obj_release_check0", {target.value});

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
    std::string className = resolveObjectClass(*stmt.target);
    if (!className.empty())
        emitCall(mangleClassDtor(className), {target.value});
    emitCall("rt_obj_free", {target.value});
    emitBr(contBlk);

    ctx.setCurrent(contBlk);
    curLoc = stmt.loc;
}

} // namespace il::frontends::basic

