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

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/LocationScope.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Lower the BASIC @c BEEP statement to a runtime helper call.
///
/// @details Emits a call to the bell/beep runtime function without arguments.
///          The current source location is preserved for diagnostics.
///
/// @param s AST node representing the @c BEEP statement.
void Lowerer::visit(const BeepStmt &s)
{
    LocationScope loc(*this, s.loc);
    emitRuntimeHelper(il::runtime::RuntimeFeature::TermBell, "rt_bell", Type(Type::Kind::Void), {});
}

/// @brief Lower the BASIC @c CLS statement to a runtime helper call.
///
/// @details Emits a request for the terminal-clear helper and dispatches the
///          call without arguments.  The current source location is preserved so
///          diagnostics and debug traces attribute the call correctly.
///
/// @param s AST node representing the @c CLS statement.
void Lowerer::visit(const ClsStmt &s)
{
    LocationScope loc(*this, s.loc);
    emitRuntimeHelper(
        il::runtime::RuntimeFeature::TermCls, "rt_term_cls", Type(Type::Kind::Void), {});
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
    LocationScope loc(*this, s.loc);
    auto fg = ensureI64(lowerExpr(*s.fg), s.loc);
    Value bgv = Value::constInt(-1);
    if (s.bg)
    {
        auto bg = ensureI64(lowerExpr(*s.bg), s.loc);
        bgv = bg.value;
    }
    Value fg32 = narrow32(fg.value, s.loc);
    Value bg32 = narrow32(bgv, s.loc);
    emitRuntimeHelper(il::runtime::RuntimeFeature::TermColor,
                      "rt_term_color_i32",
                      Type(Type::Kind::Void),
                      {fg32, bg32});
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
    LocationScope loc(*this, s.loc);
    auto row = ensureI64(lowerExpr(*s.row), s.loc);
    Value colv = Value::constInt(1);
    if (s.col)
    {
        auto col = ensureI64(lowerExpr(*s.col), s.loc);
        colv = col.value;
    }
    Value row32 = narrow32(row.value, s.loc);
    Value col32 = narrow32(colv, s.loc);
    emitRuntimeHelper(il::runtime::RuntimeFeature::TermLocate,
                      "rt_term_locate_i32",
                      Type(Type::Kind::Void),
                      {row32, col32});
}

/// @brief Lower the BASIC @c CURSOR statement to control cursor visibility.
///
/// @details Emits a request for the terminal-cursor helper and dispatches the
///          call with either 1 (show) or 0 (hide) based on the parsed visibility
///          flag.  The current source location is preserved for diagnostics.
///
/// @param s AST node representing the @c CURSOR statement.
void Lowerer::visit(const CursorStmt &s)
{
    LocationScope loc(*this, s.loc);
    Value show = Value::constInt(s.visible ? 1 : 0);
    Value show32 = narrow32(show, s.loc);
    emitRuntimeHelper(il::runtime::RuntimeFeature::TermCursor,
                      "rt_term_cursor_visible_i32",
                      Type(Type::Kind::Void),
                      {show32});
}

/// @brief Lower the BASIC @c ALTSCREEN statement to control alternate screen buffer.
///
/// @details Emits a request for the terminal-altscreen helper and dispatches the
///          call with either 1 (enable) or 0 (disable) based on the parsed enable
///          flag.  The current source location is preserved for diagnostics.
///
/// @param s AST node representing the @c ALTSCREEN statement.
void Lowerer::visit(const AltScreenStmt &s)
{
    LocationScope loc(*this, s.loc);
    Value enable = Value::constInt(s.enable ? 1 : 0);
    Value enable32 = narrow32(enable, s.loc);
    emitRuntimeHelper(il::runtime::RuntimeFeature::TermAltScreen,
                      "rt_term_alt_screen_i32",
                      Type(Type::Kind::Void),
                      {enable32});
}

