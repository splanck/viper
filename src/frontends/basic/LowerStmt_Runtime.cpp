//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements lowering routines for BASIC statements that interact with the
// runtime environment, such as CLS, COLOR, and LOCATE.  The helpers translate
// AST nodes into IL calls that request the appropriate runtime features.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Lowering helpers for runtime-oriented BASIC statements.
/// @details Each visitor updates the active @ref Lowerer state with source
///          locations, evaluates expressions, and emits IL calls that invoke the
///          runtime bridge with the proper argument marshalling.

#include "frontends/basic/Lowerer.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Lower a CLS statement into a runtime call.
/// @details Records the source location, requests the CLS runtime helper, and
///          emits a call with no operands to clear the terminal state.
/// @param s AST node describing the CLS statement.
void Lowerer::visit(const ClsStmt &s)
{
    curLoc = s.loc;
    requestHelper(il::runtime::RuntimeFeature::TermCls);
    emitCallRet(Type(Type::Kind::Void), "rt_term_cls", {});
}

/// @brief Lower a COLOR statement specifying foreground/background colours.
/// @details Evaluates optional foreground/background expressions, coerces them
///          to 32-bit integers, and emits the runtime call that updates terminal
///          colours.  Missing background operands default to -1 as per BASIC
///          semantics.
/// @param s AST node describing the COLOR statement.
void Lowerer::visit(const ColorStmt &s)
{
    curLoc = s.loc;
    auto fg = ensureI64(lowerExpr(*s.fg), s.loc);
    Value bgv = Value::constInt(-1);
    if (s.bg)
    {
        auto bg = ensureI64(lowerExpr(*s.bg), s.loc);
        bgv = bg.value;
    }
    Value fg32 = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), fg.value);
    Value bg32 = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), bgv);
    requestHelper(il::runtime::RuntimeFeature::TermColor);
    emitCallRet(Type(Type::Kind::Void), "rt_term_color_i32", {fg32, bg32});
}

/// @brief Lower a LOCATE statement positioning the cursor.
/// @details Evaluates the row expression and optional column expression,
///          requests the runtime helper, and emits the call with the coerced
///          arguments so the runtime can adjust cursor state.
/// @param s AST node describing the LOCATE statement.
void Lowerer::visit(const LocateStmt &s)
{
    curLoc = s.loc;
    auto row = ensureI64(lowerExpr(*s.row), s.loc);
    Value colv = Value::constInt(1);
    if (s.col)
    {
        auto col = ensureI64(lowerExpr(*s.col), s.loc);
        colv = col.value;
    }
    Value row32 = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), row.value);
    Value col32 = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), colv);
    requestHelper(il::runtime::RuntimeFeature::TermLocate);
    emitCallRet(Type(Type::Kind::Void), "rt_term_locate_i32", {row32, col32});
}

void Lowerer::assignScalarSlot(const SlotType &slotInfo,
                               Value slot,
                               RVal value,
                               il::support::SourceLoc loc)
{
    Type targetTy = slotInfo.type;
    bool isStr = targetTy.kind == Type::Kind::Str;
    bool isF64 = targetTy.kind == Type::Kind::F64;
    bool isBool = slotInfo.isBoolean;

    if (!isStr && !isF64 && !isBool && value.type.kind == Type::Kind::I1)
    {
        value = coerceToI64(std::move(value), loc);
    }
    if (isF64 && value.type.kind == Type::Kind::I64)
    {
        value = coerceToF64(std::move(value), loc);
    }
    else if (!isStr && !isF64 && !isBool && value.type.kind == Type::Kind::F64)
    {
        value = coerceToI64(std::move(value), loc);
    }

    if (targetTy.kind == Type::Kind::I1 && value.type.kind != Type::Kind::I1)
    {
        value = coerceToBool(std::move(value), loc);
    }

    curLoc = loc;
    if (isStr)
    {
        requireStrReleaseMaybe();
        Value oldValue = emitLoad(targetTy, slot);
        emitCall("rt_str_release_maybe", {oldValue});
        requireStrRetainMaybe();
        emitCall("rt_str_retain_maybe", {value.value});
    }

    emitStore(targetTy, slot, value.value);
}

void Lowerer::assignArrayElement(const ArrayExpr &target,
                                 RVal value,
                                 il::support::SourceLoc loc)
{
    if (value.type.kind == Type::Kind::I1)
    {
        value = coerceToI64(std::move(value), loc);
    }

    ArrayAccess access = lowerArrayAccess(target, ArrayAccessKind::Store);
    curLoc = loc;
    emitCall("rt_arr_i32_set", {access.base, access.index, value.value});
}

