//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Emit_Expr.cpp
// Purpose: Provide the out-of-line helpers that translate BASIC expressions
//          into IL by forwarding to the shared emitter and layering BASIC
//          specific policies such as array bounds checks.
// Key invariants: Helpers must never append terminators so control-flow lowering
//                 can decide block structure, and every emitted value must be
//                 tracked through the lowerer's `ProcedureContext`.
// Ownership/Lifetime: The helpers borrow the emitter, builder, and context owned
//                     by the `Lowerer` instance.  They create IL instructions in
//                     the caller-specified blocks but do not transfer ownership
//                     of AST nodes or runtime buffers.
// Links: docs/basic-language.md#lowering
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements expression emission helpers for the BASIC lowerer.
/// @details Functions in this unit create temporaries, allocate stack slots,
///          and build data-flow operations while respecting the lowerer's block
///          invariants. Each helper assumes the caller has positioned the active
///          block and will only append non-terminating instructions, leaving
///          terminator emission to control helpers. Temporary values remain
///          owned by the lowerer and are tracked through the shared
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

/// @brief Retrieve the canonical IL boolean type used by the BASIC lowerer.
///
/// @details The BASIC lowering pipeline delegates to the shared emitter for IL
///          type construction.  This wrapper exposes the emitter's boolean type
///          while hiding the emitter dependency from clients of the @ref Lowerer
///          API.
///
/// @return IL type representing a 1-bit boolean value.
Lowerer::IlType Lowerer::ilBoolTy()
{
    return emitter().ilBoolTy();
}

/// @brief Emit a boolean constant literal.
///
/// @details The helper forwards to the emitter to ensure boolean constants
///          share the same canonical representation (usually `const_i1`).  The
///          resulting value participates in subsequent IL operations through the
///          lowering context.
///
/// @param v Boolean literal value to encode.
/// @return IL value referencing the newly emitted constant.
Lowerer::IlValue Lowerer::emitBoolConst(bool v)
{
    return emitter().emitBoolConst(v);
}

/// @brief Form a boolean by materialising separate control-flow branches.
///
/// @details Short-circuit boolean expressions in BASIC are expressed as control
///          flow.  This helper asks the emitter to synthesise the branch layout
///          and merge block before returning the final SSA value.  The caller
///          supplies lambdas that emit the then/else bodies.
///
/// @param emitThen Callback that emits instructions for the "true" path.
/// @param emitElse Callback that emits instructions for the "false" path.
/// @param thenLabelBase Base label used when naming the then block.
/// @param elseLabelBase Base label used when naming the else block.
/// @param joinLabelBase Base label used when naming the join block.
/// @return IL value produced by merging the branch results.
Lowerer::IlValue Lowerer::emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                               const std::function<void(Value)> &emitElse,
                                               std::string_view thenLabelBase,
                                               std::string_view elseLabelBase,
                                               std::string_view joinLabelBase)
{
    return emitter().emitBoolFromBranches(emitThen, emitElse, thenLabelBase, elseLabelBase, joinLabelBase);
}

/// @brief Lower a BASIC array access expression into IL operations.
///
/// @details The lowering process resolves the array symbol, requests the
///          necessary runtime intrinsics (length query, set/get, and panic
///          hooks), materialises the zero-based index, and emits a conditional
///          branch that traps when the index is negative or exceeds the array
///          length.  Successful paths receive a new block with the validated
///          index while failure paths emit the runtime panic call before
///          trapping.  The caller receives the base pointer and index so it can
///          decide whether to load or store based on @p kind.
///
/// @param expr AST node describing the array access.
/// @param kind Indicates whether the caller intends to load or store.
/// @return Structure containing the base pointer and zero-based index.
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
    Value isNeg64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), isNeg);
    Value tooHigh64 = emitUnary(Opcode::Zext1, Type(Type::Kind::I64), tooHigh);
    Value oobInt = emitBinary(Opcode::Or, Type(Type::Kind::I64), isNeg64, tooHigh64);
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