/// @brief Lower the BASIC SLEEP statement to the runtime helper.
///
/// @details Evaluates the duration expression, coerces it to a 32-bit integer,
///          and emits a call to `rt_sleep_ms`. Negative values are clamped by
///          the runtime to zero. No runtime feature request is required.
///
/// @param s AST node describing the SLEEP statement.
void Lowerer::visit(const SleepStmt &s)
{
    LocationScope loc(*this, s.loc);
    auto ms = ensureI64(lowerExpr(*s.ms), s.loc);
    Value ms32 = narrow32(ms.value, s.loc);
    requireSleepMs();
    emitCallRet(Type(Type::Kind::Void), "rt_sleep_ms", {ms32});
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
    LocationScope location(*this, loc);
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
            emitCBr(shouldDestroy, destroyBlk, contBlk);

            ctx.setCurrent(destroyBlk);
            if (!slotInfo.objectClass.empty())
            {
                std::string dtor = mangleClassDtor(slotInfo.objectClass);
                bool haveDtor = false;
                if (mod)
                {
                    for (const auto &fn : mod->functions)
                    {
                        if (fn.name == dtor)
                        {
                            haveDtor = true;
                            break;
                        }
                    }
                }
                if (haveDtor)
                    emitCall(dtor, {oldValue});
            }
            emitCall("rt_obj_free", {oldValue});
            emitBr(contBlk);

            ctx.setCurrent(contBlk);
        }

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
    LocationScope location(*this, loc);

    ArrayAccess access = lowerArrayAccess(target, ArrayAccessKind::Store);

    // Determine array element type and use appropriate runtime function
    const auto *info = findSymbol(target.name);
    if (info && info->type == AstType::Str)
    {
        // String array: use rt_arr_str_put (handles retain/release)
        // ABI expects a pointer to the string handle for the value operand.
        // Materialize a temporary slot, store the string handle, and pass its address.
        Value tmp = emitAlloca(8);
        emitStore(Type(Type::Kind::Str), tmp, value.value);
        emitCall("rt_arr_str_put", {access.base, access.index, tmp});
    }
    else if (info && info->isObject)
    {
        // Object array: rt_arr_obj_put(arr, idx, ptr)
        requireArrayObjPut();
        emitCall("rt_arr_obj_put", {access.base, access.index, value.value});
    }
    else
    {
        // Integer/numeric array: use rt_arr_i32_set
        // Runtime ABI: rt_arr_i32_set expects its value operand as i64.
        // Always normalize the RHS to i64 (handles i1/i16/i32/f64).
        RVal coerced = ensureI64(std::move(value), loc);
        emitCall("rt_arr_i32_set", {access.base, access.index, coerced.value});
    }
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
    LocationScope loc(*this, stmt.loc);
    RVal value = lowerExpr(*stmt.expr);
    if (auto *var = as<const VarExpr>(*stmt.target))
    {
        auto storage = resolveVariableStorage(var->name, stmt.loc);
        assert(storage && "LET target should have storage");
        if (stmt.expr && !storage->isField)
        {
            std::string className;
            if (const auto *alloc = as<const NewExpr>(*stmt.expr))
            {
                className = alloc->className;
            }
            else
            {
                className = resolveObjectClass(*stmt.expr);
            }
            if (!className.empty())
            {
                setSymbolObjectType(var->name, className);
                storage->slotInfo = getSlotType(var->name);
            }
        }
        SlotType slotInfo = storage->slotInfo;
        if (slotInfo.isArray)
        {
            storeArray(storage->pointer, value.value, /*elementType*/ AstType::I64, /*isObjectArray*/ storage->slotInfo.isObject);
        }
        else
        {
            assignScalarSlot(slotInfo, storage->pointer, std::move(value), stmt.loc);
        }
    }
    else if (auto *arr = as<const ArrayExpr>(*stmt.target))
    {
        assignArrayElement(*arr, std::move(value), stmt.loc);
    }
    else if (auto *member = as<const MemberAccessExpr>(*stmt.target))
    {
        if (auto access = resolveMemberField(*member))
        {
            SlotType slotInfo;
            slotInfo.type = access->ilType;
            slotInfo.isArray = false;
            slotInfo.isBoolean = (access->astType == ::il::frontends::basic::Type::Bool);
            slotInfo.isObject = false;
            assignScalarSlot(slotInfo, access->ptr, std::move(value), stmt.loc);
        }
    }
}