/// @brief Lower a runtime-oriented LET statement.
/// @details Evaluates the expression, ensures the destination slot exists, and
///          emits the appropriate store or call to runtime helpers responsible
///          for variable assignment semantics.
/// @param stmt LET statement being lowered.
void Lowerer::lowerLet(const LetStmt &stmt)
{
    RVal value = lowerExpr(*stmt.expr);
    if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
    {
        const auto *info = findSymbol(var->name);
        assert(info && info->slotId);
        SlotType slotInfo = getSlotType(var->name);
        Value slot = Value::temp(*info->slotId);
        if (slotInfo.isArray)
        {
            curLoc = stmt.loc;
            storeArray(slot, value.value);
        }
        else
        {
            assignScalarSlot(slotInfo, slot, std::move(value), stmt.loc);
        }
    }
    else if (auto *arr = dynamic_cast<const ArrayExpr *>(stmt.target.get()))
    {
        assignArrayElement(*arr, std::move(value), stmt.loc);
    }
}

/// @brief Compute and validate the runtime length for array allocations.
/// @details Adds one to the supplied bound, emits overflow and negativity checks
///          by branching to trap blocks when the length is invalid, and returns
///          the validated length to the caller.
/// @param bound     Upper bound expression already coerced to I64.
/// @param loc       Source location used for diagnostics.
/// @param labelBase Base string used when naming failure/continuation blocks.
/// @return Length value suitable for passing to runtime helpers.
Value Lowerer::emitArrayLengthCheck(Value bound,
                                    il::support::SourceLoc loc,
                                    std::string_view labelBase)
{
    curLoc = loc;
    Value length =
        emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), bound, Value::constInt(1));

    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *original = ctx.current();
    if (func && original)
    {
        size_t curIdx = static_cast<size_t>(original - &func->blocks[0]);
        BlockNamer *blockNamer = ctx.blockNames().namer();

        std::string base(labelBase);
        std::string failName = base.empty() ? "arr_len_fail" : base + "_fail";
        std::string contName = base.empty() ? "arr_len_cont" : base + "_cont";

        std::string failLbl = blockNamer ? blockNamer->generic(failName)
                                         : mangler.block(failName);
        std::string contLbl = blockNamer ? blockNamer->generic(contName)
                                         : mangler.block(contName);

        size_t failIdx = func->blocks.size();
        builder->addBlock(*func, failLbl);
        size_t contIdx = func->blocks.size();
        builder->addBlock(*func, contLbl);

        BasicBlock *failBlk = &func->blocks[failIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[curIdx]);
        curLoc = loc;
        Value isNeg = emitBinary(Opcode::SCmpLT, ilBoolTy(), length, Value::constInt(0));
        emitCBr(isNeg, failBlk, contBlk);

        ctx.setCurrent(failBlk);
        curLoc = loc;
        emitTrap();

        ctx.setCurrent(contBlk);
    }

    return length;
}

/// @brief Lower a DIM statement that allocates an array.
/// @details Evaluates the bound expression, requests the runtime allocation
///          helper, stores the resulting handle, and updates optional bounds
///          tracking metadata when enabled.
/// @param stmt DIM statement describing the new array.
void Lowerer::lowerDim(const DimStmt &stmt)
{
    RVal bound = lowerExpr(*stmt.size);
    bound = ensureI64(std::move(bound), stmt.loc);
    Value length = emitArrayLengthCheck(bound.value, stmt.loc, "dim_len");
    curLoc = stmt.loc;
    Value handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_i32_new", {length});
    const auto *info = findSymbol(stmt.name);
    assert(info && info->slotId);
    storeArray(Value::temp(*info->slotId), handle);
    if (boundsChecks)
    {
        if (info && info->arrayLengthSlot)
            emitStore(Type(Type::Kind::I64), Value::temp(*info->arrayLengthSlot), length);
    }
}

/// @brief Lower a REDIM statement that resizes an existing array.
/// @details Recomputes the requested length, invokes the runtime resize helper,
///          and updates both the array slot and any cached length metadata.
/// @param stmt REDIM statement describing the new size.
void Lowerer::lowerReDim(const ReDimStmt &stmt)
{
    RVal bound = lowerExpr(*stmt.size);
    bound = ensureI64(std::move(bound), stmt.loc);
    Value length = emitArrayLengthCheck(bound.value, stmt.loc, "redim_len");
    curLoc = stmt.loc;
    const auto *info = findSymbol(stmt.name);
    assert(info && info->slotId);
    Value slot = Value::temp(*info->slotId);
    Value current = emitLoad(Type(Type::Kind::Ptr), slot);
    Value resized = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_i32_resize", {current, length});
    storeArray(slot, resized);
    if (boundsChecks && info && info->arrayLengthSlot)
        emitStore(Type(Type::Kind::I64), Value::temp(*info->arrayLengthSlot), length);
}

/// @brief Lower a RANDOMIZE statement that seeds the runtime RNG.
/// @details Evaluates the seed expression, coerces it to a 64-bit integer, and
///          emits the runtime call that updates the RNG state while tagging
///          diagnostics with the statement location.
/// @param stmt RANDOMIZE statement carrying the seed expression.
void Lowerer::lowerRandomize(const RandomizeStmt &stmt)
{
    RVal s = lowerExpr(*stmt.seed);
    Value seed = coerceToI64(std::move(s), stmt.loc).value;
    curLoc = stmt.loc;
    emitCall("rt_randomize_i64", {seed});
}

} // namespace il::frontends::basic

