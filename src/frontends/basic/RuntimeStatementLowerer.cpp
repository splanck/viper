//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/RuntimeStatementLowerer.cpp
// Purpose: Implementation of runtime statement lowering extracted from Lowerer.
//          Handles lowering of BASIC runtime statements (terminal control,
//          assignments, variable declarations) to IL and runtime calls.
// Key invariants: Maintains Lowerer's runtime lowering semantics exactly
// Ownership/Lifetime: Borrows Lowerer reference; coordinates with parent
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "RuntimeStatementLowerer.hpp"
#include "Lowerer.hpp"
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/LocationScope.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/ILTypeUtils.hpp"
#include "frontends/basic/sem/OverloadResolution.hpp"

#include <cassert>

using namespace il::core;
using il::runtime::RuntimeFeature;
using AstType = ::il::frontends::basic::Type;

namespace il::frontends::basic
{

RuntimeStatementLowerer::RuntimeStatementLowerer(Lowerer &lowerer) : lowerer_(lowerer) {}

/// @brief Lower the BASIC @c BEEP statement to a runtime helper call.
///
/// @details Emits a call to the bell/beep runtime function without arguments.
///          The current source location is preserved for diagnostics.
///
/// @param s AST node representing the @c BEEP statement.
void RuntimeStatementLowerer::visit(const BeepStmt &s)
{
    LocationScope loc(lowerer_, s.loc);
    lowerer_.emitRuntimeHelper(il::runtime::RuntimeFeature::TermBell, "rt_bell", il::core::Type(il::core::Type::Kind::Void), {});
}

/// @brief Lower the BASIC @c CLS statement to a runtime helper call.
///
/// @details Emits a request for the terminal-clear helper and dispatches the
///          call without arguments.  The current source location is preserved so
///          diagnostics and debug traces attribute the call correctly.
///
/// @param s AST node representing the @c CLS statement.
void RuntimeStatementLowerer::visit(const ClsStmt &s)
{
    LocationScope loc(lowerer_, s.loc);
    lowerer_.emitRuntimeHelper(
        il::runtime::RuntimeFeature::TermCls, "rt_term_cls", il::core::Type(il::core::Type::Kind::Void), {});
}

/// @brief Lower the BASIC @c COLOR statement to the runtime helper.
///
/// @details Evaluates the foreground and optional background expressions,
///          narrows them to 32-bit integers, requests the terminal-colour helper,
///          and emits the call.  Missing background arguments default to -1,
///          matching runtime semantics.
///
/// @param s AST node describing the @c COLOR statement.
void RuntimeStatementLowerer::visit(const ColorStmt &s)
{
    LocationScope loc(lowerer_, s.loc);
    auto fg = lowerer_.ensureI64(lowerer_.lowerExpr(*s.fg), s.loc);
    Value bgv = Value::constInt(-1);
    if (s.bg)
    {
        auto bg = lowerer_.ensureI64(lowerer_.lowerExpr(*s.bg), s.loc);
        bgv = bg.value;
    }
    Value fg32 = lowerer_.narrow32(fg.value, s.loc);
    Value bg32 = lowerer_.narrow32(bgv, s.loc);
    lowerer_.emitRuntimeHelper(il::runtime::RuntimeFeature::TermColor,
                      "rt_term_color_i32",
                      il::core::Type(il::core::Type::Kind::Void),
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
void RuntimeStatementLowerer::visit(const LocateStmt &s)
{
    LocationScope loc(lowerer_, s.loc);
    auto row = lowerer_.ensureI64(lowerer_.lowerExpr(*s.row), s.loc);
    Value colv = Value::constInt(1);
    if (s.col)
    {
        auto col = lowerer_.ensureI64(lowerer_.lowerExpr(*s.col), s.loc);
        colv = col.value;
    }
    Value row32 = lowerer_.narrow32(row.value, s.loc);
    Value col32 = lowerer_.narrow32(colv, s.loc);
    lowerer_.emitRuntimeHelper(il::runtime::RuntimeFeature::TermLocate,
                      "rt_term_locate_i32",
                      il::core::Type(il::core::Type::Kind::Void),
                      {row32, col32});
}

/// @brief Lower the BASIC @c CURSOR statement to control cursor visibility.
///
/// @details Emits a request for the terminal-cursor helper and dispatches the
///          call with either 1 (show) or 0 (hide) based on the parsed visibility
///          flag.  The current source location is preserved for diagnostics.
///
/// @param s AST node representing the @c CURSOR statement.
void RuntimeStatementLowerer::visit(const CursorStmt &s)
{
    LocationScope loc(lowerer_, s.loc);
    Value show = Value::constInt(s.visible ? 1 : 0);
    Value show32 = lowerer_.narrow32(show, s.loc);
    lowerer_.emitRuntimeHelper(il::runtime::RuntimeFeature::TermCursor,
                      "rt_term_cursor_visible_i32",
                      il::core::Type(il::core::Type::Kind::Void),
                      {show32});
}

/// @brief Lower the BASIC @c ALTSCREEN statement to control alternate screen buffer.
///
/// @details Emits a request for the terminal-altscreen helper and dispatches the
///          call with either 1 (enable) or 0 (disable) based on the parsed enable
///          flag.  The current source location is preserved for diagnostics.
///
/// @param s AST node representing the @c ALTSCREEN statement.
void RuntimeStatementLowerer::visit(const AltScreenStmt &s)
{
    LocationScope loc(lowerer_, s.loc);
    Value enable = Value::constInt(s.enable ? 1 : 0);
    Value enable32 = lowerer_.narrow32(enable, s.loc);
    lowerer_.emitRuntimeHelper(il::runtime::RuntimeFeature::TermAltScreen,
                      "rt_term_alt_screen_i32",
                      il::core::Type(il::core::Type::Kind::Void),
                      {enable32});
}

/// @brief Lower the BASIC SLEEP statement to the runtime helper.
///
/// @details Evaluates the duration expression, coerces it to a 32-bit integer,
///          and emits a call to `rt_sleep_ms`. Negative values are clamped by
///          the runtime to zero. No runtime feature request is required.
///
/// @param s AST node describing the SLEEP statement.
void RuntimeStatementLowerer::visit(const SleepStmt &s)
{
    LocationScope loc(lowerer_, s.loc);
    auto ms = lowerer_.ensureI64(lowerer_.lowerExpr(*s.ms), s.loc);
    Value ms32 = lowerer_.narrow32(ms.value, s.loc);
    lowerer_.requireSleepMs();
    lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::Void), "rt_sleep_ms", {ms32});
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
void RuntimeStatementLowerer::assignScalarSlot(const Lowerer::SlotType &slotInfo,
                               Lowerer::Value slot,
                               Lowerer::RVal value,
                               il::support::SourceLoc loc)
{
    LocationScope location(lowerer_, loc);
    il::core::Type targetTy = slotInfo.type;
    bool isStr = targetTy.kind == il::core::Type::Kind::Str;
    bool isF64 = targetTy.kind == il::core::Type::Kind::F64;
    bool isBool = slotInfo.isBoolean;

    if (!isStr && !isF64 && !isBool && value.type.kind == il::core::Type::Kind::I1)
    {
        value = lowerer_.coerceToI64(std::move(value), loc);
    }
    if (isF64 && value.type.kind == il::core::Type::Kind::I64)
    {
        value = lowerer_.coerceToF64(std::move(value), loc);
    }
    else if (!isStr && !isF64 && !isBool && value.type.kind == il::core::Type::Kind::F64)
    {
        value = lowerer_.coerceToI64(std::move(value), loc);
    }

    if (targetTy.kind == il::core::Type::Kind::I1 && value.type.kind != il::core::Type::Kind::I1)
    {
        value = lowerer_.coerceToBool(std::move(value), loc);
    }

    if (isStr)
    {
        lowerer_.requireStrReleaseMaybe();
        Value oldValue = lowerer_.emitLoad(targetTy, slot);
        lowerer_.emitCall("rt_str_release_maybe", {oldValue});
        lowerer_.requireStrRetainMaybe();
        lowerer_.emitCall("rt_str_retain_maybe", {value.value});
    }

    else if (slotInfo.isObject)
    {
        lowerer_.requestHelper(RuntimeFeature::ObjReleaseChk0);
        lowerer_.requestHelper(RuntimeFeature::ObjFree);
        lowerer_.requestHelper(RuntimeFeature::ObjRetainMaybe);

        Value oldValue = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr), slot);
        Value shouldDestroy = lowerer_.emitCallRet(lowerer_.ilBoolTy(), "rt_obj_release_check0", {oldValue});

        ProcedureContext &ctx = lowerer_.context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (func && origin)
        {
            std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
            BlockNamer *blockNamer = ctx.blockNames().namer();
            std::string base = "obj_assign";
            std::string destroyLbl =
                blockNamer ? blockNamer->generic(base + "_dtor") : lowerer_.mangler.block(base + "_dtor");
            std::string contLbl =
                blockNamer ? blockNamer->generic(base + "_cont") : lowerer_.mangler.block(base + "_cont");

            std::size_t destroyIdx = func->blocks.size();
            lowerer_.builder->addBlock(*func, destroyLbl);
            std::size_t contIdx = func->blocks.size();
            lowerer_.builder->addBlock(*func, contLbl);

            BasicBlock *destroyBlk = &func->blocks[destroyIdx];
            BasicBlock *contBlk = &func->blocks[contIdx];

            ctx.setCurrent(&func->blocks[originIdx]);
            lowerer_.emitCBr(shouldDestroy, destroyBlk, contBlk);

            ctx.setCurrent(destroyBlk);
            if (!slotInfo.objectClass.empty())
            {
                std::string dtor = mangleClassDtor(slotInfo.objectClass);
                bool haveDtor = false;
                if (lowerer_.mod)
                {
                    for (const auto &fn : lowerer_.mod->functions)
                    {
                        if (fn.name == dtor)
                        {
                            haveDtor = true;
                            break;
                        }
                    }
                }
                if (haveDtor)
                    lowerer_.emitCall(dtor, {oldValue});
            }
            lowerer_.emitCall("rt_obj_free", {oldValue});
            lowerer_.emitBr(contBlk);

            ctx.setCurrent(contBlk);
        }

        lowerer_.emitCall("rt_obj_retain_maybe", {value.value});
        targetTy = il::core::Type(il::core::Type::Kind::Ptr);
    }