/// @brief Lower a BASIC @c CONST statement.
///
/// @details Evaluates the initializer expression and stores it into the constant's
///          storage location. The lowering is similar to LET - constants are treated
///          as read-only variables at compile-time (semantic analysis prevents reassignment).
///
/// @param stmt Parsed @c CONST statement.
void Lowerer::lowerConst(const ConstStmt &stmt)
{
    LocationScope loc(*this, stmt.loc);

    // Evaluate the initializer expression
    RVal value = lowerExpr(*stmt.initializer);

    // Resolve storage for the constant (same as variable)
    auto storage = resolveVariableStorage(stmt.name, stmt.loc);
    assert(storage && "CONST target should have storage");

    // Store the value
    SlotType slotInfo = storage->slotInfo;
    if (slotInfo.isArray)
    {
        storeArray(storage->pointer, value.value, /*elementType*/ AstType::I64, /*isObjectArray*/ storage->slotInfo.isObject);
    }
    else
    {
        assignScalarSlot(slotInfo, storage->pointer, std::move(value), stmt.loc);
    }
}

/// @brief Lower BASIC @c STATIC statements declaring procedure-local persistent variables.
///
/// @details STATIC variables are allocated at module scope rather than as stack locals.
///          The actual storage allocation happens during variable collection and is
///          materialized as a module-level global. This lowering method is a no-op
///          because the declaration itself doesn't generate runtime code - only
///          uses of the variable will reference the module-level storage.
///
/// @param stmt Parsed @c STATIC statement identifying the variable name and type.
void Lowerer::lowerStatic(const StaticStmt &stmt)
{
    // No code emission needed - storage is allocated as module-level global
    // during variable collection phase, and variable references will resolve
    // to that global storage automatically.
    (void)stmt;
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
    LocationScope location(*this, loc);
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
        Value isNeg = emitBinary(Opcode::SCmpLT, ilBoolTy(), length, Value::constInt(0));
        emitCBr(isNeg, failBlk, contBlk);

        ctx.setCurrent(failBlk);
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
    LocationScope loc(*this, stmt.loc);

    // Collect dimension expressions (backward compat: check 'size' first, then 'dimensions')
    std::vector<const ExprPtr *> dimExprs;
    if (stmt.size)
    {
        dimExprs.push_back(&stmt.size);
    }
    else
    {
        for (const auto &dimExpr : stmt.dimensions)
        {
            if (dimExpr)
                dimExprs.push_back(&dimExpr);
        }
    }
    assert(!dimExprs.empty() && "DIM array must have at least one dimension");

    // For single-dimensional arrays, use the dimension directly
    Value length;
    if (dimExprs.size() == 1)
    {
        RVal bound = lowerExpr(**dimExprs[0]);
        bound = ensureI64(std::move(bound), stmt.loc);
        length = emitArrayLengthCheck(bound.value, stmt.loc, "dim_len");
    }
    else
    {
        // For multi-dimensional arrays, compute total size = product of all extents
        // Start with first dimension
        RVal bound = lowerExpr(**dimExprs[0]);
        bound = ensureI64(std::move(bound), stmt.loc);
        Value firstLen = emitArrayLengthCheck(bound.value, stmt.loc, "dim_len");

        // Multiply by remaining dimensions
        Value totalSize = firstLen;
        for (size_t i = 1; i < dimExprs.size(); ++i)
        {
            RVal dimBound = lowerExpr(**dimExprs[i]);
            dimBound = ensureI64(std::move(dimBound), stmt.loc);
            Value dimLen = emitArrayLengthCheck(dimBound.value, stmt.loc, "dim_len");
            totalSize = emitBinary(Opcode::IMulOvf, Type(Type::Kind::I64), totalSize, dimLen);
        }
        length = totalSize;
    }

    const auto *info = findSymbol(stmt.name);
    assert(info && info->slotId);

    // Determine array element type and call appropriate runtime allocator
    Value handle;
    if (info->type == AstType::Str)
    {
        // String array: use rt_arr_str_alloc
        requireArrayStrAlloc();
        handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_str_alloc", {length});
    }
    else if (info->isObject)
    {
        // Object array
        requireArrayObjNew();
        handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_obj_new", {length});
    }
    else
    {
        // Integer/numeric array: use rt_arr_i32_new
        requireArrayI32New();
        handle = emitCallRet(Type(Type::Kind::Ptr), "rt_arr_i32_new", {length});
    }

    storeArray(Value::temp(*info->slotId), handle, info->type, info->isObject);
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
    LocationScope loc(*this, stmt.loc);
    RVal bound = lowerExpr(*stmt.size);
    bound = ensureI64(std::move(bound), stmt.loc);
    Value length = emitArrayLengthCheck(bound.value, stmt.loc, "redim_len");
    const auto *info = findSymbol(stmt.name);
    assert(info && info->slotId);
    Value slot = Value::temp(*info->slotId);
    Value current = emitLoad(Type(Type::Kind::Ptr), slot);
    if (info && info->isObject)
        requireArrayObjResize();
    else
        requireArrayI32Resize();
    Value resized = emitCallRet(
        Type(Type::Kind::Ptr),
        (info && info->isObject) ? "rt_arr_obj_resize" : "rt_arr_i32_resize",
        {current, length});
    storeArray(slot, resized, /*elementType*/ AstType::I64, /*isObjectArray*/ info && info->isObject);
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
    LocationScope loc(*this, stmt.loc);
    RVal s = lowerExpr(*stmt.seed);
    Value seed = coerceToI64(std::move(s), stmt.loc).value;
    emitCall("rt_randomize_i64", {seed});
}

