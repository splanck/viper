//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/RuntimeStatementLowerer.cpp
// Purpose: Implementation of runtime statement lowering extracted from Lowerer.
//          Handles lowering of BASIC runtime statements (terminal control,
//          assignments, variable declarations) to IL and runtime calls.
//
// NOTE: This file has been split into focused modules:
//   - RuntimeStatementLowerer_Terminal.cpp : BEEP, CLS, COLOR, LOCATE, etc.
//   - RuntimeStatementLowerer_Assign.cpp   : assignScalarSlot, assignArrayElement
//   - RuntimeStatementLowerer_Decl.cpp     : DIM, REDIM, CONST, STATIC, etc.
//
// This file retains the constructor and lowerLet (the largest, most complex
// function that handles various assignment target forms).
//
// Key invariants: Maintains Lowerer's runtime lowering semantics exactly
// Ownership/Lifetime: Borrows Lowerer reference; coordinates with parent
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "RuntimeStatementLowerer.hpp"
#include "Lowerer.hpp"
#include "RuntimeCallHelpers.hpp"
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/ILTypeUtils.hpp"
#include "frontends/basic/LocationScope.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "frontends/basic/sem/OverloadResolution.hpp"
#include "frontends/basic/sem/RuntimePropertyIndex.hpp"

#include <cassert>

using namespace il::core;
using il::runtime::RuntimeFeature;
using AstType = ::il::frontends::basic::Type;