    lowerer_.emitStore(targetTy, slot, value.value);
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
void RuntimeStatementLowerer::assignArrayElement(const ArrayExpr &target, Lowerer::RVal value, il::support::SourceLoc loc)
{
    LocationScope location(lowerer_, loc);

    Lowerer::ArrayAccess access = lowerer_.lowerArrayAccess(target, Lowerer::ArrayAccessKind::Store);

    // Determine array element type and use appropriate runtime function
    const auto *info = lowerer_.findSymbol(target.name);
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
        std::string klass = lowerer_.getSlotType(baseName).objectClass;
        if (const Lowerer::ClassLayout *layout = lowerer_.findClassLayout(klass))
        {
            if (const Lowerer::ClassLayout::Field *fld = layout->findField(fieldName))
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
    bool isImplicitFieldArray = (!isMemberArray) && lowerer_.isFieldInScope(target.name);
    // BUG-108: If a local variable or parameter with the same name exists,
    // do NOT treat this as an implicit field array. Prefer the local array.
    const auto *localSym = lowerer_.findSymbol(target.name);
    if (isImplicitFieldArray && !(localSym && localSym->slotId))
    {
        if (const auto *scope = lowerer_.activeFieldScope(); scope && scope->layout)
        {
            if (const Lowerer::ClassLayout::Field *fld = scope->layout->findField(target.name))
            {
                memberElemAstType = fld->type;
                // BUG-089 fix: Check if field is an object array
                isMemberObjectArray = !fld->objectClassName.empty();
            }
            // Recompute base as ME.<field> to ensure we are storing into the
            // instance field array even when the name is implicit.
            const auto *selfInfo = lowerer_.findSymbol("ME");
            if (selfInfo && selfInfo->slotId)
            {
                lowerer_.curLoc = loc;
                Value selfPtr = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr), Value::temp(*selfInfo->slotId));
                lowerer_.curLoc = loc;
                // Look up offset again for the named field
                long long offset = 0;
                if (const Lowerer::ClassLayout::Field *f2 = scope->layout->findField(target.name))
                    offset = static_cast<long long>(f2->offset);
                Value fieldPtr = lowerer_.emitBinary(
                    Opcode::GEP, il::core::Type(il::core::Type::Kind::Ptr), selfPtr, Value::constInt(offset));
                lowerer_.curLoc = loc;
                // Load the array handle from the field into access.base
                access.base = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr), fieldPtr);
            }
        }
    }

    // Prefer RHS-driven dispatch to avoid relying on fragile symbol/type
    // resolution across scopes: a string RHS must use string array helpers,
    // an object (ptr) RHS must use object array helpers; otherwise numeric.
    if (value.type.kind == il::core::Type::Kind::Str ||
        (info && info->type == AstType::Str) ||
        (isMemberArray && memberElemAstType == ::il::frontends::basic::Type::Str) ||
        (isImplicitFieldArray && memberElemAstType == ::il::frontends::basic::Type::Str))
    {
        // String array: use rt_arr_str_put (handles retain/release)
        // ABI expects a pointer to the string handle for the value operand.
        // Materialize a temporary slot, store the string handle, and pass its address.
        Value tmp = lowerer_.emitAlloca(8);
        lowerer_.emitStore(il::core::Type(il::core::Type::Kind::Str), tmp, value.value);
        lowerer_.emitCall("rt_arr_str_put", {access.base, access.index, tmp});
    }
    else if (value.type.kind == il::core::Type::Kind::Ptr || (!isMemberArray && info && info->isObject) || isMemberObjectArray)
    {
        // BUG-089 fix: Object array (including member object arrays): rt_arr_obj_put(arr, idx, ptr)
        lowerer_.requireArrayObjPut();
        lowerer_.emitCall("rt_arr_obj_put", {access.base, access.index, value.value});
    }
    else
    {
        // Integer/numeric array: use rt_arr_i32_set
        // Runtime ABI: rt_arr_i32_set expects its value operand as i64.
        // Always normalize the RHS to i64 (handles i1/i16/i32/f64).
        Lowerer::RVal coerced = lowerer_.ensureI64(std::move(value), loc);
        lowerer_.emitCall("rt_arr_i32_set", {access.base, access.index, coerced.value});
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
void RuntimeStatementLowerer::lowerLet(const LetStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);
    Lowerer::RVal value = lowerer_.lowerExpr(*stmt.expr);
    if (auto *var = as<const VarExpr>(*stmt.target))
    {
        auto storage = lowerer_.resolveVariableStorage(var->name, stmt.loc);
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
                className = lowerer_.resolveObjectClass(*stmt.expr);
            }
            if (!className.empty())
            {
                lowerer_.setSymbolObjectType(var->name, className);
            }
        }
        // Always refresh slot typing from symbols/sema to avoid stale kinds
        // when crossing complex control flow (e.g., SELECT CASE) (BUG-076).
        Lowerer::SlotType slotInfo = lowerer_.getSlotType(var->name);
        if (slotInfo.isArray)
        {
            lowerer_.storeArray(storage->pointer,
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
                const auto *baseSym = lowerer_.findSymbol(baseName);
                if (baseSym && baseSym->slotId)
                {
                    lowerer_.curLoc = stmt.loc;
                    Value selfPtr = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr), Value::temp(*baseSym->slotId));
            std::string klass = lowerer_.getSlotType(baseName).objectClass;
            if (const Lowerer::ClassLayout *layout = lowerer_.findClassLayout(klass))
            {
                if (const Lowerer::ClassLayout::Field *fld = layout->findField(mc->method))
                {
                    lowerer_.curLoc = stmt.loc;
                    Value fieldPtr =
                        lowerer_.emitBinary(Opcode::GEP,
                                   il::core::Type(il::core::Type::Kind::Ptr),
                                   selfPtr,
                                   Value::constInt(static_cast<long long>(fld->offset)));
                    Value arrHandle = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr), fieldPtr);

                            // BUG-094 fix: Lower all indices and compute flattened index for multi-dimensional arrays
                            std::vector<Value> indices;
                            indices.reserve(mc->args.size());
                            for (const auto &arg : mc->args)
                            {
                                Lowerer::RVal idx = lowerer_.lowerExpr(*arg);
                                idx = lowerer_.coerceToI64(std::move(idx), stmt.loc);
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

                                    lowerer_.curLoc = stmt.loc;
                                    index = lowerer_.emitBinary(Opcode::IMulOvf,
                                                      il::core::Type(il::core::Type::Kind::I64),
                                                      indices[0],
                                                      Value::constInt(stride));

                                    for (size_t k = 1; k < indices.size(); ++k)
                                    {
                                        stride = 1;
                                        for (size_t i = k + 1; i < lengths.size(); ++i)
                                            stride *= lengths[i];
                                        lowerer_.curLoc = stmt.loc;
                                        Value term = lowerer_.emitBinary(Opcode::IMulOvf,
                                                               il::core::Type(il::core::Type::Kind::I64),
                                                               indices[k],
                                                               Value::constInt(stride));
                                        lowerer_.curLoc = stmt.loc;
                                        index = lowerer_.emitBinary(Opcode::IAddOvf,
                                                          il::core::Type(il::core::Type::Kind::I64),
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
                                lowerer_.requireArrayStrLen();
                                len = lowerer_.emitCallRet(
                                    il::core::Type(il::core::Type::Kind::I64), "rt_arr_str_len", {arrHandle});
                            }
                            else if (isMemberObjectArray)
                            {
                                lowerer_.requireArrayObjLen();
                                len = lowerer_.emitCallRet(
                                    il::core::Type(il::core::Type::Kind::I64), "rt_arr_obj_len", {arrHandle});
                            }
                            else
                            {
                                lowerer_.requireArrayI32Len();
                                len = lowerer_.emitCallRet(
                                    il::core::Type(il::core::Type::Kind::I64), "rt_arr_i32_len", {arrHandle});
                            }
                            Value isNeg =
                                lowerer_.emitBinary(Opcode::SCmpLT, lowerer_.ilBoolTy(), index, Value::constInt(0));
                            Value tooHigh = lowerer_.emitBinary(Opcode::SCmpGE, lowerer_.ilBoolTy(), index, len);
                            auto emitc = lowerer_.emitCommon(stmt.loc);
                            Value isNeg64 = emitc.widen_to(isNeg, 1, 64, Signedness::Unsigned);
                            Value tooHigh64 = emitc.widen_to(tooHigh, 1, 64, Signedness::Unsigned);
                            Value oobInt = emitc.logical_or(isNeg64, tooHigh64);
                            Value oobCond =
                                lowerer_.emitBinary(Opcode::ICmpNe, lowerer_.ilBoolTy(), oobInt, Value::constInt(0));

                            ProcedureContext &ctx = lowerer_.context();
                            Function *func = ctx.function();
                            size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
                            unsigned bcId = ctx.consumeBoundsCheckId();
                            BlockNamer *blockNamer = ctx.blockNames().namer();
                            size_t okIdx = func->blocks.size();
                            std::string okLbl =
                                blockNamer ? blockNamer->tag("bc_ok" + std::to_string(bcId))
                                           : lowerer_.mangler.block("bc_ok" + std::to_string(bcId));
                            lowerer_.builder->addBlock(*func, okLbl);
                            size_t oobIdx = func->blocks.size();
                            std::string oobLbl =
                                blockNamer ? blockNamer->tag("bc_oob" + std::to_string(bcId))
                                           : lowerer_.mangler.block("bc_oob" + std::to_string(bcId));
                            lowerer_.builder->addBlock(*func, oobLbl);
                            BasicBlock *ok = &func->blocks[okIdx];
                            BasicBlock *oob = &func->blocks[oobIdx];
                            ctx.setCurrent(&func->blocks[curIdx]);
                            lowerer_.emitCBr(oobCond, oob, ok);
                            ctx.setCurrent(oob);
                            lowerer_.requireArrayOobPanic();
                            lowerer_.emitCall("rt_arr_oob_panic", {index, len});
                            lowerer_.emitTrap();
                            ctx.setCurrent(ok);

                            // Perform the store
                            if (fld->type == ::il::frontends::basic::Type::Str)
                            {
                                lowerer_.requireArrayStrPut();
                                Value tmp = lowerer_.emitAlloca(8);
                                lowerer_.emitStore(il::core::Type(il::core::Type::Kind::Str), tmp, value.value);
                                lowerer_.emitCall("rt_arr_str_put", {arrHandle, index, tmp});
                            }
                            else if (isMemberObjectArray)
                            {
                                lowerer_.requireArrayObjPut();
                                lowerer_.emitCall("rt_arr_obj_put", {arrHandle, index, value.value});
                            }
                            else
                            {
                                lowerer_.requireArrayI32Set();
                                Lowerer::RVal coerced = lowerer_.ensureI64(std::move(value), stmt.loc);
                                lowerer_.emitCall("rt_arr_i32_set", {arrHandle, index, coerced.value});
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
        if (lowerer_.isFieldInScope(call->callee))
        {
            if (const auto *scope = lowerer_.activeFieldScope(); scope && scope->layout)
            {
                if (const Lowerer::ClassLayout::Field *fld = scope->layout->findField(call->callee))
                {
                    if (fld->isArray)
                    {
                        // This is a field array access. Lower it inline similar to MethodCallExpr handling.
                        // Get the ME pointer and compute the field array handle
                        const auto *selfInfo = lowerer_.findSymbol("ME");
                        if (selfInfo && selfInfo->slotId)
                        {
                            lowerer_.curLoc = stmt.loc;
                            Value selfPtr = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr), Value::temp(*selfInfo->slotId));
                            lowerer_.curLoc = stmt.loc;
                            Value fieldPtr = lowerer_.emitBinary(
                                Opcode::GEP,
                                il::core::Type(il::core::Type::Kind::Ptr),
                                selfPtr,
                                Value::constInt(static_cast<long long>(fld->offset)));
                            Value arrHandle = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr), fieldPtr);

                            // Lower the index
                            Value index = Value::constInt(0);
                            if (!call->args.empty() && call->args[0])
                            {
                                Lowerer::RVal idx = lowerer_.lowerExpr(*call->args[0]);
                                idx = lowerer_.coerceToI64(std::move(idx), stmt.loc);
                                index = idx.value;
                            }

                            // Now perform bounds-checked array assignment
                            // We need to call the appropriate rt_arr_*_put function
                            bool isMemberObjectArray = !fld->objectClassName.empty();

                            // Bounds check
                            Value len;
                            if (fld->type == ::il::frontends::basic::Type::Str)
                            {
                                lowerer_.requireArrayStrLen();
                                len = lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::I64), "rt_arr_str_len", {arrHandle});
                            }
                            else if (isMemberObjectArray)
                            {
                                lowerer_.requireArrayObjLen();
                                len = lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::I64), "rt_arr_obj_len", {arrHandle});
                            }
                            else
                            {
                                lowerer_.requireArrayI32Len();
                                len = lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::I64), "rt_arr_i32_len", {arrHandle});
                            }

                            Value isNeg = lowerer_.emitBinary(Opcode::SCmpLT, lowerer_.ilBoolTy(), index, Value::constInt(0));
                            Value tooHigh = lowerer_.emitBinary(Opcode::SCmpGE, lowerer_.ilBoolTy(), index, len);
                            auto emitc = lowerer_.emitCommon(stmt.loc);
                            Value isNeg64 = emitc.widen_to(isNeg, 1, 64, Signedness::Unsigned);
                            Value tooHigh64 = emitc.widen_to(tooHigh, 1, 64, Signedness::Unsigned);
                            Value oobInt = emitc.logical_or(isNeg64, tooHigh64);
                            Value oobCond = lowerer_.emitBinary(Opcode::ICmpNe, lowerer_.ilBoolTy(), oobInt, Value::constInt(0));

                            ProcedureContext &ctx = lowerer_.context();
                            Function *func = ctx.function();
                            size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
                            unsigned bcId = ctx.consumeBoundsCheckId();
                            BlockNamer *blockNamer = ctx.blockNames().namer();
                            size_t okIdx = func->blocks.size();
                            std::string okLbl = blockNamer ? blockNamer->tag("bc_ok" + std::to_string(bcId))
                                                           : lowerer_.mangler.block("bc_ok" + std::to_string(bcId));
                            lowerer_.builder->addBlock(*func, okLbl);
                            size_t oobIdx = func->blocks.size();
                            std::string oobLbl = blockNamer ? blockNamer->tag("bc_oob" + std::to_string(bcId))
                                                            : lowerer_.mangler.block("bc_oob" + std::to_string(bcId));
                            lowerer_.builder->addBlock(*func, oobLbl);
                            BasicBlock *ok = &func->blocks[okIdx];
                            BasicBlock *oob = &func->blocks[oobIdx];
                            ctx.setCurrent(&func->blocks[curIdx]);
                            lowerer_.emitCBr(oobCond, oob, ok);
                            ctx.setCurrent(oob);
                            lowerer_.requireArrayOobPanic();
                            lowerer_.emitCall("rt_arr_oob_panic", {index, len});
                            lowerer_.emitTrap();
                            ctx.setCurrent(ok);

                            // Perform the actual assignment
                            if (fld->type == ::il::frontends::basic::Type::Str)
                            {
                                lowerer_.requireArrayStrPut();
                                Value tmp = lowerer_.emitAlloca(8);
                                lowerer_.emitStore(il::core::Type(il::core::Type::Kind::Str), tmp, value.value);
                                lowerer_.emitCall("rt_arr_str_put", {arrHandle, index, tmp});
                            }
                            else if (isMemberObjectArray)
                            {
                                lowerer_.requireArrayObjPut();
                                lowerer_.emitCall("rt_arr_obj_put", {arrHandle, index, value.value});
                            }
                            else
                            {
                                lowerer_.requireArrayI32Set();
                                Lowerer::RVal coerced = lowerer_.ensureI64(std::move(value), stmt.loc);
                                lowerer_.emitCall("rt_arr_i32_set", {arrHandle, index, coerced.value});
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
        if (auto access = lowerer_.resolveMemberField(*member))
        {
            Lowerer::SlotType slotInfo;
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
        else
        {
            // Property setter sugar (instance): base.member = value -> call set_member(base, value)
            // Property setter sugar (static):   Class.member = value -> call Class.set_member(value)
            // Static field assignment:          Class.field  = value -> store @Class::field

            std::string className = lowerer_.resolveObjectClass(*member->base);
            if (!className.empty())
            {
                std::string qname = lowerer_.qualify(className);
                std::string setter = std::string("set_") + member->member;
                // Overload resolution for instance setter with one user arg (value)
                auto mapIlToAst = [](Lowerer::Type t) -> ::il::frontends::basic::Type
                {
                    using K = Lowerer::Type::Kind;
                    switch (t.kind)
                    {
                        case K::F64: return ::il::frontends::basic::Type::F64;
                        case K::Str: return ::il::frontends::basic::Type::Str;
                        case K::I1: return ::il::frontends::basic::Type::Bool;
                        default: return ::il::frontends::basic::Type::I64;
                    }
                };
                std::vector<::il::frontends::basic::Type> argTypes{mapIlToAst(value.type)};
                if (auto resolved = sem::resolveMethodOverload(lowerer_.oopIndex_, qname, member->member,
                                                              /*isStatic*/ false, argTypes,
                                                              lowerer_.currentClass(),
                                                              lowerer_.diagnosticEmitter(), stmt.loc))
                {
                    setter = resolved->methodName;
                }
                else if (lowerer_.diagnosticEmitter())
                {
                    return;
                }
                std::string callee = mangleMethod(qname, setter);
                Lowerer::RVal base = lowerer_.lowerExpr(*member->base);
                std::vector<Lowerer::Value> args{base.value, value.value};
                lowerer_.emitCall(callee, args);
                return;
            }

            if (const auto *v = as<const VarExpr>(*member->base))
            {
                // If a symbol with this name exists (local/param/global), treat as instance, not static
                if (const auto *sym = lowerer_.findSymbol(v->name); sym && sym->slotId)
                {
                    // analyzer should have already errored if not a property/field; nothing more to do here
                    return;
                }
                std::string qname = lowerer_.resolveQualifiedClassCasing(lowerer_.qualify(v->name));
                if (const ClassInfo *ci = lowerer_.oopIndex_.findClass(qname))
                {
                    // Prefer static property setter when present
                    std::string setter = std::string("set_") + member->member;
                    auto mapIlToAst = [](Lowerer::Type t) -> ::il::frontends::basic::Type
                    {
                        using K = Lowerer::Type::Kind;
                        switch (t.kind)
                        {
                            case K::F64: return ::il::frontends::basic::Type::F64;
                            case K::Str: return ::il::frontends::basic::Type::Str;
                            case K::I1: return ::il::frontends::basic::Type::Bool;
                            default: return ::il::frontends::basic::Type::I64;
                        }
                    };
                    std::vector<::il::frontends::basic::Type> argTypes{mapIlToAst(value.type)};
                    if (auto resolved = sem::resolveMethodOverload(lowerer_.oopIndex_, qname, member->member,
                                                                  /*isStatic*/ true, argTypes,
                                                                  lowerer_.currentClass(),
                                                                  lowerer_.diagnosticEmitter(), stmt.loc))
                    {
                        setter = resolved->methodName;
                    }
                    else if (lowerer_.diagnosticEmitter())
                    {
                        return;
                    }
                    auto it = ci->methods.find(setter);
                    if (it != ci->methods.end() && it->second.isStatic)
                    {
                        std::string callee = mangleMethod(ci->qualifiedName, setter);
                        lowerer_.emitCall(callee, {value.value});
                        return;
                    }

                    // Otherwise store into a static field global
                    for (const auto &sf : ci->staticFields)
                    {
                        if (sf.name == member->member)
                        {
                            Lowerer::Type ilTy = sf.objectClassName.empty()
                                                     ? type_conv::astToIlType(sf.type)
                                                     : Lowerer::Type(Lowerer::Type::Kind::Ptr);
                            lowerer_.curLoc = stmt.loc;
                            std::string gname = ci->qualifiedName + "::" + member->member;
                            Lowerer::Value addr = lowerer_.emitUnary(Lowerer::Opcode::AddrOf,
                                                                     Lowerer::Type(Lowerer::Type::Kind::Ptr),
                                                                     Lowerer::Value::global(gname));
                            // Coerce booleans when needed
                            Lowerer::RVal vcoerced = value;
                            if (ilTy.kind == Lowerer::Type::Kind::I1)
                                vcoerced = lowerer_.coerceToBool(std::move(vcoerced), stmt.loc);
                            lowerer_.emitStore(ilTy, addr, vcoerced.value);
                            return;
                        }
                    }
                }
            }
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
void RuntimeStatementLowerer::lowerConst(const ConstStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);

    // Evaluate the initializer expression
    Lowerer::RVal value = lowerer_.lowerExpr(*stmt.initializer);

    // Resolve storage for the constant (same as variable)
    auto storage = lowerer_.resolveVariableStorage(stmt.name, stmt.loc);
    assert(storage && "CONST target should have storage");

    // Store the value
    Lowerer::SlotType slotInfo = storage->slotInfo;
    if (slotInfo.isArray)
    {
        lowerer_.storeArray(storage->pointer,
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
void RuntimeStatementLowerer::lowerStatic(const StaticStmt &stmt)
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
Value RuntimeStatementLowerer::emitArrayLengthCheck(Value bound,
                                    il::support::SourceLoc loc,
                                    std::string_view labelBase)
{
    LocationScope location(lowerer_, loc);
    Value length = lowerer_.emitCommon(loc).add_checked(bound, Value::constInt(1), OverflowPolicy::Checked);

    ProcedureContext &ctx = lowerer_.context();
    Function *func = ctx.function();
    BasicBlock *original = ctx.current();
    if (func && original)
    {
        size_t curIdx = static_cast<size_t>(original - &func->blocks[0]);
        BlockNamer *blockNamer = ctx.blockNames().namer();

        std::string base(labelBase);
        std::string failName = base.empty() ? "arr_len_fail" : base + "_fail";
        std::string contName = base.empty() ? "arr_len_cont" : base + "_cont";

        std::string failLbl = blockNamer ? blockNamer->generic(failName) : lowerer_.mangler.block(failName);
        std::string contLbl = blockNamer ? blockNamer->generic(contName) : lowerer_.mangler.block(contName);

        size_t failIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, failLbl);
        size_t contIdx = func->blocks.size();
        lowerer_.builder->addBlock(*func, contLbl);

        BasicBlock *failBlk = &func->blocks[failIdx];
        BasicBlock *contBlk = &func->blocks[contIdx];

        ctx.setCurrent(&func->blocks[curIdx]);
        Value isNeg = lowerer_.emitBinary(Opcode::SCmpLT, lowerer_.ilBoolTy(), length, Value::constInt(0));
        lowerer_.emitCBr(isNeg, failBlk, contBlk);

        ctx.setCurrent(failBlk);
        lowerer_.emitTrap();

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
void RuntimeStatementLowerer::lowerDim(const DimStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);

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
        Lowerer::RVal bound = lowerer_.lowerExpr(**dimExprs[0]);
        bound = lowerer_.ensureI64(std::move(bound), stmt.loc);
        length = emitArrayLengthCheck(bound.value, stmt.loc, "dim_len");
    }
    else
    {
        // For multi-dimensional arrays, compute total size = product of all extents
        // Start with first dimension
        Lowerer::RVal bound = lowerer_.lowerExpr(**dimExprs[0]);
        bound = lowerer_.ensureI64(std::move(bound), stmt.loc);
        Value firstLen = emitArrayLengthCheck(bound.value, stmt.loc, "dim_len");

        // Multiply by remaining dimensions
        Value totalSize = firstLen;
        for (size_t i = 1; i < dimExprs.size(); ++i)
        {
            Lowerer::RVal dimBound = lowerer_.lowerExpr(**dimExprs[i]);
            dimBound = lowerer_.ensureI64(std::move(dimBound), stmt.loc);
            Value dimLen = emitArrayLengthCheck(dimBound.value, stmt.loc, "dim_len");
            totalSize = lowerer_.emitBinary(Opcode::IMulOvf, il::core::Type(il::core::Type::Kind::I64), totalSize, dimLen);
        }
        length = totalSize;
    }

    const auto *info = lowerer_.findSymbol(stmt.name);
    // Determine array element type and call appropriate runtime allocator
    Value handle;
    if (info->type == AstType::Str)
    {
        // String array: use rt_arr_str_alloc
        lowerer_.requireArrayStrAlloc();
        handle = lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::Ptr), "rt_arr_str_alloc", {length});
    }
    else if (info->isObject)
    {
        // Object array
        lowerer_.requireArrayObjNew();
        handle = lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::Ptr), "rt_arr_obj_new", {length});
    }
    else
    {
        // Integer/numeric array: use rt_arr_i32_new
        lowerer_.requireArrayI32New();
        handle = lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::Ptr), "rt_arr_i32_new", {length});
    }

    // Store into the resolved storage (supports module-level globals across procedures)
    if (auto storage = lowerer_.resolveVariableStorage(stmt.name, stmt.loc))
    {
        lowerer_.storeArray(
            storage->pointer, handle, info ? info->type : AstType::I64, info && info->isObject);
    }
    else
    {
        // Avoid hard assertions in production builds; emit a trap so the
        // failure is observable without terminating the entire test suite.
        lowerer_.emitTrap();
    }
    if (lowerer_.boundsChecks)
    {
        if (info && info->arrayLengthSlot)
            lowerer_.emitStore(il::core::Type(il::core::Type::Kind::I64), Value::temp(*info->arrayLengthSlot), length);
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
void RuntimeStatementLowerer::lowerReDim(const ReDimStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);
    Lowerer::RVal bound = lowerer_.lowerExpr(*stmt.size);
    bound = lowerer_.ensureI64(std::move(bound), stmt.loc);
    Value length = emitArrayLengthCheck(bound.value, stmt.loc, "redim_len");
    const auto *info = lowerer_.findSymbol(stmt.name);
    auto storage = lowerer_.resolveVariableStorage(stmt.name, stmt.loc);
    assert(storage && "REDIM target should have resolvable storage");
    Value current = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr), storage->pointer);
    if (info && info->isObject)
        lowerer_.requireArrayObjResize();
    else
        lowerer_.requireArrayI32Resize();
    Value resized =
        lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::Ptr),
                    (info && info->isObject) ? "rt_arr_obj_resize" : "rt_arr_i32_resize",
                    {current, length});
    lowerer_.storeArray(storage->pointer,
               resized,
               /*elementType*/ AstType::I64,
               /*isObjectArray*/ info && info->isObject);
    if (lowerer_.boundsChecks && info && info->arrayLengthSlot)
        lowerer_.emitStore(il::core::Type(il::core::Type::Kind::I64), Value::temp(*info->arrayLengthSlot), length);
}

