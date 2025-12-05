//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/RuntimeStatementLowerer_Assign.cpp
// Purpose: Implementation of assignment-related runtime statement lowering.
//          Handles scalar slot assignments, array element assignments, and
//          the common assignment coercion logic.
// Key invariants: Maintains Lowerer's runtime lowering semantics exactly.
// Ownership/Lifetime: Borrows Lowerer reference; coordinates with parent.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "RuntimeStatementLowerer.hpp"
#include "Lowerer.hpp"
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/LocationScope.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include <cassert>

using namespace il::core;
using il::runtime::RuntimeFeature;
using AstType = ::il::frontends::basic::Type;

namespace il::frontends::basic
{

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
        Value shouldDestroy =
            lowerer_.emitCallRet(lowerer_.ilBoolTy(), "rt_obj_release_check0", {oldValue});

        ProcedureContext &ctx = lowerer_.context();
        Function *func = ctx.function();
        BasicBlock *origin = ctx.current();
        if (func && origin)
        {
            std::size_t originIdx = static_cast<std::size_t>(origin - &func->blocks[0]);
            BlockNamer *blockNamer = ctx.blockNames().namer();
            std::string base = "obj_assign";
            std::string destroyLbl = blockNamer ? blockNamer->generic(base + "_dtor")
                                                : lowerer_.mangler.block(base + "_dtor");
            std::string contLbl = blockNamer ? blockNamer->generic(base + "_cont")
                                             : lowerer_.mangler.block(base + "_cont");

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
void RuntimeStatementLowerer::assignArrayElement(const ArrayExpr &target,
                                                 Lowerer::RVal value,
                                                 il::support::SourceLoc loc)
{
    LocationScope location(lowerer_, loc);

    Lowerer::ArrayAccess access =
        lowerer_.lowerArrayAccess(target, Lowerer::ArrayAccessKind::Store);

    // Determine array element type and use appropriate runtime function
    const auto *info = lowerer_.findSymbol(target.name);
    // Array field assignments (dotted names) derive element type from class layout,
    // not the symbol table. (BUG-056)
    bool isMemberArray = target.name.find('.') != std::string::npos;
    ::il::frontends::basic::Type memberElemAstType = ::il::frontends::basic::Type::I64;
    bool isMemberObjectArray = false; // Track if member array holds objects (BUG-089)
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
                // Object arrays require object-typed runtime calls (BUG-089)
                isMemberObjectArray = !fld->objectClassName.empty();
            }
        }
    }
    // Implicit field array accesses (inside methods) use non-dotted names like `inventory(i)`.
    // Derive element type from the active class layout to choose correct runtime helpers. (BUG-058)
    bool isImplicitFieldArray = (!isMemberArray) && lowerer_.isFieldInScope(target.name);
    // Local variables or parameters shadow implicit field arrays. Prefer the local array. (BUG-108)
    const auto *localSym = lowerer_.findSymbol(target.name);
    if (isImplicitFieldArray && !(localSym && localSym->slotId))
    {
        if (const auto *scope = lowerer_.activeFieldScope(); scope && scope->layout)
        {
            if (const Lowerer::ClassLayout::Field *fld = scope->layout->findField(target.name))
            {
                memberElemAstType = fld->type;
                // Object arrays require object-typed runtime calls (BUG-089)
                isMemberObjectArray = !fld->objectClassName.empty();
            }
            // Recompute base as ME.<field> to ensure we are storing into the
            // instance field array even when the name is implicit.
            const auto *selfInfo = lowerer_.findSymbol("ME");
            if (selfInfo && selfInfo->slotId)
            {
                lowerer_.curLoc = loc;
                Value selfPtr = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr),
                                                  Value::temp(*selfInfo->slotId));
                lowerer_.curLoc = loc;
                // Look up offset again for the named field
                long long offset = 0;
                if (const Lowerer::ClassLayout::Field *f2 = scope->layout->findField(target.name))
                    offset = static_cast<long long>(f2->offset);
                Value fieldPtr = lowerer_.emitBinary(Opcode::GEP,
                                                     il::core::Type(il::core::Type::Kind::Ptr),
                                                     selfPtr,
                                                     Value::constInt(offset));
                lowerer_.curLoc = loc;
                // Load the array handle from the field into access.base
                access.base =
                    lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr), fieldPtr);
            }
        }
    }

    // Prefer RHS-driven dispatch to avoid relying on fragile symbol/type
    // resolution across scopes: a string RHS must use string array helpers,
    // an object (ptr) RHS must use object array helpers; otherwise numeric.
    if (value.type.kind == il::core::Type::Kind::Str || (info && info->type == AstType::Str) ||
        (isMemberArray && memberElemAstType == ::il::frontends::basic::Type::Str) ||
        (isImplicitFieldArray && memberElemAstType == ::il::frontends::basic::Type::Str))
    {
        // String array: use rt_arr_str_put (handles retain/release)
        // Pass the string handle directly - the C runtime expects rt_string by value.
        lowerer_.emitCall("rt_arr_str_put", {access.base, access.index, value.value});
    }
    else if (value.type.kind == il::core::Type::Kind::Ptr ||
             (!isMemberArray && info && info->isObject) || isMemberObjectArray)
    {
        // Object arrays (including member object arrays) use rt_arr_obj_put (BUG-089)
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

} // namespace il::frontends::basic