namespace il::frontends::basic
{

RuntimeStatementLowerer::RuntimeStatementLowerer(Lowerer &lowerer) : lowerer_(lowerer) {}

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
        // Invariant: Slot typing must be refreshed from symbols/sema on each use
        // to avoid stale kinds when crossing complex control flow (e.g., SELECT CASE). (BUG-076)
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
        // Handle array field assignment (obj.arrayField(index) = value). (BUG-056)
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
                    Value selfPtr = lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr),
                                                      Value::temp(*baseSym->slotId));
                    std::string klass = lowerer_.getSlotType(baseName).objectClass;
                    if (const Lowerer::ClassLayout *layout = lowerer_.findClassLayout(klass))
                    {
                        if (const Lowerer::ClassLayout::Field *fld = layout->findField(mc->method))
                        {
                            lowerer_.curLoc = stmt.loc;
                            Value fieldPtr = lowerer_.emitBinary(
                                Opcode::GEP,
                                il::core::Type(il::core::Type::Kind::Ptr),
                                selfPtr,
                                Value::constInt(static_cast<long long>(fld->offset)));
                            Value arrHandle = lowerer_.emitLoad(
                                il::core::Type(il::core::Type::Kind::Ptr), fieldPtr);

                            // Multi-dimensional arrays require flattened index computation.
                            // (BUG-094)
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
                                    // For extents [E0, E1, ..., E_{N-1}] and indices [i0, i1, ...,
                                    // i_{N-1}]: flat = i0*L1*L2*...*L_{N-1} + i1*L2*...*L_{N-1} +
                                    // ... + i_{N-1} where Lk = (Ek + 1) are inclusive lengths per
                                    // dimension.
                                    std::vector<long long> lengths;
                                    for (long long e : fld->arrayExtents)
                                        lengths.push_back(e + 1);

                                    long long stride = 1;
                                    for (size_t i = 1; i < lengths.size(); ++i)
                                        stride *= lengths[i];

                                    lowerer_.curLoc = stmt.loc;
                                    index = lowerer_.emitBinary(
                                        Opcode::IMulOvf,
                                        il::core::Type(il::core::Type::Kind::I64),
                                        indices[0],
                                        Value::constInt(stride));

                                    for (size_t k = 1; k < indices.size(); ++k)
                                    {
                                        stride = 1;
                                        for (size_t i = k + 1; i < lengths.size(); ++i)
                                            stride *= lengths[i];
                                        lowerer_.curLoc = stmt.loc;
                                        Value term = lowerer_.emitBinary(
                                            Opcode::IMulOvf,
                                            il::core::Type(il::core::Type::Kind::I64),
                                            indices[k],
                                            Value::constInt(stride));
                                        lowerer_.curLoc = stmt.loc;
                                        index = lowerer_.emitBinary(
                                            Opcode::IAddOvf,
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
                                len =
                                    lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::I64),
                                                         "rt_arr_str_len",
                                                         {arrHandle});
                            }
                            else if (isMemberObjectArray)
                            {
                                lowerer_.requireArrayObjLen();
                                len =
                                    lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::I64),
                                                         "rt_arr_obj_len",
                                                         {arrHandle});
                            }
                            else
                            {
                                lowerer_.requireArrayI64Len();
                                len =
                                    lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::I64),
                                                         "rt_arr_i64_len",
                                                         {arrHandle});
                            }
                            Value isNeg = lowerer_.emitBinary(
                                Opcode::SCmpLT, lowerer_.ilBoolTy(), index, Value::constInt(0));
                            Value tooHigh = lowerer_.emitBinary(
                                Opcode::SCmpGE, lowerer_.ilBoolTy(), index, len);
                            auto emitc = lowerer_.emitCommon(stmt.loc);
                            Value isNeg64 = emitc.widen_to(isNeg, 1, 64, Signedness::Unsigned);
                            Value tooHigh64 = emitc.widen_to(tooHigh, 1, 64, Signedness::Unsigned);
                            Value oobInt = emitc.logical_or(isNeg64, tooHigh64);
                            Value oobCond = lowerer_.emitBinary(
                                Opcode::ICmpNe, lowerer_.ilBoolTy(), oobInt, Value::constInt(0));

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
                                blockNamer
                                    ? blockNamer->tag("bc_oob" + std::to_string(bcId))
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
                                lowerer_.emitStore(
                                    il::core::Type(il::core::Type::Kind::Str), tmp, value.value);
                                lowerer_.emitCall("rt_arr_str_put", {arrHandle, index, tmp});
                            }
                            else if (isMemberObjectArray)
                            {
                                lowerer_.requireArrayObjPut();
                                lowerer_.emitCall("rt_arr_obj_put",
                                                  {arrHandle, index, value.value});
                            }
                            else if (fld->type == ::il::frontends::basic::Type::F64)
                            {
                                lowerer_.requireArrayF64Set();
                                Lowerer::RVal coerced =
                                    lowerer_.ensureF64(std::move(value), stmt.loc);
                                lowerer_.emitCall("rt_arr_f64_set",
                                                  {arrHandle, index, coerced.value});
                            }
                            else
                            {
                                lowerer_.requireArrayI64Set();
                                Lowerer::RVal coerced =
                                    lowerer_.ensureI64(std::move(value), stmt.loc);
                                lowerer_.emitCall("rt_arr_i64_set",
                                                  {arrHandle, index, coerced.value});
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
        // CallExpr can be an implicit field array access (e.g., items(i) inside a method).
        // Check if this refers to a field array in the current class. (BUG-089)
        if (lowerer_.isFieldInScope(call->callee))
        {
            if (const auto *scope = lowerer_.activeFieldScope(); scope && scope->layout)
            {
                if (const Lowerer::ClassLayout::Field *fld = scope->layout->findField(call->callee))
                {
                    if (fld->isArray)
                    {
                        // This is a field array access. Lower it inline similar to MethodCallExpr
                        // handling. Get the ME pointer and compute the field array handle
                        const auto *selfInfo = lowerer_.findSymbol("ME");
                        if (selfInfo && selfInfo->slotId)
                        {
                            lowerer_.curLoc = stmt.loc;
                            Value selfPtr =
                                lowerer_.emitLoad(il::core::Type(il::core::Type::Kind::Ptr),
                                                  Value::temp(*selfInfo->slotId));
                            lowerer_.curLoc = stmt.loc;
                            Value fieldPtr = lowerer_.emitBinary(
                                Opcode::GEP,
                                il::core::Type(il::core::Type::Kind::Ptr),
                                selfPtr,
                                Value::constInt(static_cast<long long>(fld->offset)));
                            Value arrHandle = lowerer_.emitLoad(
                                il::core::Type(il::core::Type::Kind::Ptr), fieldPtr);

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
                                len =
                                    lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::I64),
                                                         "rt_arr_str_len",
                                                         {arrHandle});
                            }
                            else if (isMemberObjectArray)
                            {
                                lowerer_.requireArrayObjLen();
                                len =
                                    lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::I64),
                                                         "rt_arr_obj_len",
                                                         {arrHandle});
                            }
                            else
                            {
                                lowerer_.requireArrayI64Len();
                                len =
                                    lowerer_.emitCallRet(il::core::Type(il::core::Type::Kind::I64),
                                                         "rt_arr_i64_len",
                                                         {arrHandle});
                            }

                            Value isNeg = lowerer_.emitBinary(
                                Opcode::SCmpLT, lowerer_.ilBoolTy(), index, Value::constInt(0));
                            Value tooHigh = lowerer_.emitBinary(
                                Opcode::SCmpGE, lowerer_.ilBoolTy(), index, len);
                            auto emitc = lowerer_.emitCommon(stmt.loc);
                            Value isNeg64 = emitc.widen_to(isNeg, 1, 64, Signedness::Unsigned);
                            Value tooHigh64 = emitc.widen_to(tooHigh, 1, 64, Signedness::Unsigned);
                            Value oobInt = emitc.logical_or(isNeg64, tooHigh64);
                            Value oobCond = lowerer_.emitBinary(
                                Opcode::ICmpNe, lowerer_.ilBoolTy(), oobInt, Value::constInt(0));

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
                                blockNamer
                                    ? blockNamer->tag("bc_oob" + std::to_string(bcId))
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
                                lowerer_.emitStore(
                                    il::core::Type(il::core::Type::Kind::Str), tmp, value.value);
                                lowerer_.emitCall("rt_arr_str_put", {arrHandle, index, tmp});
                            }
                            else if (isMemberObjectArray)
                            {
                                lowerer_.requireArrayObjPut();
                                lowerer_.emitCall("rt_arr_obj_put",
                                                  {arrHandle, index, value.value});
                            }
                            else if (fld->type == ::il::frontends::basic::Type::F64)
                            {
                                lowerer_.requireArrayF64Set();
                                Lowerer::RVal coerced =
                                    lowerer_.ensureF64(std::move(value), stmt.loc);
                                lowerer_.emitCall("rt_arr_f64_set",
                                                  {arrHandle, index, coerced.value});
                            }
                            else
                            {
                                lowerer_.requireArrayI64Set();
                                Lowerer::RVal coerced =
                                    lowerer_.ensureI64(std::move(value), stmt.loc);
                                lowerer_.emitCall("rt_arr_i64_set",
                                                  {arrHandle, index, coerced.value});
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
            slotInfo.isObject =
                !access->objectClassName.empty(); // Object fields use pointer semantics (BUG-082)
            if (slotInfo.isObject)
            {
                slotInfo.objectClass = access->objectClassName;
            }
            assignScalarSlot(slotInfo, access->ptr, std::move(value), stmt.loc);
        }
        else
        {
            // Runtime class property setter via catalog (e.g., Viper.String)
            {
                Lowerer::RVal baseVal = lowerer_.lowerExpr(*member->base);
                std::string qClass;
                {
                    std::string cls = lowerer_.resolveObjectClass(*member->base);
                    if (!cls.empty())
                        qClass = lowerer_.qualify(cls);
                }
                if (qClass.empty() && baseVal.type.kind == Lowerer::Type::Kind::Str)
                    qClass = "Viper.String";
                if (!qClass.empty())
                {
                    auto &pidx = runtimePropertyIndex();
                    auto prop = pidx.find(qClass, member->member);
                    if (!prop && string_utils::iequals(qClass, "Viper.String"))
                        prop = pidx.find("Viper.System.String", member->member);
                    if (prop)
                    {
                        if (prop->readonly || prop->setter.empty())
                        {
                            if (auto *em = lowerer_.diagnosticEmitter())
                            {
                                std::string msg = "property '" + member->member + "' on '" +
                                                  qClass + "' is read-only";
                                em->emit(il::support::Severity::Error,
                                         "E_PROP_READONLY",
                                         stmt.loc,
                                         static_cast<uint32_t>(member->member.size()),
                                         std::move(msg));
                            }
                            return;
                        }
                        auto mapTy = [](std::string_view t) -> Lowerer::Type::Kind
                        {
                            if (t == "i64")
                                return Lowerer::Type::Kind::I64;
                            if (t == "f64")
                                return Lowerer::Type::Kind::F64;
                            if (t == "i1")
                                return Lowerer::Type::Kind::I1;
                            if (t == "str")
                                return Lowerer::Type::Kind::Str;
                            return Lowerer::Type::Kind::I64;
                        };
                        Lowerer::RVal v = value;
                        // Coerce according to expected type token
                        auto k = mapTy(prop->type);
                        if (k == Lowerer::Type::Kind::I1)
                            v = lowerer_.coerceToBool(std::move(v), stmt.loc);
                        else if (k == Lowerer::Type::Kind::F64)
                            v = lowerer_.coerceToF64(std::move(v), stmt.loc);
                        else if (k == Lowerer::Type::Kind::I64)
                            v = lowerer_.coerceToI64(std::move(v), stmt.loc);
                        lowerer_.emitCall(prop->setter, {baseVal.value, v.value});
                        return;
                    }
                    else if (auto *em = lowerer_.diagnosticEmitter())
                    {
                        std::string msg =
                            "no such property '" + member->member + "' on '" + qClass + "'";
                        em->emit(il::support::Severity::Error,
                                 "E_PROP_NO_SUCH_PROPERTY",
                                 stmt.loc,
                                 static_cast<uint32_t>(member->member.size()),
                                 std::move(msg));
                        return;
                    }
                }
            }
            // Property setter sugar (instance): base.member = value -> call set_member(base, value)
            // Property setter sugar (static):   Class.member = value -> call
            // Class.set_member(value) Static field assignment:          Class.field  = value ->
            // store @Class::field

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
                        case K::F64:
                            return ::il::frontends::basic::Type::F64;
                        case K::Str:
                            return ::il::frontends::basic::Type::Str;
                        case K::I1:
                            return ::il::frontends::basic::Type::Bool;
                        default:
                            return ::il::frontends::basic::Type::I64;
                    }
                };
                std::vector<::il::frontends::basic::Type> argTypes{mapIlToAst(value.type)};
                if (auto resolved = sem::resolveMethodOverload(lowerer_.oopIndex_,
                                                               qname,
                                                               member->member,
                                                               /*isStatic*/ false,
                                                               argTypes,
                                                               lowerer_.currentClass(),
                                                               lowerer_.diagnosticEmitter(),
                                                               stmt.loc))
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
                // If a symbol with this name exists (local/param/global), treat as instance, not
                // static
                if (const auto *sym = lowerer_.findSymbol(v->name); sym && sym->slotId)
                {
                    // analyzer should have already errored if not a property/field; nothing more to
                    // do here
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
                            case K::F64:
                                return ::il::frontends::basic::Type::F64;
                            case K::Str:
                                return ::il::frontends::basic::Type::Str;
                            case K::I1:
                                return ::il::frontends::basic::Type::Bool;
                            default:
                                return ::il::frontends::basic::Type::I64;
                        }
                    };
                    std::vector<::il::frontends::basic::Type> argTypes{mapIlToAst(value.type)};
                    if (auto resolved = sem::resolveMethodOverload(lowerer_.oopIndex_,
                                                                   qname,
                                                                   member->member,
                                                                   /*isStatic*/ true,
                                                                   argTypes,
                                                                   lowerer_.currentClass(),
                                                                   lowerer_.diagnosticEmitter(),
                                                                   stmt.loc))
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
                            Lowerer::Value addr =
                                lowerer_.emitUnary(Lowerer::Opcode::AddrOf,
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

// The following functions have been moved to separate files:
// - visit(BeepStmt), visit(ClsStmt), etc. -> RuntimeStatementLowerer_Terminal.cpp
// - assignScalarSlot, assignArrayElement -> RuntimeStatementLowerer_Assign.cpp
// - lowerConst, lowerStatic, lowerDim, lowerReDim, lowerRandomize, lowerSwap,
//   emitArrayLengthCheck -> RuntimeStatementLowerer_Decl.cpp

} // namespace il::frontends::basic
