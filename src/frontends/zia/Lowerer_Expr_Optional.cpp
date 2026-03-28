//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/zia/Lowerer_Expr_Optional.cpp
// Purpose: Optional-related expression lowering for the Zia IL lowerer —
//          null-coalescing (??), optional chaining (?.), try (?),
//          force-unwrap (!), and await expressions.
// Key invariants:
//   - Null checks use alloca+store+load pattern to convert Ptr to I64 for ICmpNe
//   - Coalesce and optional chain produce merge blocks for both paths
// Ownership/Lifetime:
//   - Lowerer owns IL builder; block indices are stable within a function
// Links: src/frontends/zia/Lowerer.hpp, src/frontends/zia/Lowerer_Expr_Complex.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"

namespace il::frontends::zia {

using namespace runtime;

//=============================================================================
// Coalesce Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerCoalesce(CoalesceExpr *expr) {
    // Get the type to determine how to handle the coalesce
    TypeRef leftType = sema_.typeOf(expr->left.get());
    TypeRef resultType = sema_.typeOf(expr);
    Type ilResultType = mapType(resultType);
    bool expectsOptional = resultType && resultType->kind == TypeKindSem::Optional;
    TypeRef optionalInner = expectsOptional ? resultType->innerType() : nullptr;
    (void)optionalInner;
    // Unwrap type comes from the left operand's optional inner type, not the result.
    // For nested coalescing (a ?? b) ?? c, the left may already be non-optional.
    bool leftIsOptional = leftType && leftType->kind == TypeKindSem::Optional;
    TypeRef innerType = leftIsOptional ? leftType->innerType() : nullptr;

    // For reference types (entities, etc.), check if the pointer is null
    // For value-type optionals, we would need to check the flag field
    // Currently implementing reference-type coalesce

    // Allocate a stack slot for the result BEFORE branching
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)}; // 8 bytes for ptr/i64
    allocaInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value resultSlot = Value::temp(allocaId);

    // Lower the left expression
    auto left = lowerExpr(expr->left.get());

    // Create blocks for the coalesce
    size_t hasValueIdx = createBlock("coalesce_has");
    size_t isNullIdx = createBlock("coalesce_null");
    size_t mergeIdx = createBlock("coalesce_merge");

    // Check if it's null (for reference types, compare pointer to 0)
    // Note: ICmpNe requires i64 operands, so we convert the pointer via alloca/store/load
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    ptrSlotInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = left.type;
    storePtrInstr.operands = {ptrSlot, left.value};
    storePtrInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    loadAsI64Instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNotNull =
        emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
    emitCBr(isNotNull, hasValueIdx, isNullIdx);

    // Has value block - store left value and branch to merge
    setBlock(hasValueIdx);
    {
        Value unwrapped = left.value;
        if (innerType) {
            auto innerVal = emitOptionalUnwrap(left.value, innerType);
            unwrapped = innerVal.value;
        }
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = ilResultType;
        storeInstr.operands = {resultSlot, unwrapped};
        storeInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }
    emitBr(mergeIdx);

    // Is null block - evaluate right, store, and branch to merge
    setBlock(isNullIdx);
    auto right = lowerExpr(expr->right.get());
    {
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = ilResultType;
        storeInstr.operands = {resultSlot, right.value};
        storeInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }
    emitBr(mergeIdx);

    // Merge block - load the result
    setBlock(mergeIdx);
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilResultType;
    loadInstr.operands = {resultSlot};
    loadInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilResultType};
}

//=============================================================================
// Optional Chain Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerOptionalChain(OptionalChainExpr *expr) {
    auto base = lowerExpr(expr->base.get());
    TypeRef baseType = sema_.typeOf(expr->base.get());
    if (!baseType || baseType->kind != TypeKindSem::Optional) {
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    TypeRef innerType = baseType->innerType();
    TypeRef fieldType = types::unknown();

    // Allocate a stack slot for the result (optional pointer)
    unsigned resultSlotId = nextTempId();
    il::core::Instr resultAlloca;
    resultAlloca.result = resultSlotId;
    resultAlloca.op = Opcode::Alloca;
    resultAlloca.type = Type(Type::Kind::Ptr);
    resultAlloca.operands = {Value::constInt(8)};
    resultAlloca.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(resultAlloca);
    Value resultSlot = Value::temp(resultSlotId);

    // Compare optional pointer with null
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    ptrSlotInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, base.value};
    storePtrInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    loadAsI64Instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNull = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));

    size_t hasValueIdx = createBlock("optchain_has");
    size_t isNullIdx = createBlock("optchain_null");
    size_t mergeIdx = createBlock("optchain_merge");
    emitCBr(isNull, isNullIdx, hasValueIdx);

    // Null block
    setBlock(isNullIdx);
    il::core::Instr storeNull;
    storeNull.op = Opcode::Store;
    storeNull.type = Type(Type::Kind::Ptr);
    storeNull.operands = {resultSlot, Value::null()};
    storeNull.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(storeNull);
    emitBr(mergeIdx);

    // Has value block
    setBlock(hasValueIdx);
    Value fieldValue = Value::null();
    if (innerType) {
        if (innerType->kind == TypeKindSem::Struct || innerType->kind == TypeKindSem::Class) {
            const std::unordered_map<std::string, StructTypeInfo> &valueTypes = structTypes_;
            const std::unordered_map<std::string, ClassTypeInfo> &entityTypes = classTypes_;
            if (innerType->kind == TypeKindSem::Struct) {
                auto it = valueTypes.find(innerType->name);
                if (it != valueTypes.end()) {
                    const FieldLayout *field = it->second.findField(expr->field);
                    if (field) {
                        fieldType = field->type;
                        fieldValue = emitFieldLoad(field, base.value);
                    }
                }
            } else {
                auto it = entityTypes.find(innerType->name);
                if (it != entityTypes.end()) {
                    const FieldLayout *field = it->second.findField(expr->field);
                    if (field) {
                        fieldType = field->type;
                        fieldValue = emitFieldLoad(field, base.value);
                    }
                }
            }
        } else if (innerType->kind == TypeKindSem::List) {
            if (expr->field == "count" || expr->field == "size" || expr->field == "length") {
                fieldType = types::integer();
                fieldValue = emitCallRet(Type(Type::Kind::I64), kListCount, {base.value});
            }
        } else if (innerType->kind == TypeKindSem::Map) {
            if (expr->field == "count" || expr->field == "size" || expr->field == "length") {
                fieldType = types::integer();
                fieldValue = emitCallRet(Type(Type::Kind::I64), kMapCount, {base.value});
            }
        } else if (innerType->kind == TypeKindSem::Set) {
            if (expr->field == "count" || expr->field == "size" || expr->field == "length") {
                fieldType = types::integer();
                fieldValue = emitCallRet(Type(Type::Kind::I64), kSetCount, {base.value});
            }
        }
    }

    Value optionalValue = Value::null();
    if (fieldType && fieldType->kind == TypeKindSem::Optional) {
        optionalValue = fieldValue;
    } else if (fieldType && fieldType->kind != TypeKindSem::Unknown) {
        optionalValue = emitOptionalWrap(fieldValue, fieldType);
    }

    il::core::Instr storeVal;
    storeVal.op = Opcode::Store;
    storeVal.type = Type(Type::Kind::Ptr);
    storeVal.operands = {resultSlot, optionalValue};
    storeVal.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(storeVal);
    emitBr(mergeIdx);

    setBlock(mergeIdx);
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = Type(Type::Kind::Ptr);
    loadInstr.operands = {resultSlot};
    loadInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), Type(Type::Kind::Ptr)};
}

