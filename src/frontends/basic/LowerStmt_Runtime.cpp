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
    // BUG-056: If assigning to an array field (dotted name), derive element
    // type from the owning class layout rather than symbol table.
    bool isMemberArray = target.name.find('.') != std::string::npos;
    ::il::frontends::basic::Type memberElemAstType = ::il::frontends::basic::Type::I64;
    bool isMemberObjectArray = false; // BUG-089: Track if member array holds objects
    if (isMemberArray)
    {
        const std::string &full = target.name;
        std::size_t dot = full.find('.');
        std::string baseName = full.substr(0, dot);
        std::string fieldName = full.substr(dot + 1);
        std::string klass = getSlotType(baseName).objectClass;
        if (const ClassLayout *layout = findClassLayout(klass))
        {
            if (const ClassLayout::Field *fld = layout->findField(fieldName))
            {
                memberElemAstType = fld->type;
                // BUG-089 fix: Check if field is an object array
                isMemberObjectArray = !fld->objectClassName.empty();
            }
        }
    }
    // BUG-058: Implicit field array accesses (inside methods) use non-dotted
    // names like `inventory(i)`. When in a field scope, derive element type
    // from the active class layout to choose correct string helpers.
    bool isImplicitFieldArray = (!isMemberArray) && isFieldInScope(target.name);
    // BUG-108: If a local variable or parameter with the same name exists,
    // do NOT treat this as an implicit field array. Prefer the local array.
    const auto *localSym = findSymbol(target.name);
    if (isImplicitFieldArray && !(localSym && localSym->slotId))
    {
        if (const auto *scope = activeFieldScope(); scope && scope->layout)
        {
            if (const ClassLayout::Field *fld = scope->layout->findField(target.name))
            {
                memberElemAstType = fld->type;
                // BUG-089 fix: Check if field is an object array
                isMemberObjectArray = !fld->objectClassName.empty();
            }
            // Recompute base as ME.<field> to ensure we are storing into the
            // instance field array even when the name is implicit.
            const auto *selfInfo = findSymbol("ME");
            if (selfInfo && selfInfo->slotId)
            {
                curLoc = loc;
                Value selfPtr = emitLoad(Type(Type::Kind::Ptr), Value::temp(*selfInfo->slotId));
                curLoc = loc;
                // Look up offset again for the named field
                long long offset = 0;
                if (const ClassLayout::Field *f2 = scope->layout->findField(target.name))
                    offset = static_cast<long long>(f2->offset);
                Value fieldPtr = emitBinary(
                    Opcode::GEP, Type(Type::Kind::Ptr), selfPtr, Value::constInt(offset));
                curLoc = loc;
                // Load the array handle from the field into access.base
                access.base = emitLoad(Type(Type::Kind::Ptr), fieldPtr);
            }
        }
    }

    // Prefer RHS-driven dispatch to avoid relying on fragile symbol/type
    // resolution across scopes: a string RHS must use string array helpers,
    // an object (ptr) RHS must use object array helpers; otherwise numeric.
    if (value.type.kind == Type::Kind::Str ||
        (info && info->type == AstType::Str) ||
        (isMemberArray && memberElemAstType == ::il::frontends::basic::Type::Str) ||
        (isImplicitFieldArray && memberElemAstType == ::il::frontends::basic::Type::Str))
    {
        // String array: use rt_arr_str_put (handles retain/release)
        // ABI expects a pointer to the string handle for the value operand.
        // Materialize a temporary slot, store the string handle, and pass its address.
        Value tmp = emitAlloca(8);
        emitStore(Type(Type::Kind::Str), tmp, value.value);
        emitCall("rt_arr_str_put", {access.base, access.index, tmp});
    }
    else if (value.type.kind == Type::Kind::Ptr || (!isMemberArray && info && info->isObject) || isMemberObjectArray)
    {
        // BUG-089 fix: Object array (including member object arrays): rt_arr_obj_put(arr, idx, ptr)
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
            }
        }
        // Always refresh slot typing from symbols/sema to avoid stale kinds
        // when crossing complex control flow (e.g., SELECT CASE) (BUG-076).
        SlotType slotInfo = getSlotType(var->name);
        if (slotInfo.isArray)
        {
            storeArray(storage->pointer,
                       value.value,
                       /*elementType*/ AstType::I64,
                       /*isObjectArray*/ slotInfo.isObject);
        }
        else
        {
            assignScalarSlot(slotInfo, storage->pointer, std::move(value), stmt.loc);
        }
    }
    else if (auto *mc = as<const MethodCallExpr>(*stmt.target))
    {
        // BUG-056: Support obj.arrayField(index) = value
        // Only handle simple base forms we can resolve (VarExpr or ME).
        if (mc->base)
        {
            std::string baseName;
            if (auto *v = as<const VarExpr>(*mc->base))
                baseName = v->name;
            else if (is<MeExpr>(*mc->base))
                baseName = "ME";
            if (!baseName.empty())
            {
                // Compute array handle from object field
                const auto *baseSym = findSymbol(baseName);
                if (baseSym && baseSym->slotId)
                {
                    curLoc = stmt.loc;
                    Value selfPtr = emitLoad(Type(Type::Kind::Ptr), Value::temp(*baseSym->slotId));
            std::string klass = getSlotType(baseName).objectClass;
            if (const ClassLayout *layout = findClassLayout(klass))
            {
                if (const ClassLayout::Field *fld = layout->findField(mc->method))
                {
                    curLoc = stmt.loc;
                    Value fieldPtr =
                        emitBinary(Opcode::GEP,
                                   Type(Type::Kind::Ptr),
                                   selfPtr,
                                   Value::constInt(static_cast<long long>(fld->offset)));
                    Value arrHandle = emitLoad(Type(Type::Kind::Ptr), fieldPtr);

                            // BUG-094 fix: Lower all indices and compute flattened index for multi-dimensional arrays
                            std::vector<Value> indices;
                            indices.reserve(mc->args.size());
                            for (const auto &arg : mc->args)
                            {
                                RVal idx = lowerExpr(*arg);
                                idx = coerceToI64(std::move(idx), stmt.loc);
                                indices.push_back(idx.value);
                            }

                            // Compute flattened index for multi-dimensional arrays
                            Value index = Value::constInt(0);
                            if (!indices.empty())
                            {
                                if (indices.size() == 1)
                                {
                                    index = indices[0];
                                }
                                else if (fld->isArray && !fld->arrayExtents.empty() &&
                                         fld->arrayExtents.size() == indices.size())
                                {
                                    // Multi-dimensional: compute row-major flattened index
                                    // For extents [E0, E1, ..., E_{N-1}] and indices [i0, i1, ..., i_{N-1}]:
                                    // flat = i0*L1*L2*...*L_{N-1} + i1*L2*...*L_{N-1} + ... + i_{N-1}
                                    // where Lk = (Ek + 1) are inclusive lengths per dimension.
                                    std::vector<long long> lengths;
                                    for (long long e : fld->arrayExtents)
                                        lengths.push_back(e + 1);

                                    long long stride = 1;
                                    for (size_t i = 1; i < lengths.size(); ++i)
                                        stride *= lengths[i];

                                    curLoc = stmt.loc;
                                    index = emitBinary(Opcode::IMulOvf,
                                                      Type(Type::Kind::I64),
                                                      indices[0],
                                                      Value::constInt(stride));

                                    for (size_t k = 1; k < indices.size(); ++k)
                                    {
                                        stride = 1;
                                        for (size_t i = k + 1; i < lengths.size(); ++i)
                                            stride *= lengths[i];
                                        curLoc = stmt.loc;
                                        Value term = emitBinary(Opcode::IMulOvf,
                                                               Type(Type::Kind::I64),
                                                               indices[k],
                                                               Value::constInt(stride));
                                        curLoc = stmt.loc;
                                        index = emitBinary(Opcode::IAddOvf,
                                                          Type(Type::Kind::I64),
                                                          index,
                                                          term);
                                    }
                                }
                                else
                                {
                                    // Fallback: use first index only
                                    index = indices[0];
                                }
                            }

                            // Determine element kind for helpers
                            const bool isMemberObjectArray = !fld->objectClassName.empty();

                            // Bounds check (select len helper based on element kind)
                            Value len;
                            if (fld->type == ::il::frontends::basic::Type::Str)
                            {
                                requireArrayStrLen();
                                len = emitCallRet(
                                    Type(Type::Kind::I64), "rt_arr_str_len", {arrHandle});
                            }
                            else if (isMemberObjectArray)
                            {
                                requireArrayObjLen();
                                len = emitCallRet(
                                    Type(Type::Kind::I64), "rt_arr_obj_len", {arrHandle});
                            }
                            else
                            {
                                requireArrayI32Len();
                                len = emitCallRet(
                                    Type(Type::Kind::I64), "rt_arr_i32_len", {arrHandle});
                            }
                            Value isNeg =
                                emitBinary(Opcode::SCmpLT, ilBoolTy(), index, Value::constInt(0));
                            Value tooHigh = emitBinary(Opcode::SCmpGE, ilBoolTy(), index, len);
                            auto emitc = emitCommon(stmt.loc);
                            Value isNeg64 = emitc.widen_to(isNeg, 1, 64, Signedness::Unsigned);
                            Value tooHigh64 = emitc.widen_to(tooHigh, 1, 64, Signedness::Unsigned);
                            Value oobInt = emitc.logical_or(isNeg64, tooHigh64);
                            Value oobCond =
                                emitBinary(Opcode::ICmpNe, ilBoolTy(), oobInt, Value::constInt(0));

                            ProcedureContext &ctx = context();
                            Function *func = ctx.function();
                            size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
                            unsigned bcId = ctx.consumeBoundsCheckId();
                            BlockNamer *blockNamer = ctx.blockNames().namer();
                            size_t okIdx = func->blocks.size();
                            std::string okLbl =
                                blockNamer ? blockNamer->tag("bc_ok" + std::to_string(bcId))
                                           : mangler.block("bc_ok" + std::to_string(bcId));
                            builder->addBlock(*func, okLbl);
                            size_t oobIdx = func->blocks.size();
                            std::string oobLbl =
                                blockNamer ? blockNamer->tag("bc_oob" + std::to_string(bcId))
                                           : mangler.block("bc_oob" + std::to_string(bcId));
                            builder->addBlock(*func, oobLbl);
                            BasicBlock *ok = &func->blocks[okIdx];
                            BasicBlock *oob = &func->blocks[oobIdx];
                            ctx.setCurrent(&func->blocks[curIdx]);
                            emitCBr(oobCond, oob, ok);
                            ctx.setCurrent(oob);
                            requireArrayOobPanic();
                            emitCall("rt_arr_oob_panic", {index, len});
                            emitTrap();
                            ctx.setCurrent(ok);

                            // Perform the store
                            if (fld->type == ::il::frontends::basic::Type::Str)
                            {
                                requireArrayStrPut();
                                Value tmp = emitAlloca(8);
                                emitStore(Type(Type::Kind::Str), tmp, value.value);
                                emitCall("rt_arr_str_put", {arrHandle, index, tmp});
                            }
                            else if (isMemberObjectArray)
                            {
                                requireArrayObjPut();
                                emitCall("rt_arr_obj_put", {arrHandle, index, value.value});
                            }
                            else
                            {
                                requireArrayI32Set();
                                RVal coerced = ensureI64(std::move(value), stmt.loc);
                                emitCall("rt_arr_i32_set", {arrHandle, index, coerced.value});
                            }
                            return;
                        }
                    }
                }
            }
        }
        // Fallback: not a supported lvalue form; do nothing here (analyzer should have errored).
    }
    else if (auto *call = as<const CallExpr>(*stmt.target))
    {
        // BUG-089 fix: CallExpr might be an implicit field array access (e.g., items(i) in a method)
        // Check if this is a field array in the current class
        if (isFieldInScope(call->callee))
        {
            if (const auto *scope = activeFieldScope(); scope && scope->layout)
            {
                if (const ClassLayout::Field *fld = scope->layout->findField(call->callee))
                {
                    if (fld->isArray)
                    {
                        // This is a field array access. Lower it inline similar to MethodCallExpr handling.
                        // Get the ME pointer and compute the field array handle
                        const auto *selfInfo = findSymbol("ME");
                        if (selfInfo && selfInfo->slotId)
                        {
                            curLoc = stmt.loc;
                            Value selfPtr = emitLoad(Type(Type::Kind::Ptr), Value::temp(*selfInfo->slotId));
                            curLoc = stmt.loc;
                            Value fieldPtr = emitBinary(
                                Opcode::GEP,
                                Type(Type::Kind::Ptr),
                                selfPtr,
                                Value::constInt(static_cast<long long>(fld->offset)));
                            Value arrHandle = emitLoad(Type(Type::Kind::Ptr), fieldPtr);

                            // Lower the index
                            Value index = Value::constInt(0);
                            if (!call->args.empty() && call->args[0])
                            {
                                RVal idx = lowerExpr(*call->args[0]);
                                idx = coerceToI64(std::move(idx), stmt.loc);
                                index = idx.value;
                            }

                            // Now perform bounds-checked array assignment
                            // We need to call the appropriate rt_arr_*_put function
                            bool isMemberObjectArray = !fld->objectClassName.empty();

                            // Bounds check
                            Value len;
                            if (fld->type == ::il::frontends::basic::Type::Str)
                            {
                                requireArrayStrLen();
                                len = emitCallRet(Type(Type::Kind::I64), "rt_arr_str_len", {arrHandle});
                            }
                            else if (isMemberObjectArray)
                            {
                                requireArrayObjLen();
                                len = emitCallRet(Type(Type::Kind::I64), "rt_arr_obj_len", {arrHandle});
                            }
                            else
                            {
                                requireArrayI32Len();
                                len = emitCallRet(Type(Type::Kind::I64), "rt_arr_i32_len", {arrHandle});
                            }

                            Value isNeg = emitBinary(Opcode::SCmpLT, ilBoolTy(), index, Value::constInt(0));
                            Value tooHigh = emitBinary(Opcode::SCmpGE, ilBoolTy(), index, len);
                            auto emitc = emitCommon(stmt.loc);
                            Value isNeg64 = emitc.widen_to(isNeg, 1, 64, Signedness::Unsigned);
                            Value tooHigh64 = emitc.widen_to(tooHigh, 1, 64, Signedness::Unsigned);
                            Value oobInt = emitc.logical_or(isNeg64, tooHigh64);
                            Value oobCond = emitBinary(Opcode::ICmpNe, ilBoolTy(), oobInt, Value::constInt(0));

                            ProcedureContext &ctx = context();
                            Function *func = ctx.function();
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
                            requireArrayOobPanic();
                            emitCall("rt_arr_oob_panic", {index, len});
                            emitTrap();
                            ctx.setCurrent(ok);

                            // Perform the actual assignment
                            if (fld->type == ::il::frontends::basic::Type::Str)
                            {
                                requireArrayStrPut();
                                Value tmp = emitAlloca(8);
                                emitStore(Type(Type::Kind::Str), tmp, value.value);
                                emitCall("rt_arr_str_put", {arrHandle, index, tmp});
                            }
                            else if (isMemberObjectArray)
                            {
                                requireArrayObjPut();
                                emitCall("rt_arr_obj_put", {arrHandle, index, value.value});
                            }
                            else
                            {
                                requireArrayI32Set();
                                RVal coerced = ensureI64(std::move(value), stmt.loc);
                                emitCall("rt_arr_i32_set", {arrHandle, index, coerced.value});
                            }
                            return;
                        }
                    }
                }
            }
        }
        // Not a field array; fall through (analyzer should have errored)
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
            slotInfo.isObject = !access->objectClassName.empty();  // BUG-082 fix
            if (slotInfo.isObject)
            {
                slotInfo.objectClass = access->objectClassName;
            }
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
        storeArray(storage->pointer,
                   value.value,
                   /*elementType*/ AstType::I64,
                   /*isObjectArray*/ storage->slotInfo.isObject);
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

    // Store into the resolved storage (supports module-level globals across procedures)
    if (auto storage = resolveVariableStorage(stmt.name, stmt.loc))
    {
        storeArray(
            storage->pointer, handle, info ? info->type : AstType::I64, info && info->isObject);
    }
    else
    {
        // Avoid hard assertions in production builds; emit a trap so the
        // failure is observable without terminating the entire test suite.
        emitTrap();
    }
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
    auto storage = resolveVariableStorage(stmt.name, stmt.loc);
    assert(storage && "REDIM target should have resolvable storage");
    Value current = emitLoad(Type(Type::Kind::Ptr), storage->pointer);
    if (info && info->isObject)
        requireArrayObjResize();
    else
        requireArrayI32Resize();
    Value resized =
        emitCallRet(Type(Type::Kind::Ptr),
                    (info && info->isObject) ? "rt_arr_obj_resize" : "rt_arr_i32_resize",
                    {current, length});
    storeArray(storage->pointer,
               resized,
               /*elementType*/ AstType::I64,
               /*isObjectArray*/ info && info->isObject);
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
