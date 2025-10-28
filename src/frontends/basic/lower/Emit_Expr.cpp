//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Emit_Expr.cpp
// Purpose: Provide expression emission helpers for the BASIC lowerer so common
//          IL patterns remain centralised.
// Key invariants: Helpers assume the caller manages current block state and
//                 avoid emitting terminators, leaving control transfer to
//                 statement lowering routines.
// Ownership/Lifetime: Operates on ProcedureContext owned by the active Lowerer
//                     and returns IL values tracked by the lowerer's emitter.
// Links: docs/basic-language.md, docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements expression emission helpers for the BASIC lowerer.
/// @details Functions in this unit create temporaries, allocate stack slots,
///          and build data-flow operations while respecting the lowerer's block
///          invariants. Each helper assumes the caller has positioned the
///          active block and will only append non-terminating instructions,
///          leaving terminator emission to control helpers. Temporary values
///          remain owned by the lowerer and are tracked through the shared
///          ProcedureContext.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <cassert>
#include <utility>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Fetch the canonical IL boolean type used by BASIC lowering.
/// @details Delegates to the shared @ref Emitter instance because it owns the
///          interned IL type objects. Using the emitter ensures downstream call
///          sites receive the exact handle used when materialising boolean
///          constants.
/// @return IL type representing a one-bit logical value.
Lowerer::IlType Lowerer::ilBoolTy()
{
    return emitter().ilBoolTy();
}

/// @brief Emit a boolean constant value.
/// @details Wraps the emitter helper so boolean literals produced during
///          lowering stay consistent with other constant-generation paths.
/// @param v Boolean value that should be materialised.
/// @return IL value referencing the emitted constant.
Lowerer::IlValue Lowerer::emitBoolConst(bool v)
{
    return emitter().emitBoolConst(v);
}

/// @brief Materialise a boolean result from two control-flow branches.
/// @details Builds the mini CFG required by short-circuit expressions by
///          delegating to the emitter. Callers provide lambdas that emit the
///          true and false branches, while this helper ensures the resulting
///          value is stored in a shared slot and merged at @p joinLabelBase.
/// @param emitThen Callback invoked when the true branch is active.
/// @param emitElse Callback invoked when the false branch is active.
/// @param thenLabelBase Base label used for the true branch blocks.
/// @param elseLabelBase Base label used for the false branch blocks.
/// @param joinLabelBase Base label for the join block where values converge.
/// @return Boolean IL value synthesised by the emitter helper.
Lowerer::IlValue Lowerer::emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                               const std::function<void(Value)> &emitElse,
                                               std::string_view thenLabelBase,
                                               std::string_view elseLabelBase,
                                               std::string_view joinLabelBase)
{
    return emitter().emitBoolFromBranches(
        emitThen, emitElse, thenLabelBase, elseLabelBase, joinLabelBase);
}

/// @brief Lower a BASIC array access expression.
/// @details Requests the runtime helpers needed for bounds checks, loads the
///          backing pointer for the array variable, coerces the index to 64-bit,
///          and emits the bounds check that panics on out-of-range accesses. The
///          resulting @ref ArrayAccess struct captures the address and length so
///          callers can emit load or store operations as needed.
/// @param expr Array expression AST node being lowered.
/// @param kind Indicates whether the caller intends to load from or store to the array.
/// @return Metadata describing the computed address and optional result slot.
Lowerer::ArrayAccess Lowerer::lowerArrayAccess(const ArrayExpr &expr, ArrayAccessKind kind)
{
    requireArrayI32Len();
    requireArrayOobPanic();
    if (kind == ArrayAccessKind::Load)
        requireArrayI32Get();
    else
        requireArrayI32Set();

    ProcedureContext &ctx = context();
    const auto *info = findSymbol(expr.name);
    assert(info && info->slotId);
    Value slot = Value::temp(*info->slotId);
    Value base = emitLoad(Type(Type::Kind::Ptr), slot);
    RVal idx = lowerExpr(*expr.index);
    idx = coerceToI64(std::move(idx), expr.loc);
    Value index = idx.value;
    curLoc = expr.loc;

    Value len = emitCallRet(Type(Type::Kind::I64), "rt_arr_i32_len", {base});
    Value isNeg = emitBinary(Opcode::SCmpLT, ilBoolTy(), index, Value::constInt(0));
    Value tooHigh = emitBinary(Opcode::SCmpGE, ilBoolTy(), index, len);
    auto emit = emitCommon(expr.loc);
    Value isNeg64 = emit.widen_to(isNeg, 1, 64, Signedness::Unsigned);
    Value tooHigh64 = emit.widen_to(tooHigh, 1, 64, Signedness::Unsigned);
    Value oobInt = emit.logical_or(isNeg64, tooHigh64);
    Value oobCond = emitBinary(Opcode::ICmpNe, ilBoolTy(), oobInt, Value::constInt(0));

    Function *func = ctx.function();
    assert(func && ctx.current());
    size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
    unsigned bcId = ctx.consumeBoundsCheckId();
    BlockNamer *blockNamer = ctx.blockNames().namer();
    size_t okIdx = func->blocks.size();
    std::string okLbl = blockNamer ? blockNamer->tag("bc_ok" + std::to_string(bcId))
                                   : mangler.block("bc_ok" + std::to_string(bcId));
    builder->addBlock(*func, okLbl);
    size_t oobIdx = func->blocks.size();
    std::string oobLbl = blockNamer ? blockNamer->tag("bc_oob" + std::to_string(bcId))
                                    : mangler.block("bc_oob" + std::to_string(bcId));
    builder->addBlock(*func, oobLbl);
    BasicBlock *ok = &func->blocks[okIdx];
    BasicBlock *oob = &func->blocks[oobIdx];
    ctx.setCurrent(&func->blocks[curIdx]);
    emitCBr(oobCond, oob, ok);

    ctx.setCurrent(oob);
    emitCall("rt_arr_oob_panic", {index, len});
    emitTrap();

    ctx.setCurrent(ok);
    return ArrayAccess{base, index};
}

Value Lowerer::emitAlloca(int bytes)
{
    return emitter().emitAlloca(bytes);
}

Value Lowerer::emitLoad(Type ty, Value addr)
{
    return emitter().emitLoad(ty, addr);
}

void Lowerer::emitStore(Type ty, Value addr, Value val)
{
    emitter().emitStore(ty, addr, val);
}

Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    return emitter().emitBinary(op, ty, lhs, rhs);
}

Value Lowerer::emitUnary(Opcode op, Type ty, Value val)
{
    return emitter().emitUnary(op, ty, val);
}

Value Lowerer::emitCheckedNeg(Type ty, Value val)
{
    return emitter().emitCheckedNeg(ty, val);
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    emitter().emitCall(callee, args);
}

Value Lowerer::emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args)
{
    return emitter().emitCallRet(ty, callee, args);
}

Value Lowerer::emitConstStr(const std::string &globalName)
{
    return emitter().emitConstStr(globalName);
}

std::string Lowerer::getStringLabel(const std::string &s)
{
    auto &info = ensureSymbol(s);
    if (!info.stringLabel.empty())
        return info.stringLabel;
    std::string name = ".L" + std::to_string(nextStringId++);
    builder->addGlobalStr(name, s);
    info.stringLabel = name;
    return info.stringLabel;
}

} // namespace il::frontends::basic
