//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/LowerStmt_Runtime.cpp
// Purpose: Implements runtime-oriented BASIC statement lowering helpers such as
//          assignment, array management, and terminal control statements.
// Key invariants: Helpers reuse Lowerer's active context to manage string
//                 ownership, runtime feature requests, and diagnostics.
// Ownership/Lifetime: Operates on Lowerer without owning AST nodes or IL data.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include "frontends/basic/NameMangler_OOP.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Lower the BASIC @c CLS statement to a runtime helper call.
///
/// @details Emits a request for the terminal-clear helper and dispatches the
///          call without arguments.  The current source location is preserved so
///          diagnostics and debug traces attribute the call correctly.
///
/// @param s AST node representing the @c CLS statement.
void Lowerer::visit(const ClsStmt &s)
{
    curLoc = s.loc;
    requestHelper(il::runtime::RuntimeFeature::TermCls);
    emitCallRet(Type(Type::Kind::Void), "rt_term_cls", {});
}

/// @brief Lower the BASIC @c COLOR statement to the runtime helper.
///
/// @details Evaluates the foreground and optional background expressions,
///          narrows them to 32-bit integers, requests the terminal-colour helper,
///          and emits the call.  Missing background arguments default to -1,
///          matching runtime semantics.
///
/// @param s AST node describing the @c COLOR statement.
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
    auto emit = emitCommon(s.loc);
    Value fg32 = emit.to_iN(fg.value, 32);
    Value bg32 = emit.to_iN(bgv, 32);
    requestHelper(il::runtime::RuntimeFeature::TermColor);
    emitCallRet(Type(Type::Kind::Void), "rt_term_color_i32", {fg32, bg32});
}

/// @brief Lower the BASIC @c LOCATE statement that positions the cursor.
///
/// @details Evaluates the row and optional column expressions, coercing them to
///          32-bit integers after clamping to runtime-supported ranges.  The
///          helper request ensures the runtime terminal locator is linked into
///          the module when used.
///
/// @param s AST node describing the @c LOCATE statement.
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
    auto emit = emitCommon(s.loc);
    Value row32 = emit.to_iN(row.value, 32);
    Value col32 = emit.to_iN(colv, 32);
    requestHelper(il::runtime::RuntimeFeature::TermLocate);
    emitCallRet(Type(Type::Kind::Void), "rt_term_locate_i32", {row32, col32});
}

/// @brief Assign a value to a scalar slot with BASIC-compatible coercions.
///
/// @details Handles boolean conversion, floating/integer promotion and
///          demotion, string retain/release, and object lifetime maintenance.
///          The implementation mirrors BASIC semantics by ensuring integer
///          booleans remain 0/1 and objects trigger retain/release helpers while
///          generating deterministic clean-up paths.
///
/// @param slotInfo Metadata describing the target slot's type and traits.
/// @param slot     Value referencing the storage location.
/// @param value    Lowered right-hand side value paired with its type.
/// @param loc      Source location for diagnostics and helper calls.
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

    else if (slotInfo.isObject)
    {
        requestHelper(RuntimeFeature::ObjReleaseChk0);
        requestHelper(RuntimeFeature::ObjFree);
        requestHelper(RuntimeFeature::ObjRetainMaybe);

        Value oldValue = emitLoad(Type(Type::Kind::Ptr), slot);
        Value shouldDestroy = emitCallRet(ilBoolTy(), "rt_obj_release_check0", {oldValue});

        ProcedureContext &ctx = context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (func && origin)
        {
            std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
            BlockNamer *blockNamer = ctx.blockNames().namer();
            std::string base = "obj_assign";
            std::string destroyLbl =
                blockNamer ? blockNamer->generic(base + "_dtor") : mangler.block(base + "_dtor");
            std::string contLbl =
                blockNamer ? blockNamer->generic(base + "_cont") : mangler.block(base + "_cont");

            std::size_t destroyIdx = func->blocks.size();
            builder->addBlock(*func, destroyLbl);
            std::size_t contIdx = func->blocks.size();
            builder->addBlock(*func, contLbl);

            BasicBlock *destroyBlk = &func->blocks[destroyIdx];
            BasicBlock *contBlk = &func->blocks[contIdx];

            ctx.setCurrent(&func->blocks[originIdx]);
            curLoc = loc;
            emitCBr(shouldDestroy, destroyBlk, contBlk);

            ctx.setCurrent(destroyBlk);
            curLoc = loc;
            if (!slotInfo.objectClass.empty())
                emitCall(mangleClassDtor(slotInfo.objectClass), {oldValue});
            emitCall("rt_obj_free", {oldValue});
            emitBr(contBlk);

            ctx.setCurrent(contBlk);
        }

        curLoc = loc;
        emitCall("rt_obj_retain_maybe", {value.value});
        targetTy = Type(Type::Kind::Ptr);
    }

    emitStore(targetTy, slot, value.value);
}