/// @brief Lower a @c SWAP statement to exchange two lvalue contents.
///
/// @details Emits IL instructions to: (1) load both lvalues, (2) store first
///          value into second location, (3) store second value into first location.
///          Uses a temporary slot to hold the first value during the exchange.
///
/// @param stmt Parsed @c SWAP statement.
void Lowerer::lowerSwap(const SwapStmt &stmt)
{
    LocationScope loc(*this, stmt.loc);

    // Lower both lvalues to get their RVals
    RVal lhsVal = lowerExpr(*stmt.lhs);
    RVal rhsVal = lowerExpr(*stmt.rhs);

    // Store lhs value to temp
    Value tempSlot = emitAlloca(8);
    emitStore(lhsVal.type, tempSlot, lhsVal.value);

    // Assign rhs to lhs
    if (auto *var = as<const VarExpr>(*stmt.lhs))
    {
        auto storage = resolveVariableStorage(var->name, stmt.loc);
        if (storage)
        {
            assignScalarSlot(storage->slotInfo, storage->pointer, rhsVal, stmt.loc);
        }
    }
    else if (auto *arr = as<const ArrayExpr>(*stmt.lhs))
    {
        assignArrayElement(*arr, rhsVal, stmt.loc);
    }

    // Assign temp to rhs
    Value tempVal = emitLoad(lhsVal.type, tempSlot);
    RVal tempRVal{tempVal, lhsVal.type};
    if (auto *var = as<const VarExpr>(*stmt.rhs))
    {
        auto storage = resolveVariableStorage(var->name, stmt.loc);
        if (storage)
        {
            assignScalarSlot(storage->slotInfo, storage->pointer, tempRVal, stmt.loc);
        }
    }
    else if (auto *arr = as<const ArrayExpr>(*stmt.rhs))
    {
        assignArrayElement(*arr, tempRVal, stmt.loc);
    }
}

} // namespace il::frontends::basic
