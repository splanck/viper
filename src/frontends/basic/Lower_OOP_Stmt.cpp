//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lower_OOP_Stmt.cpp
// Purpose: Lower BASIC OOP statements such as DELETE when OOP support is
//          enabled, emitting appropriate runtime object management calls.
// Key invariants: Generated control flow mirrors manual retain/release logic and
//                 honours runtime helper contracts.
// Ownership/Lifetime: Operates on Lowerer without taking ownership of AST or
//                     IL structures.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "support/feature_flags.hpp"

#if VIPER_ENABLE_OOP

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

using namespace il::core;

namespace il::frontends::basic
{

void Lowerer::lowerDelete(const DeleteStmt &stmt)
{
    if (!stmt.target)
        return;

    RVal value = lowerExpr(*stmt.target);
    curLoc = stmt.loc;

    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *origin = ctx.current();
    if (!func || !origin)
        return;

    BlockNamer *blockNamer = ctx.blockNames().namer();
    std::string baseLabel = "delete_obj";
    size_t checkIdx = func->blocks.size();
    std::string checkLbl = blockNamer ? blockNamer->generic(baseLabel + "_check")
                                      : mangler.block(baseLabel + "_check");
    builder->addBlock(*func, checkLbl);
    size_t contIdx = func->blocks.size();
    std::string contLbl = blockNamer ? blockNamer->generic(baseLabel + "_cont")
                                     : mangler.block(baseLabel + "_cont");
    builder->addBlock(*func, contLbl);

    BasicBlock *checkBlk = &func->blocks[checkIdx];
    BasicBlock *contBlk = &func->blocks[contIdx];

    ctx.setCurrent(origin);
    curLoc = stmt.loc;
    Value isNonNull = emitBinary(Opcode::ICmpNe, ilBoolTy(), value.value, Value::null());
    emitCBr(isNonNull, checkBlk, contBlk);

    ctx.setCurrent(checkBlk);
    curLoc = stmt.loc;
    Value releaseCount = emitCallRet(Type(Type::Kind::I32), "rt_obj_release_check0", {value.value});
    Value shouldFree = emitBinary(Opcode::ICmpNe, ilBoolTy(), releaseCount, Value::constInt(0));

    size_t freeIdx = func->blocks.size();
    std::string freeLbl = blockNamer ? blockNamer->generic(baseLabel + "_free")
                                     : mangler.block(baseLabel + "_free");
    builder->addBlock(*func, freeLbl);
    BasicBlock *freeBlk = &func->blocks[freeIdx];

    emitCBr(shouldFree, freeBlk, contBlk);

    ctx.setCurrent(freeBlk);
    curLoc = stmt.loc;
    auto className = resolveObjectClass(*stmt.target);
    if (className && !className->empty())
    {
        std::string dtorName = mangleClassDtor(*className);
        emitCall(dtorName, {value.value});
    }
    emitCall("rt_obj_free", {value.value});
    emitBr(contBlk);

    ctx.setCurrent(contBlk);
}

} // namespace il::frontends::basic

#endif // VIPER_ENABLE_OOP