/// @brief Store a value into a BASIC array element with range checks.
///
/// @details Loads the target array metadata, evaluates the index expression,
///          applies bounds checking helpers when required, and then performs the
///          store while honouring string/object lifetime rules.  The helper keeps
///          array bookkeeping (retain/release requirements) consistent across all
///          assignment sites.
///
/// @param target Array expression describing the destination element.
/// @param value  Lowered right-hand side value being assigned.
/// @param loc    Source location for diagnostics and helper invocations.
void Lowerer::assignArrayElement(const ArrayExpr &target, RVal value, il::support::SourceLoc loc)
{
    // Runtime ABI: rt_arr_i32_set expects its value operand as i64.
    // Always normalize the RHS to i64 (handles i1/i16/i32/f64).
    RVal coerced = ensureI64(std::move(value), loc);

    ArrayAccess access = lowerArrayAccess(target, ArrayAccessKind::Store);
    curLoc = loc;
    emitCall("rt_arr_i32_set", {access.base, access.index, coerced.value});
}

/// @brief Lower a BASIC @c LET statement.
///
/// @details Evaluates the right-hand expression and dispatches to the
///          appropriate assignment helper based on whether the left-hand side is
///          a scalar or array element.  The lowering cursor is updated so any
///          helper-triggered diagnostics point at the @c LET statement.
///
/// @param stmt Parsed @c LET statement.
void Lowerer::lowerLet(const LetStmt &stmt)
{
    RVal value = lowerExpr(*stmt.expr);
    if (auto *var = dynamic_cast<const VarExpr *>(stmt.target.get()))
    {
        const auto *info = findSymbol(var->name);
        assert(info && info->slotId);
        if (stmt.expr)
        {
            std::string className;
            if (const auto *alloc = dynamic_cast<const NewExpr *>(stmt.expr.get()))
            {
                className = alloc->className;
            }
            else
            {
                className = resolveObjectClass(*stmt.expr);
            }
            if (!className.empty())
                setSymbolObjectType(var->name, className);
        }
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

/// @brief Emit runtime validation logic for array length expressions.
///
/// @details Adjusts the requested bound to account for BASIC's inclusive array
///          lengths, generates overflow-aware addition, and emits a conditional
///          branch to the runtime failure path when the bound is invalid.  The
///          @p labelBase parameter keeps generated block names deterministic for
///          debugging and reproducibility.
///
/// @param bound     Value representing the user-supplied length expression.
/// @param loc       Source location used for diagnostics and helper emission.
/// @param labelBase Prefix used when naming generated failure blocks.
/// @return Validated length value produced by the runtime helper.
Value Lowerer::emitArrayLengthCheck(Value bound,
                                    il::support::SourceLoc loc,
                                    std::string_view labelBase)
{
    curLoc = loc;
    Value length = emitCommon(loc).add_checked(bound, Value::constInt(1), OverflowPolicy::Checked);

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

        std::string failLbl = blockNamer ? blockNamer->generic(failName) : mangler.block(failName);
        std::string contLbl = blockNamer ? blockNamer->generic(contName) : mangler.block(contName);

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

/// @brief Lower BASIC @c DIM declarations into runtime allocations.
///
/// @details Iterates the declared arrays, evaluates bounds with
///          @ref emitArrayLengthCheck, and emits runtime helper calls to allocate
///          the storage.  Newly allocated arrays are stored into their target
///          slots with retain bookkeeping configured so later scope exits release
///          the memory.
///
/// @param stmt Parsed @c DIM statement to lower.
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

/// @brief Lower BASIC @c REDIM statements that resize dynamic arrays.
///
/// @details Reuses @ref emitArrayLengthCheck for bounds validation, requests the
///          runtime helpers that implement preserving or non-preserving reallocation,
///          and updates the stored array handle while releasing the previous one
///          to prevent leaks.
///
/// @param stmt Parsed @c REDIM statement describing the new bounds.
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

/// @brief Lower the BASIC @c RANDOMIZE statement configuring the RNG seed.
///
/// @details Requests the runtime feature that exposes the random subsystem,
///          evaluates the optional seed expression (defaulting to zero), and
///          invokes the helper that applies the seed.
///
/// @param stmt Parsed @c RANDOMIZE statement.
void Lowerer::lowerRandomize(const RandomizeStmt &stmt)
{
    RVal s = lowerExpr(*stmt.seed);
    Value seed = coerceToI64(std::move(s), stmt.loc).value;
    curLoc = stmt.loc;
    emitCall("rt_randomize_i64", {seed});
}

} // namespace il::frontends::basic