//=============================================================================
// Try Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerTry(TryExpr *expr) {
    // The ? operator propagates null/error by returning early from the function
    // For now, we implement this for optional types (null propagation)

    auto operand = lowerExpr(expr->operand.get());

    // Create blocks for the null check
    size_t hasValueIdx = createBlock("try.hasvalue");
    size_t returnNullIdx = createBlock("try.returnnull");

    // Check if the value is null (comparing pointer as i64 to 0)
    // First, store the pointer and load as i64 for comparison
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    ptrSlotInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, operand.value};
    storePtrInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    loadAsI64Instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNotNull =
        emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
    emitCBr(isNotNull, hasValueIdx, returnNullIdx);

    // Return null block - return null from the current function
    setBlock(returnNullIdx);
    // For functions returning optional types, return null (0 as pointer)
    // For void functions, we just return void
    if (currentFunc_->retType.kind == Type::Kind::Void) {
        emitRetVoid();
    } else {
        // Return null for optional/pointer return types
        emitRet(Value::constInt(0));
    }

    // Has value block - continue with the unwrapped value
    setBlock(hasValueIdx);

    // Return the operand value (unwrap optionals when needed)
    TypeRef operandType = sema_.typeOf(expr->operand.get());
    if (operandType && operandType->kind == TypeKindSem::Optional) {
        TypeRef innerTypeRef = operandType->innerType();
        if (innerTypeRef)
            return emitOptionalUnwrap(operand.value, innerTypeRef);
    }
    return operand;
}

//=============================================================================
// Force-Unwrap Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerForceUnwrap(ForceUnwrapExpr *expr) {
    auto operand = lowerExpr(expr->operand.get());

    TypeRef operandType = sema_.typeOf(expr->operand.get());
    if (!operandType || operandType->kind != TypeKindSem::Optional) {
        // Sema should have caught this; fall through as identity
        return operand;
    }

    TypeRef innerType = operandType->innerType();
    if (!innerType)
        return operand;

    // Null check: store pointer, load as i64, compare != 0
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    ptrSlotInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, operand.value};
    storePtrInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    loadAsI64Instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    size_t unwrapOkIdx = createBlock("unwrap.ok");
    size_t unwrapFailIdx = createBlock("unwrap.fail");

    Value isNotNull =
        emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
    emitCBr(isNotNull, unwrapOkIdx, unwrapFailIdx);

    // Trap block -- abort if null
    setBlock(unwrapFailIdx);
    il::core::Instr trapInstr;
    trapInstr.op = Opcode::Trap;
    trapInstr.type = Type(Type::Kind::Void);
    trapInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(trapInstr);

    // Continue with unwrapped value
    setBlock(unwrapOkIdx);
    return emitOptionalUnwrap(operand.value, innerType);
}

//=============================================================================
// Await Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerAwait(AwaitExpr *expr) {
    // Lower the future-producing operand expression.
    auto futureResult = lowerExpr(expr->operand.get());

    // Emit call to Viper.Threads.Future.Get(future) which blocks until resolved.
    Value result = emitCallRet(Type(Type::Kind::Ptr), runtime::kFutureGet, {futureResult.value});

    TypeRef awaitedType = sema_.typeOf(expr);
    if (!awaitedType || awaitedType->kind == TypeKindSem::Any ||
        awaitedType->kind == TypeKindSem::Unknown || awaitedType->kind == TypeKindSem::Void)
        return {result, Type(Type::Kind::Ptr)};

    Type ilType = mapType(awaitedType);
    if (awaitedType->kind == TypeKindSem::Struct || ilType.kind != Type::Kind::Ptr)
        return emitUnboxValue(result, ilType, awaitedType);
    return {result, ilType};
}

} // namespace il::frontends::zia
