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
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include "viper/il/Module.hpp"

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
    // Resolve storage for the target symbol instead of assuming a local slot.
    // This supports module-level globals referenced inside procedures (BUG-053),
    // where globals are routed through runtime-backed storage and do not have
    // a materialised local stack slot.
    // BUG-056: Detect object field array access via dotted name (e.g., B.CELLS(i)).
    // BUG-059/058: Also detect field arrays accessed without dotted syntax in methods (e.g.,
    // inventory(i)).
    const bool isMemberArray = expr.name.find('.') != std::string::npos;

    const auto *info = isMemberArray ? nullptr : findSymbol(expr.name);

    // BUG-097 fix: Check module cache for object array type info
    std::string moduleObjectClass;
    if (!isMemberArray && (!info || (info && !info->isObject)))
    {
        moduleObjectClass = lookupModuleArrayElemClass(expr.name);
    }

    // When accessing array fields, we'll compute 'base' by loading the pointer from
    // the object's field; otherwise we load from variable storage as usual.
    Type::Kind memberElemIlKind = Type::Kind::I64; // default element kind
    ::il::frontends::basic::Type memberElemAstType = ::il::frontends::basic::Type::I64;
    bool isMemberObjectArray = false; // BUG-089: Track if member array holds objects
    Value base;

    // Only resolve storage for non-member arrays
    std::optional<VariableStorage> storage;
    if (!isMemberArray)
    {
        storage = resolveVariableStorage(expr.name, expr.loc);
        assert(storage && "array access requires resolvable storage");
    }

    // Determine element type early so we can require the right runtime functions
    if (isMemberArray)
    {
        // Split into base variable and field name
        const std::string &full = expr.name;
        std::size_t dot = full.find('.');
        std::string baseName = full.substr(0, dot);
        std::string fieldName = full.substr(dot + 1);

        // Look up field type in class layout
        const auto *baseSym = findSymbol(baseName);
        if (baseSym)
        {
            std::string klass = getSlotType(baseName).objectClass;
            auto it = classLayouts_.find(klass);
            if (it != classLayouts_.end())
            {
                if (const ClassLayout::Field *fld = it->second.findField(fieldName))
                {
                    memberElemAstType = fld->type;
                    // BUG-089 fix: Check if field is an object array
                    isMemberObjectArray = !fld->objectClassName.empty();
                    if (fld->type == ::il::frontends::basic::Type::Str)
                        memberElemIlKind = Type::Kind::Str;
                    else if (isMemberObjectArray)
                        memberElemIlKind = Type::Kind::Ptr;
                    else
                        memberElemIlKind = Type::Kind::I64;
                }
            }
        }
    }

    // Require appropriate runtime functions based on array element type
    if (isMemberArray)
    {
        // Use memberElemAstType for field arrays (dotted or implicit)
        if (memberElemAstType == ::il::frontends::basic::Type::Str)
        {
            requireArrayStrLen();
            if (kind == ArrayAccessKind::Load)
                requireArrayStrGet();
            else
            {
                requireArrayStrPut();
                requireStrRetainMaybe();
            }
        }
        else if (isMemberObjectArray)
        {
            // BUG-089 fix: Use object array runtime functions for object fields
            requireArrayObjLen();
            if (kind == ArrayAccessKind::Load)
                requireArrayObjGet();
            else
                requireArrayObjPut();
        }
        else
        {
            requireArrayI32Len();
            if (kind == ArrayAccessKind::Load)
                requireArrayI32Get();
            else
                requireArrayI32Set();
        }
    }
    else if (info && info->type == AstType::Str)
    {
        requireArrayStrLen();
        if (kind == ArrayAccessKind::Load)
            requireArrayStrGet();
        else
        {
            requireArrayStrPut();
            requireStrRetainMaybe();
        }
    }
    else if ((info && info->isObject) || !moduleObjectClass.empty())
    {
        // BUG-097 fix: Use object array functions for module-level object arrays too
        requireArrayObjLen();
        if (kind == ArrayAccessKind::Load)
            requireArrayObjGet();
        else
            requireArrayObjPut();
    }
    else
    {
        requireArrayI32Len();
        if (kind == ArrayAccessKind::Load)
            requireArrayI32Get();
        else
            requireArrayI32Set();
    }
    requireArrayOobPanic();

    // Capture member field extents when available so we can compute
    // correct row-major flattened indices for multi-dimensional arrays.
    std::vector<long long> memberFieldExtents;
    ProcedureContext &ctx = context();
    if (isMemberArray)
    {
        // Split into base variable and field name
        const std::string &full = expr.name;
        std::size_t dot = full.find('.');
        std::string baseName = full.substr(0, dot);
        std::string fieldName = full.substr(dot + 1);

        // Load the object pointer for the base
        const auto *baseSym = findSymbol(baseName);
        if (baseSym && baseSym->slotId)
        {
            curLoc = expr.loc;
            Value selfPtr = emitLoad(Type(Type::Kind::Ptr), Value::temp(*baseSym->slotId));
            // Find field in class layout (already looked up above)
            std::string klass = getSlotType(baseName).objectClass;
            auto it = classLayouts_.find(klass);
            if (it != classLayouts_.end())
            {
                if (const ClassLayout::Field *fld = it->second.findField(fieldName))
                {
                    // Type already set above
                    memberElemAstType = fld->type;
                    // BUG-089 fix: Check if field is an object array (same as earlier)
                    isMemberObjectArray = !fld->objectClassName.empty();
                    if (fld->type == ::il::frontends::basic::Type::Str)
                        memberElemIlKind = Type::Kind::Str;
                    else if (isMemberObjectArray)
                        memberElemIlKind = Type::Kind::Ptr;
                    else
                        memberElemIlKind = Type::Kind::I64;
                    curLoc = expr.loc;
                    Value fieldPtr =
                        emitBinary(Opcode::GEP,
                                   Type(Type::Kind::Ptr),
                                   selfPtr,
                                   Value::constInt(static_cast<long long>(fld->offset)));
                    curLoc = expr.loc;
                    base = emitLoad(Type(Type::Kind::Ptr), fieldPtr);
                    // Use declared extents from class layout if available
                    if (fld->isArray)
                    {
                        memberFieldExtents = fld->arrayExtents;
                    }
                }
            }
        }
    }
    else
    {
        // storage->pointer is the address of the variable's storage (local slot or
        // runtime-backed module variable). Load the array handle pointer from it.
        base = emitLoad(Type(Type::Kind::Ptr), storage->pointer);
    }

    // Collect all index expressions (backward compat: check 'index' first, then 'indices')
    std::vector<const ExprPtr *> indexExprs;
    if (expr.index)
    {
        indexExprs.push_back(&expr.index);
    }
    else
    {
        for (const auto &idxExpr : expr.indices)
        {
            if (idxExpr)
                indexExprs.push_back(&idxExpr);
        }
    }
    assert(!indexExprs.empty() && "array access must have at least one index");

    // Lower all index expressions to i64 for bounds checking in the current block
    std::vector<Value> indices;
    for (const ExprPtr *idxPtr : indexExprs)
    {
        RVal idx = lowerExpr(**idxPtr);
        idx = coerceToI64(std::move(idx), expr.loc);
        indices.push_back(idx.value);
    }
    curLoc = expr.loc;

    // Compute flattened index for multi-dimensional arrays using row-major order
    // For N dimensions with extents [E0, E1, ..., E_{N-1}] and indices [i0, i1, ..., i_{N-1}]:
    // flat_index = i0*L1*L2*...*L_{N-1} + i1*L2*...*L_{N-1} + ... + i_{N-2}*L_{N-1} + i_{N-1}
    // where Lk = (Ek + 1) are inclusive lengths per dimension.
    Value index;
    // For implicit field arrays (e.g., inventory(i) in methods), retrieve extents from active layout
    if (memberFieldExtents.empty())
    {
        // Try field scope layout for implicit field arrays
        if (const FieldScope *scope = activeFieldScope(); scope && scope->layout)
        {
            if (const ClassLayout::Field *fld2 = scope->layout->findField(expr.name))
            {
                if (fld2->isArray)
                    memberFieldExtents = fld2->arrayExtents;
            }
        }
    }
    auto computeFlatIndex = [&](const std::vector<Value> &idxVals) -> Value
    {
        if (idxVals.size() == 1)
            return idxVals[0];
        // Prefer member field extents when available
        if (!memberFieldExtents.empty() && memberFieldExtents.size() == idxVals.size())
        {
            // Convert declared bounds to inclusive lengths
            std::vector<long long> lengths(memberFieldExtents.size(), 0);
            for (size_t i = 0; i < memberFieldExtents.size(); ++i)
                lengths[i] = memberFieldExtents[i] + 1;
            long long stride = 1;
            for (size_t i = 1; i < lengths.size(); ++i)
                stride *= lengths[i];
            Value sum = emitBinary(Opcode::IMulOvf,
                                   Type(Type::Kind::I64),
                                   idxVals[0],
                                   Value::constInt(stride));
            for (size_t k = 1; k < idxVals.size(); ++k)
            {
                stride = 1;
                for (size_t i = k + 1; i < lengths.size(); ++i)
                    stride *= lengths[i];
                Value term = emitBinary(
                    Opcode::IMulOvf, Type(Type::Kind::I64), idxVals[k], Value::constInt(stride));
                sum = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), sum, term);
            }
            return sum;
        }
        // Analyzer metadata for non-field arrays; convert bounds to lengths via +1
        const SemanticAnalyzer *sema = semanticAnalyzer();
        const ArrayMetadata *metadata = sema ? sema->lookupArrayMetadata(expr.name) : nullptr;
        if (metadata && metadata->extents.size() == idxVals.size())
        {
            std::vector<long long> lengths(metadata->extents.size(), 0);
            for (size_t i = 0; i < metadata->extents.size(); ++i)
                lengths[i] = metadata->extents[i] + 1;
            long long stride = 1;
            for (size_t i = 1; i < lengths.size(); ++i)
                stride *= lengths[i];
            Value sum = emitBinary(Opcode::IMulOvf,
                                   Type(Type::Kind::I64),
                                   idxVals[0],
                                   Value::constInt(stride));
            for (size_t k = 1; k < idxVals.size(); ++k)
            {
                stride = 1;
                for (size_t i = k + 1; i < lengths.size(); ++i)
                    stride *= lengths[i];
                Value term = emitBinary(
                    Opcode::IMulOvf, Type(Type::Kind::I64), idxVals[k], Value::constInt(stride));
                sum = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), sum, term);
            }
            return sum;
        }
        // Fallback: just use first index
        return idxVals[0];
    };
    index = computeFlatIndex(indices);

    // Use appropriate length function based on array element type
    Value len;
    if (isMemberArray)
    {
        if (memberElemAstType == ::il::frontends::basic::Type::Str)
            len = emitCallRet(Type(Type::Kind::I64), "rt_arr_str_len", {base});
        else if (isMemberObjectArray)
            len = emitCallRet(Type(Type::Kind::I64), "rt_arr_obj_len", {base}); // BUG-089 fix
        else
            len = emitCallRet(Type(Type::Kind::I64), "rt_arr_i32_len", {base});
    }
    else
    {
        assert(info && "info must be non-null for non-member arrays");
        if (info->type == AstType::Str)
            len = emitCallRet(Type(Type::Kind::I64), "rt_arr_str_len", {base});
        else if (info->isObject || !moduleObjectClass.empty())
            len = emitCallRet(Type(Type::Kind::I64), "rt_arr_obj_len", {base}); // BUG-097: Check module cache too
        else
            len = emitCallRet(Type(Type::Kind::I64), "rt_arr_i32_len", {base});
    }
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
    // Only for string/object arrays (value or member), re-lower base/index in the ok block
    // to avoid cross-block temp reuse issues seen with reference-counted element handling.
    bool isRefCountedArray = false;
    if (isMemberArray)
        isRefCountedArray = (memberElemAstType == ::il::frontends::basic::Type::Str) || isMemberObjectArray;
    else if (info)
        isRefCountedArray = (info->type == AstType::Str) || info->isObject;

    if (isRefCountedArray)
    {
        Value baseOk;
        if (isMemberArray)
        {
            const std::string &full = expr.name;
            std::size_t dot = full.find('.');
            std::string baseName = full.substr(0, dot);
            std::string fieldName = full.substr(dot + 1);
            const auto *baseSym = findSymbol(baseName);
            if (baseSym && baseSym->slotId)
            {
                curLoc = expr.loc;
                Value selfPtr = emitLoad(Type(Type::Kind::Ptr), Value::temp(*baseSym->slotId));
                std::string klass = getSlotType(baseName).objectClass;
                auto it = classLayouts_.find(klass);
                if (it != classLayouts_.end())
                {
                    if (const ClassLayout::Field *fld = it->second.findField(fieldName))
                    {
                        Value fieldPtr = emitBinary(Opcode::GEP,
                                                    Type(Type::Kind::Ptr),
                                                    selfPtr,
                                                    Value::constInt(static_cast<long long>(fld->offset)));
                        baseOk = emitLoad(Type(Type::Kind::Ptr), fieldPtr);
                    }
                }
            }
        }
        else
        {
            baseOk = emitLoad(Type(Type::Kind::Ptr), storage->pointer);
        }
        std::vector<Value> indicesOk;
        indicesOk.reserve(indexExprs.size());
        for (const ExprPtr *idxPtr : indexExprs)
        {
            RVal idx = lowerExpr(**idxPtr);
            idx = coerceToI64(std::move(idx), expr.loc);
            indicesOk.push_back(idx.value);
        }
        Value indexOk = computeFlatIndex(indicesOk);
        return ArrayAccess{baseOk, indexOk};
    }
    // Non-reference-counted arrays (i32/i64/f64): keep original SSA values to preserve IL golden tests
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