/// @brief Emit an @c alloca instruction allocating @p bytes on the VM stack.
///
/// @details The helper exists so higher level lowering code can request stack
///          space without depending on the emitter API directly.  Alignment and
///          lifetime policies remain governed by the emitter.
///
/// @param bytes Number of bytes to reserve.
/// @return Pointer value representing the allocated stack slot.
Value Lowerer::emitAlloca(int bytes)
{
    return emitter().emitAlloca(bytes);
}

/// @brief Emit a load from the given address and type.
///
/// @details Delegates to the emitter, ensuring that loads generated throughout
///          the BASIC frontend maintain consistent metadata (such as volatility
///          flags).
///
/// @param ty Element type to load.
/// @param addr Address from which the value should be read.
/// @return SSA value representing the loaded data.
Value Lowerer::emitLoad(Type ty, Value addr)
{
    return emitter().emitLoad(ty, addr);
}

/// @brief Emit a store to the specified address.
///
/// @details This wrapper allows lowering logic to stage stores without
///          importing emitter headers.  The emitter enforces type compatibility
///          and emits the actual IL instruction.
///
/// @param ty Element type of the value being written.
/// @param addr Destination address.
/// @param val Source value to store.
void Lowerer::emitStore(Type ty, Value addr, Value val)
{
    emitter().emitStore(ty, addr, val);
}

/// @brief Emit a binary arithmetic or logical instruction.
///
/// @details Parameters mirror the IL instruction encoding, allowing callers to
///          specify opcode, result type, and operands.  The emitter records the
///          operation in the current block.
///
/// @param op Opcode describing the binary operation.
/// @param ty Result type.
/// @param lhs Left-hand operand.
/// @param rhs Right-hand operand.
/// @return SSA value representing the operation result.
Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    return emitter().emitBinary(op, ty, lhs, rhs);
}

/// @brief Emit a unary instruction such as casts or negation.
///
/// @details Maintains parity with the emitter's API so callers can perform
///          conversions without reaching into the emitter object.
///
/// @param op Opcode describing the unary operation.
/// @param ty Result type.
/// @param val Operand to transform.
/// @return SSA value capturing the operation result.
Value Lowerer::emitUnary(Opcode op, Type ty, Value val)
{
    return emitter().emitUnary(op, ty, val);
}

/// @brief Emit a negation that checks for overflow on signed integers.
///
/// @details Some BASIC operations require trapping on overflow.  The helper
///          forwards to the emitter's checked negation facility which expands to
///          the appropriate IL sequence for the target type.
///
/// @param ty Result type.
/// @param val Value to negate.
/// @return SSA value representing the negated operand.
Value Lowerer::emitCheckedNeg(Type ty, Value val)
{
    return emitter().emitCheckedNeg(ty, val);
}

/// @brief Emit a call instruction that discards the callee's return value.
///
/// @details Used for procedures and runtime intrinsics returning @c void.  The
///          emitter handles argument marshalling and call-site metadata.
///
/// @param callee Symbol name of the function to invoke.
/// @param args Evaluated argument values.
void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    emitter().emitCall(callee, args);
}

/// @brief Emit a call instruction that captures the callee's return value.
///
/// @details Mirrors @ref emitCall but additionally records the SSA result in
///          the current block so callers can consume the produced value.
///
/// @param ty Declared return type of the callee.
/// @param callee Symbol name of the function to invoke.
/// @param args Evaluated argument values.
/// @return SSA value containing the call result.
Value Lowerer::emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args)
{
    return emitter().emitCallRet(ty, callee, args);
}

/// @brief Materialise a pointer to a global string literal.
///
/// @details Delegates to the emitter so that global string deduplication and
///          storage allocation remain centralised.  The returned value points to
///          a NUL-terminated buffer usable by runtime helpers.
///
/// @param globalName Symbol name of the string literal.
/// @return Pointer value referencing the emitted constant.
Value Lowerer::emitConstStr(const std::string &globalName)
{
    return emitter().emitConstStr(globalName);
}

/// @brief Retrieve or create a global string label for the provided literal.
///
/// @details BASIC string constants are pooled so repeated literals reuse a
///          single global.  The helper consults the symbol table to reuse
///          existing labels, otherwise emits a new global via the builder and
///          caches the assigned label for future queries.
///
/// @param s Raw string literal from the BASIC program.
/// @return Name of the global string constant storing @p s.
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
