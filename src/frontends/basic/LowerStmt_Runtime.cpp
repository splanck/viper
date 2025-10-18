// File: src/frontends/basic/LowerStmt_Runtime.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements runtime-oriented BASIC statement lowering helpers such as
//          assignment, array management, and terminal control statements.
// Key invariants: Helpers reuse Lowerer's active context to manage string
//                 ownership, runtime feature requests, and diagnostics.
// Ownership/Lifetime: Operates on Lowerer without owning AST nodes or IL data.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"

#include "frontends/basic/NameMangler_OOP.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

void Lowerer::visit(const ClsStmt &s)
{
    curLoc = s.loc;
    requestHelper(il::runtime::RuntimeFeature::TermCls);
    emitCallRet(Type(Type::Kind::Void), "rt_term_cls", {});
}

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

    else if (slotInfo.isObject)
    {
        requestHelper(RuntimeFeature::ObjReleaseChk0);
        requestHelper(RuntimeFeature::ObjFree);
        requestHelper(RuntimeFeature::ObjRetainMaybe);

        Value oldValue = emitLoad(Type(Type::Kind::Ptr), slot);
        Value shouldDestroy =
            emitCallRet(ilBoolTy(), "rt_obj_release_check0", {oldValue});

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

void Lowerer::assignArrayElement(const ArrayExpr &target, RVal value, il::support::SourceLoc loc)
{
    if (value.type.kind == Type::Kind::I1)
    {
        value = coerceToI64(std::move(value), loc);
    }

    ArrayAccess access = lowerArrayAccess(target, ArrayAccessKind::Store);
    curLoc = loc;
    emitCall("rt_arr_i32_set", {access.base, access.index, value.value});
}

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

Value Lowerer::emitArrayLengthCheck(Value bound,
                                    il::support::SourceLoc loc,
                                    std::string_view labelBase)
{
    curLoc = loc;
    Value length = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), bound, Value::constInt(1));

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

/// @brief Lower a REDIM array reallocation.
/// @param stmt REDIM statement describing the new size.
/// @details Re-evaluates the target length, invokes @c rt_arr_i32_resize to
///          adjust the array storage, and stores the returned handle into the
///          tracked array slot, mirroring DIM lowering semantics.
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

/// @brief Lower a RANDOMIZE seed update.
/// @param stmt RANDOMIZE statement carrying the seed expression.
/// @details Converts the seed expression to a 64-bit integer when necessary and
///          invokes the runtime @c rt_randomize_i64 helper. @ref curLoc is
///          updated to associate diagnostics with the statement while leaving
///          @ref cur unchanged.
void Lowerer::lowerRandomize(const RandomizeStmt &stmt)
{
    RVal s = lowerExpr(*stmt.seed);
    Value seed = coerceToI64(std::move(s), stmt.loc).value;
    curLoc = stmt.loc;
    emitCall("rt_randomize_i64", {seed});
}

} // namespace il::frontends::basic