/// @brief Narrow a 64-bit value to 32 bits.
/// @details Convenience helper that wraps emitCommon().to_iN(value, 32) to reduce
///          boilerplate when preparing arguments for 32-bit runtime function calls.
///          This pattern appears frequently in LowerStmt_Runtime.cpp when calling
///          terminal control functions that expect i32 arguments.
/// @param value The value to narrow (typically i64 from BASIC expressions).
/// @param loc Source location for the narrowing instruction.
/// @return Narrowed 32-bit value suitable for passing to runtime helpers.
Value Lowerer::narrow32(Value value, il::support::SourceLoc loc)
{
    return emitCommon(loc).to_iN(value, 32);
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    emitter().emitCall(callee, args);
}

Value Lowerer::emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args)
{
    return emitter().emitCallRet(ty, callee, args);
}

/// @brief Request a runtime helper and emit a call in one operation.
/// @details Combines requestHelper() and emitCallRet() to reduce boilerplate when
///          calling runtime functions. This is especially useful in lowering visitor
///          methods where both operations are always performed together.
/// @param feature Runtime feature to request (ensures the helper is linked).
/// @param callee Name of the runtime function to call.
/// @param returnType Return type of the runtime function.
/// @param args Arguments to pass to the runtime function.
/// @return Value representing the result of the call.
Value Lowerer::emitRuntimeHelper(il::runtime::RuntimeFeature feature,
                                 const std::string &callee,
                                 Type returnType,
                                 const std::vector<Value> &args)
{
    requestHelper(feature);
    return emitCallRet(returnType, callee, args);
}

Value Lowerer::emitCallIndirectRet(Type ty, Value callee, const std::vector<Value> &args)
{
    return emitter().emitCallIndirectRet(ty, callee, args);
}

void Lowerer::emitCallIndirect(Value callee, const std::vector<Value> &args)
{
    emitter().emitCallIndirect(callee, args);
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