/// @brief Lower the BASIC @c RANDOMIZE statement configuring the RNG seed.
///
/// @details Requests the runtime feature that exposes the random subsystem,
///          evaluates the optional seed expression (defaulting to zero), and
///          invokes the helper that applies the seed.
///
/// @param stmt Parsed @c RANDOMIZE statement.
void RuntimeStatementLowerer::lowerRandomize(const RandomizeStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);
    Lowerer::RVal s = lowerer_.lowerExpr(*stmt.seed);
    Value seed = lowerer_.coerceToI64(std::move(s), stmt.loc).value;
    lowerer_.emitCall("rt_randomize_i64", {seed});
}

/// @brief Lower a @c SWAP statement to exchange two lvalue contents.
///
/// @details Emits IL instructions to: (1) load both lvalues, (2) store first
///          value into second location, (3) store second value into first location.
///          Uses a temporary slot to hold the first value during the exchange.
///
/// @param stmt Parsed @c SWAP statement.
void RuntimeStatementLowerer::lowerSwap(const SwapStmt &stmt)
{
    LocationScope loc(lowerer_, stmt.loc);

    // Lower both lvalues to get their RVals
    Lowerer::RVal lhsVal = lowerer_.lowerExpr(*stmt.lhs);
    Lowerer::RVal rhsVal = lowerer_.lowerExpr(*stmt.rhs);

    // Store lhs value to temp
    Value tempSlot = lowerer_.emitAlloca(8);
    lowerer_.emitStore(lhsVal.type, tempSlot, lhsVal.value);

    // Assign rhs to lhs
    if (auto *var = as<const VarExpr>(*stmt.lhs))
    {
        auto storage = lowerer_.resolveVariableStorage(var->name, stmt.loc);
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
    Value tempVal = lowerer_.emitLoad(lhsVal.type, tempSlot);
    Lowerer::RVal tempRVal{tempVal, lhsVal.type};
    if (auto *var = as<const VarExpr>(*stmt.rhs))
    {
        auto storage = lowerer_.resolveVariableStorage(var->name, stmt.loc);
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
