//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Emit.cpp
/// @brief Instruction emission and helpers for the Zia IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "support/alignment.hpp"
#include <algorithm>
#include <cctype>
#include <functional>

namespace il::frontends::zia {

using namespace runtime;

namespace {

bool isInlineAggregateType(TypeRef type) {
    return type && (type->kind == TypeKindSem::Struct || type->kind == TypeKindSem::FixedArray ||
                    type->kind == TypeKindSem::Tuple);
}

} // namespace

//=============================================================================
// Block Management
//=============================================================================

/// @brief Create a new basic block with a unique label derived from @p base.
/// @details Allocates a new block in the current function being lowered. The
///          block is not immediately set as the insertion point; use setBlock()
///          to begin emitting instructions into it.
/// @param base Base name for the block label (e.g., "then", "else", "loop").
/// @return Index of the newly created block.
size_t Lowerer::createBlock(const std::string &base) {
    return blockMgr_.createBlock(base);
}

/// @brief Set the current insertion point to the block at @p blockIdx.
/// @details All subsequent instruction emissions will append to this block
///          until setBlock() is called again with a different index.
/// @param blockIdx Index of the block to make current (from createBlock()).
void Lowerer::setBlock(size_t blockIdx) {
    blockMgr_.setBlock(blockIdx);
}

//=============================================================================
// Instruction Emission Helpers
//=============================================================================

Lowerer::Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs) {
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = op;
    instr.type = ty;
    instr.operands = {lhs, rhs};
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitUnary(Opcode op, Type ty, Value operand) {
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = op;
    instr.type = ty;
    instr.operands = {operand};
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

Lowerer::Value Lowerer::widenByteToInteger(Value value) {
    // Zero-extend i32 to i64 via alloca/store/load pattern
    // Store i32 value, load as i64 (upper bits will be zero due to alloca zeroing)
    unsigned slotId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = slotId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)}; // 8 bytes for i64
    allocaInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value slot = Value::temp(slotId);

    // Store i32 value at offset 0
    emitStore(slot, value, Type(Type::Kind::I32));

    // Load as i64 - the upper 32 bits will be whatever was in memory (likely zero from alloca)
    // To ensure zeroing, we mask after loading
    Value loaded = emitLoad(slot, Type(Type::Kind::I64));
    return emitBinary(Opcode::And, Type(Type::Kind::I64), loaded, Value::constInt(0xFFFFFFFFLL));
}

Lowerer::Value Lowerer::widenIntegralToI64(Value value, Type valueType) {
    switch (valueType.kind) {
        case Type::Kind::I64:
            return value;
        case Type::Kind::I32:
            return widenByteToInteger(value);
        case Type::Kind::I1:
            return emitUnary(Opcode::Zext1, Type(Type::Kind::I64), value);
        default:
            return value;
    }
}

Lowerer::Value Lowerer::emitIndexCheck(Value index, Value lowerInclusive, Value upperExclusive) {
    if (!options_.boundsChecks)
        return index;

    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::IdxChk;
    instr.type = Type(Type::Kind::I64);
    instr.operands = {index, lowerInclusive, upperExclusive};
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitPositiveStepCheck(Value stepValue) {
    if (!options_.boundsChecks)
        return stepValue;

    Value invalid = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), stepValue, Value::constInt(1));
    size_t trapIdx = createBlock("range_step_trap");
    size_t okIdx = createBlock("range_step_ok");
    emitCBr(invalid, trapIdx, okIdx);

    setBlock(trapIdx);
    il::core::Instr trap;
    trap.op = Opcode::Trap;
    trap.type = Type(Type::Kind::Void);
    trap.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(trap));
    blockMgr_.currentBlock()->terminated = true;

    setBlock(okIdx);
    return stepValue;
}

Lowerer::Value Lowerer::narrowIntegerToByte(Value value) {
    return emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), value);
}

LowerResult Lowerer::coerceValueToType(Value value,
                                       Type valueIlType,
                                       TypeRef sourceType,
                                       TypeRef targetType) {
    if (!targetType)
        return {value, valueIlType};

    Type targetIlType = mapType(targetType);
    if (targetType->kind == TypeKindSem::Unit)
        return {Value::null(), targetIlType};

    TypeRef effectiveSource = sourceType;
    bool sourceIsUnknownish = !effectiveSource || effectiveSource->kind == TypeKindSem::Unknown ||
                              effectiveSource->kind == TypeKindSem::Any;

    if (targetType->kind == TypeKindSem::Any) {
        if (valueIlType.kind == Type::Kind::Ptr)
            return {value, Type(Type::Kind::Ptr)};
        TypeRef boxType = (!sourceIsUnknownish && effectiveSource) ? effectiveSource
                                                                   : reverseMapType(valueIlType);
        return {emitBoxValue(value, valueIlType, boxType), Type(Type::Kind::Ptr)};
    }

    if (targetType->kind == TypeKindSem::TypeParam) {
        if (valueIlType.kind == Type::Kind::Ptr)
            return {value, Type(Type::Kind::Ptr)};
        TypeRef boxType = (!sourceIsUnknownish && effectiveSource) ? effectiveSource
                                                                   : reverseMapType(valueIlType);
        return {emitBoxValue(value, valueIlType, boxType), Type(Type::Kind::Ptr)};
    }

    if (sourceIsUnknownish && valueIlType.kind == Type::Kind::Ptr &&
        targetIlType.kind != Type::Kind::Ptr) {
        return emitUnbox(value, targetIlType);
    }

    if (sourceIsUnknownish) {
        effectiveSource = reverseMapType(valueIlType);
    }

    if (targetType->kind == TypeKindSem::Optional) {
        TypeRef innerType = targetType->innerType();
        auto optionalStoreType = [&]() {
            Type optionalIlType = mapType(targetType);
            return value.kind == Value::Kind::NullPtr && optionalIlType.kind == Type::Kind::Str
                       ? Type(Type::Kind::Ptr)
                       : optionalIlType;
        };
        if (effectiveSource && effectiveSource->kind == TypeKindSem::Optional)
            return {value, optionalStoreType()};
        if (innerType) {
            auto coercedInner = coerceValueToType(value, valueIlType, effectiveSource, innerType);
            return {emitOptionalWrap(coercedInner.value, innerType), mapType(targetType)};
        }
        return {value, mapType(targetType)};
    }

    if (targetType->kind == TypeKindSem::Interface && effectiveSource &&
        effectiveSource->kind == TypeKindSem::Struct) {
        const StructTypeInfo *info = getOrCreateStructTypeInfo(effectiveSource->name);
        if (info && info->classId > 0) {
            Value wrapper = emitCallRet(
                Type(Type::Kind::Ptr),
                "rt_obj_new_i64",
                {Value::constInt(static_cast<int64_t>(info->classId)),
                 Value::constInt(static_cast<int64_t>(kClassFieldsOffset + info->totalSize))});
            Value payload = emitGEP(wrapper, static_cast<int64_t>(kClassFieldsOffset));
            emitStructTypeInitialize(*info, payload, value);
            return {wrapper, Type(Type::Kind::Ptr)};
        }
    }

    if (effectiveSource && effectiveSource->kind == TypeKindSem::Unknown &&
        valueIlType.kind == Type::Kind::Ptr && targetIlType.kind != Type::Kind::Ptr) {
        return emitUnbox(value, targetIlType);
    }

    if (effectiveSource) {
        if (effectiveSource->kind == TypeKindSem::Number &&
            targetType->kind == TypeKindSem::Integer) {
            return {emitUnary(Opcode::CastFpToSiRteChk, Type(Type::Kind::I64), value),
                    Type(Type::Kind::I64)};
        }
        if (effectiveSource->kind == TypeKindSem::Integer &&
            targetType->kind == TypeKindSem::Number) {
            return {emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), value), Type(Type::Kind::F64)};
        }
        if (effectiveSource->kind == TypeKindSem::Integer &&
            targetType->kind == TypeKindSem::Byte) {
            return {narrowIntegerToByte(value), Type(Type::Kind::I32)};
        }
        if (effectiveSource->kind == TypeKindSem::Byte &&
            targetType->kind == TypeKindSem::Integer) {
            return {widenByteToInteger(value), Type(Type::Kind::I64)};
        }
        if (effectiveSource->kind == TypeKindSem::Byte && targetType->kind == TypeKindSem::Number) {
            Value widened = widenByteToInteger(value);
            return {emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), widened),
                    Type(Type::Kind::F64)};
        }
    }

    return {value, targetIlType};
}

/// @brief Check if a string-returning call returns a borrowed reference.
/// @details Runtime string accessors should return owned handles. Keep this
///          hook for future borrowed APIs, but default to the owned contract.
static bool isBorrowedStringCall(const std::string &callee) {
    (void)callee;
    return false;
}

Lowerer::Value Lowerer::emitCallRet(Type retTy,
                                    const std::string &callee,
                                    const std::vector<Value> &args) {
    usedExterns_.insert(callee);
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::Call;
    instr.type = retTy;
    instr.callee = callee;
    instr.operands = args;
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(instr);

    Value result = Value::temp(id);

    // Auto-track string-returning calls for deferred release at statement
    // boundary. Excludes borrowed-reference calls (getters/unboxers).
    if (retTy.kind == Type::Kind::Str && !isBorrowedStringCall(callee))
        deferRelease(result, /*isString=*/true);

    return result;
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args) {
    usedExterns_.insert(callee);
    il::core::Instr instr;
    instr.op = Opcode::Call;
    instr.type = Type(Type::Kind::Void);
    instr.callee = callee;
    instr.operands = args;
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(instr);
}

void Lowerer::emitCallIndirect(Value funcPtr, const std::vector<Value> &args) {
    il::core::Instr instr;
    instr.op = Opcode::CallIndirect;
    instr.type = Type(Type::Kind::Void);
    instr.hasIndirectSignature = true;
    instr.indirectRetType = Type(Type::Kind::Void);
    instr.indirectIsVarArg = true;
    // For call.indirect, the function pointer is the first operand
    instr.operands.push_back(funcPtr);
    for (const auto &arg : args) {
        instr.operands.push_back(arg);
    }
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(instr);
}

Lowerer::Value Lowerer::emitCallIndirectRet(Type retTy,
                                            Value funcPtr,
                                            const std::vector<Value> &args) {
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::CallIndirect;
    instr.type = retTy;
    instr.hasIndirectSignature = true;
    instr.indirectRetType = retTy;
    instr.indirectIsVarArg = true;
    // For call.indirect, the function pointer is the first operand
    instr.operands.push_back(funcPtr);
    for (const auto &arg : args) {
        instr.operands.push_back(arg);
    }
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

LowerResult Lowerer::emitCallWithReturn(const std::string &callee,
                                        const std::vector<Value> &args,
                                        Type returnType) {
    if (returnType.kind == Type::Kind::Void) {
        emitCall(callee, args);
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }
    return {emitCallRet(returnType, callee, args), returnType};
}

Lowerer::Value Lowerer::emitToString(Value val, TypeRef sourceType) {
    if (!sourceType)
        return val;

    switch (sourceType->kind) {
        case TypeKindSem::String:
            return val;
        case TypeKindSem::Byte:
            return emitCallRet(Type(Type::Kind::Str), kStringFromInt, {widenByteToInteger(val)});
        case TypeKindSem::Integer:
        case TypeKindSem::Enum:
            return emitCallRet(Type(Type::Kind::Str), kStringFromInt, {val});
        case TypeKindSem::Number:
            return emitCallRet(Type(Type::Kind::Str), kStringFromNum, {val});
        case TypeKindSem::Boolean:
            return emitCallRet(Type(Type::Kind::Str), kFmtBool, {val});
        default:
            return emitCallRet(Type(Type::Kind::Str), kObjectToString, {val});
    }
}

void Lowerer::emitBr(size_t targetIdx) {
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::Br;
    instr.type = Type(Type::Kind::Void);
    instr.labels.push_back(currentFunc_->blocks[targetIdx].label);
    instr.brArgs.push_back({});
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

void Lowerer::emitCBr(Value cond, size_t trueIdx, size_t falseIdx) {
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::CBr;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(cond);
    instr.labels.push_back(currentFunc_->blocks[trueIdx].label);
    instr.labels.push_back(currentFunc_->blocks[falseIdx].label);
    instr.brArgs.push_back({});
    instr.brArgs.push_back({});
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

void Lowerer::emitRet(Value val) {
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(val);
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

void Lowerer::emitRetVoid() {
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    instr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

Lowerer::Value Lowerer::emitConstStr(const std::string &globalName) {
    return builder_->emitConstStr(globalName, curLoc_);
}

Lowerer::Value Lowerer::emitEmptyString() {
    return emitConstStr(getStringGlobal(""));
}

unsigned Lowerer::nextTempId() {
    unsigned id = builder_->reserveTempId();
    if (currentFunc_) {
        if (currentFunc_->valueNames.size() <= id)
            currentFunc_->valueNames.resize(id + 1);
        if (currentFunc_->valueNames[id].empty())
            currentFunc_->valueNames[id] = "%t" + std::to_string(id);
    }
    return id;
}

void Lowerer::nameTemp(unsigned id, const std::string &name) {
    if (!currentFunc_ || name.empty())
        return;
    if (currentFunc_->valueNames.size() <= id)
        currentFunc_->valueNames.resize(id + 1);

    bool nameInUse = false;
    for (size_t i = 0; i < currentFunc_->valueNames.size(); ++i) {
        if (i != id && currentFunc_->valueNames[i] == name) {
            nameInUse = true;
            break;
        }
    }
    currentFunc_->valueNames[id] = nameInUse ? name + "$" + std::to_string(id) : name;
}

//=============================================================================
// Boxing/Unboxing Helpers
//=============================================================================

Lowerer::Value Lowerer::emitBox(Value val, Type type) {
    switch (type.kind) {
        case Type::Kind::I64:
        case Type::Kind::I32:
        case Type::Kind::I16:
            return emitCallRet(Type(Type::Kind::Ptr), kBoxI64, {val});
        case Type::Kind::F64:
            return emitCallRet(Type(Type::Kind::Ptr), kBoxF64, {val});
        case Type::Kind::I1:
            return emitCallRet(Type(Type::Kind::Ptr), kBoxI1, {val});
        case Type::Kind::Str:
            return emitCallRet(Type(Type::Kind::Ptr), kBoxStr, {val});
        case Type::Kind::Ptr:
            // Objects don't need boxing
            return val;
        default:
            return val;
    }
}

Lowerer::Value Lowerer::emitBoxValue(Value val, Type ilType, TypeRef semanticType) {
    // Check if this is a struct type that needs heap allocation
    if (semanticType && semanticType->kind == TypeKindSem::Struct &&
        ilType.kind == Type::Kind::Ptr) {
        // Look up the struct type info
        const StructTypeInfo *info = getOrCreateStructTypeInfo(semanticType->name);
        if (info && info->totalSize > 0) {
            struct ManagedValueField {
                size_t offset;
                int64_t kind;
                int8_t retainNow;
            };

            std::vector<ManagedValueField> managedFields;
            std::function<void(TypeRef, size_t)> collectManagedFields = [&](TypeRef type,
                                                                            size_t baseOffset) {
                if (!type)
                    return;
                if (type->kind == TypeKindSem::Struct) {
                    if (const StructTypeInfo *nested = getOrCreateStructTypeInfo(type->name)) {
                        for (const auto &field : nested->fields)
                            collectManagedFields(field.type, baseOffset + field.offset);
                    }
                    return;
                }
                if (type->kind == TypeKindSem::FixedArray) {
                    TypeRef elemType = type->elementType();
                    const size_t elemSize = getSemanticTypeSize(elemType);
                    for (size_t i = 0; i < type->elementCount; ++i)
                        collectManagedFields(elemType, baseOffset + i * elemSize);
                    return;
                }
                if (type->kind == TypeKindSem::Tuple) {
                    const auto elements = type->tupleElementTypes();
                    for (size_t i = 0; i < elements.size(); ++i)
                        collectManagedFields(elements[i],
                                             baseOffset + getTupleElementOffset(type, i));
                    return;
                }

                if (isStringType(type)) {
                    managedFields.push_back(
                        {baseOffset, /*RT_VALUE_FIELD_STR=*/2, /*retainNow=*/1});
                    return;
                }

                bool objectLike = needsRelease(type);
                if (type->kind == TypeKindSem::Interface || type->kind == TypeKindSem::Function ||
                    type->kind == TypeKindSem::Any)
                    objectLike = true;
                if (objectLike && mapType(type).kind == Type::Kind::Ptr) {
                    managedFields.push_back(
                        {baseOffset, /*RT_VALUE_FIELD_OBJ=*/1, /*retainNow=*/1});
                }
            };
            collectManagedFields(semanticType, 0);

            // Read all field values BEFORE allocating heap memory. The source
            // pointer (val) may point into a callee's C stack frame that was
            // just returned from; in native code that frame is freed on return
            // and will be overwritten by the very next function call
            // (rt_box_value_type → rt_heap_alloc). Reading first keeps all
            // fields in IL temporaries (vregs) that survive the call via
            // callee-save registers or spills into the current frame.
            std::vector<Value> fieldValues;
            fieldValues.reserve(info->fields.size());
            for (const auto &field : info->fields)
                fieldValues.push_back(emitFieldLoad(&field, val));

            // Now it is safe to allocate heap memory
            Value heapPtr = emitCallRet(Type(Type::Kind::Ptr),
                                        kBoxValueType,
                                        {Value::constInt(static_cast<int64_t>(info->totalSize))});

            // Write the pre-read field values into the heap object
            for (size_t i = 0; i < info->fields.size(); ++i)
                emitFieldStore(&info->fields[i], heapPtr, fieldValues[i]);

            for (const auto &field : managedFields) {
                emitCall(kBoxValueTypeAddField,
                         {heapPtr,
                          Value::constInt(static_cast<int64_t>(field.offset)),
                          Value::constInt(field.kind),
                          Value::constInt(field.retainNow)});
            }

            for (size_t i = 0; i < info->fields.size(); ++i) {
                if (isStringType(info->fields[i].type))
                    emitCall(kStrReleaseMaybe, {fieldValues[i]});
            }

            return heapPtr;
        }
    }

    // Fall back to standard boxing
    return emitBox(val, ilType);
}

LowerResult Lowerer::emitUnbox(Value boxed, Type expectedType) {
    switch (expectedType.kind) {
        case Type::Kind::I64:
        case Type::Kind::I32:
        case Type::Kind::I16: {
            Value unboxed = emitCallRet(Type(Type::Kind::I64), kUnboxI64, {boxed});
            return {unboxed, Type(Type::Kind::I64)};
        }
        case Type::Kind::F64: {
            Value unboxed = emitCallRet(Type(Type::Kind::F64), kUnboxF64, {boxed});
            return {unboxed, Type(Type::Kind::F64)};
        }
        case Type::Kind::I1: {
            Value unboxed = emitCallRet(Type(Type::Kind::I1), kUnboxI1, {boxed});
            return {unboxed, Type(Type::Kind::I1)};
        }
        case Type::Kind::Str: {
            Value unboxed = emitCallRet(Type(Type::Kind::Str), kUnboxStr, {boxed});
            return {unboxed, Type(Type::Kind::Str)};
        }
        case Type::Kind::Ptr:
            // Object references don't need unboxing
            return {boxed, Type(Type::Kind::Ptr)};
        default:
            return {boxed, Type(Type::Kind::Ptr)};
    }
}

LowerResult Lowerer::emitUnboxValue(Value boxed, Type ilType, TypeRef semanticType) {
    // Check if this is a struct type that needs copying from heap to stack
    if (semanticType && semanticType->kind == TypeKindSem::Struct &&
        ilType.kind == Type::Kind::Ptr) {
        // Look up the struct type info
        const StructTypeInfo *info = getOrCreateStructTypeInfo(semanticType->name);
        if (info && info->totalSize > 0) {
            // Allocate stack memory for the copy
            Value stackCopy = emitStructTypeCopy(*info, boxed);
            return {stackCopy, Type(Type::Kind::Ptr)};
        }
    }

    // Fall back to standard unboxing
    return emitUnbox(boxed, ilType);
}

Lowerer::Value Lowerer::emitOptionalWrap(Value val, TypeRef innerType) {
    Type ilType = mapType(innerType);
    // Structs map to ptr at the IL level but still have value semantics.
    // Optionals of structs therefore need a boxed heap copy, not a raw
    // stack pointer that would dangle after the current expression returns.
    if (innerType && innerType->kind == TypeKindSem::Struct)
        return emitBoxValue(val, ilType, innerType);
    // Reference-like IL values already use null as the optional sentinel.
    // That includes both object pointers and raw string pointers.
    if (ilType.kind == Type::Kind::Ptr || ilType.kind == Type::Kind::Str)
        return val;
    // Primitive/value payloads need boxing to convert to ptr for Optional representation.
    return emitBox(val, ilType);
}

LowerResult Lowerer::emitOptionalUnwrap(Value val, TypeRef innerType) {
    Type ilType = mapType(innerType);
    // Struct optionals carry boxed heap payloads so force-unwrap / narrowing
    // produces a fresh stack value with normal copy semantics.
    if (innerType && innerType->kind == TypeKindSem::Struct)
        return emitUnboxValue(val, ilType, innerType);
    // Reference-like optional payloads use null to represent None, so the
    // stored IL value is already the underlying value.
    if (ilType.kind == Type::Kind::Ptr || ilType.kind == Type::Kind::Str)
        return {val, ilType};
    // Primitive/value payloads need unboxing from ptr back to their concrete type.
    return emitUnbox(val, ilType);
}

//=============================================================================
// Low-Level Instruction Emission
//=============================================================================

Lowerer::Value Lowerer::emitGEP(Value ptr, int64_t offset) {
    unsigned gepId = nextTempId();
    il::core::Instr gepInstr;
    gepInstr.result = gepId;
    gepInstr.op = Opcode::GEP;
    gepInstr.type = Type(Type::Kind::Ptr);
    gepInstr.operands = {ptr, Value::constInt(offset)};
    gepInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(gepInstr));
    return Value::temp(gepId);
}

Lowerer::Value Lowerer::emitLoad(Value ptr, Type type) {
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = type;
    loadInstr.operands = {ptr};
    loadInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(loadInstr));
    return Value::temp(loadId);
}

void Lowerer::emitStore(Value ptr, Value val, Type type) {
    il::core::Instr storeInstr;
    storeInstr.op = Opcode::Store;
    storeInstr.type = type;
    storeInstr.operands = {ptr, val};
    storeInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(storeInstr));
}

Lowerer::Value Lowerer::emitFieldLoad(const FieldLayout *field, Value selfPtr) {
    Value fieldAddr = emitGEP(selfPtr, static_cast<int64_t>(field->offset));
    if (field->isWeak) {
        Value weakHandle = emitLoad(fieldAddr, Type(Type::Kind::Ptr));
        return emitCallRet(Type(Type::Kind::Ptr), "Viper.Memory.WeakRef.Get", {weakHandle});
    }
    // Inline aggregates live directly inside the containing value. Loading them
    // as a pointer-sized scalar would read the first bytes of the aggregate.
    if (isInlineAggregateType(field->type))
        return fieldAddr;
    Type fieldType = mapType(field->type);
    Value loaded = emitLoad(fieldAddr, fieldType);
    // BUG-ADV-001: Retain loaded string fields to prevent use-after-free.
    // Load gives a borrowed reference; callers may consume the string in
    // concatenation or pass it cross-module, making the borrow dangling.
    if (fieldType.kind == Type::Kind::Str)
        emitCall(runtime::kStrRetainMaybe, {loaded});
    return loaded;
}

void Lowerer::emitFieldStore(const FieldLayout *field, Value selfPtr, Value val) {
    Value fieldAddr = emitGEP(selfPtr, static_cast<int64_t>(field->offset));
    if (field->isWeak) {
        Value oldHandle = emitLoad(fieldAddr, Type(Type::Kind::Ptr));
        Value newHandle = emitCallRet(Type(Type::Kind::Ptr), "Viper.Memory.WeakRef.New", {val});
        emitStore(fieldAddr, newHandle, Type(Type::Kind::Ptr));
        emitCall("Viper.Memory.WeakRef.Free", {oldHandle});
        return;
    }
    emitInlineValueStore(field->type, fieldAddr, val, true);
}

void Lowerer::emitInlineValueStore(TypeRef valueType,
                                   Value destPtr,
                                   Value value,
                                   bool destInitialized) {
    if (isInlineAggregateType(valueType)) {
        emitInlineValueCopy(valueType, destPtr, value, destInitialized);
        return;
    }

    Type ilType = mapType(valueType);
    if (ilType.kind == Type::Kind::Str) {
        if (destInitialized) {
            Value oldValue = emitLoad(destPtr, ilType);
            emitCall(runtime::kStrRetainMaybe, {value});
            emitStore(destPtr, value, ilType);
            emitCall(runtime::kStrReleaseMaybe, {oldValue});
        } else {
            emitCall(runtime::kStrRetainMaybe, {value});
            emitStore(destPtr, value, ilType);
        }
        return;
    }

    emitStore(destPtr, value, ilType);
}

void Lowerer::emitInlineValueCopy(TypeRef valueType,
                                  Value destPtr,
                                  Value sourcePtr,
                                  bool destInitialized) {
    if (!valueType) {
        Value loaded = emitLoad(sourcePtr, Type(Type::Kind::Ptr));
        emitStore(destPtr, loaded, Type(Type::Kind::Ptr));
        return;
    }

    if (valueType->kind == TypeKindSem::Struct) {
        if (const StructTypeInfo *info = getOrCreateStructTypeInfo(valueType->name)) {
            if (destInitialized)
                emitStructTypeStore(*info, destPtr, sourcePtr);
            else
                emitStructTypeInitialize(*info, destPtr, sourcePtr);
            return;
        }
    }

    if (valueType->kind == TypeKindSem::FixedArray) {
        TypeRef elemType = valueType->elementType();
        const size_t elemSize = getSemanticTypeSize(elemType);
        for (size_t i = 0; i < valueType->elementCount; ++i) {
            Value dstElem = i == 0 ? destPtr : emitGEP(destPtr, static_cast<int64_t>(i * elemSize));
            Value srcElem =
                i == 0 ? sourcePtr : emitGEP(sourcePtr, static_cast<int64_t>(i * elemSize));
            emitInlineValueCopy(elemType, dstElem, srcElem, destInitialized);
        }
        return;
    }

    if (valueType->kind == TypeKindSem::Tuple) {
        const auto elements = valueType->tupleElementTypes();
        for (size_t i = 0; i < elements.size(); ++i) {
            const size_t offset = getTupleElementOffset(valueType, i);
            Value dstElem = offset == 0 ? destPtr : emitGEP(destPtr, static_cast<int64_t>(offset));
            Value srcElem =
                offset == 0 ? sourcePtr : emitGEP(sourcePtr, static_cast<int64_t>(offset));
            emitInlineValueCopy(elements[i], dstElem, srcElem, destInitialized);
        }
        return;
    }

    Type ilType = mapType(valueType);
    Value loaded = emitLoad(sourcePtr, ilType);
    emitInlineValueStore(valueType, destPtr, loaded, destInitialized);
}

void Lowerer::emitInlineValueZero(TypeRef valueType, Value destPtr) {
    if (valueType && valueType->kind == TypeKindSem::Struct) {
        if (const StructTypeInfo *info = getOrCreateStructTypeInfo(valueType->name)) {
            for (const auto &field : info->fields) {
                Value fieldAddr = emitGEP(destPtr, static_cast<int64_t>(field.offset));
                emitInlineValueZero(field.type, fieldAddr);
            }
            return;
        }
    }

    if (valueType && valueType->kind == TypeKindSem::FixedArray) {
        TypeRef elemType = valueType->elementType();
        const size_t elemSize = getSemanticTypeSize(elemType);
        for (size_t i = 0; i < valueType->elementCount; ++i) {
            Value elemPtr = i == 0 ? destPtr : emitGEP(destPtr, static_cast<int64_t>(i * elemSize));
            emitInlineValueZero(elemType, elemPtr);
        }
        return;
    }

    if (valueType && valueType->kind == TypeKindSem::Tuple) {
        const auto elements = valueType->tupleElementTypes();
        for (size_t i = 0; i < elements.size(); ++i) {
            const size_t offset = getTupleElementOffset(valueType, i);
            Value elemPtr = offset == 0 ? destPtr : emitGEP(destPtr, static_cast<int64_t>(offset));
            emitInlineValueZero(elements[i], elemPtr);
        }
        return;
    }

    Type ilType = mapType(valueType);
    switch (ilType.kind) {
        case Type::Kind::I64:
        case Type::Kind::I32:
        case Type::Kind::I16:
        case Type::Kind::I1:
            emitStore(destPtr, Value::constInt(0), ilType);
            break;
        case Type::Kind::F64:
            emitStore(destPtr, Value::constFloat(0.0), ilType);
            break;
        case Type::Kind::Str:
            emitStore(destPtr, Value::null(), Type(Type::Kind::Ptr));
            break;
        case Type::Kind::Ptr:
        default:
            emitStore(destPtr, Value::null(), Type(Type::Kind::Ptr));
            break;
    }
}

Lowerer::Value Lowerer::emitInlineValueAlloc(TypeRef valueType) {
    const size_t storageSize = std::max<size_t>(1, getSemanticTypeSize(valueType));
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(static_cast<int64_t>(storageSize))};
    allocaInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(allocaInstr));

    Value destPtr = Value::temp(allocaId);
    emitInlineValueZero(valueType, destPtr);
    return destPtr;
}

Lowerer::Value Lowerer::emitStructTypeCopy(const StructTypeInfo &info, Value sourcePtr) {
    // Allocate stack space for the copy
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(static_cast<int64_t>(info.totalSize))};
    allocaInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value destPtr = Value::temp(allocaId);

    emitStructTypeInitialize(info, destPtr, sourcePtr);
    return destPtr;
}

void Lowerer::emitStructTypeInitialize(const StructTypeInfo &info, Value destPtr, Value sourcePtr) {
    for (const auto &field : info.fields) {
        Value fieldAddr = emitGEP(destPtr, static_cast<int64_t>(field.offset));
        Value srcAddr = emitGEP(sourcePtr, static_cast<int64_t>(field.offset));
        emitInlineValueCopy(field.type, fieldAddr, srcAddr, false);
    }
}

void Lowerer::emitStructTypeStore(const StructTypeInfo &info, Value destPtr, Value sourcePtr) {
    for (const auto &field : info.fields) {
        Value fieldAddr = emitGEP(destPtr, static_cast<int64_t>(field.offset));
        Value srcAddr = emitGEP(sourcePtr, static_cast<int64_t>(field.offset));
        emitInlineValueCopy(field.type, fieldAddr, srcAddr, true);
    }
}

Lowerer::Value Lowerer::emitStructTypeAlloc(const StructTypeInfo &info) {
    // Allocate stack space for the struct type
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(static_cast<int64_t>(info.totalSize))};
    allocaInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value destPtr = Value::temp(allocaId);

    // Zero-initialize all fields
    for (const auto &field : info.fields) {
        Value fieldAddr = emitGEP(destPtr, static_cast<int64_t>(field.offset));
        emitInlineValueZero(field.type, fieldAddr);
    }

    return destPtr;
}

LowerResult Lowerer::materializeCallResult(Value result, TypeRef semanticType, Type ilType) {
    if (semanticType && semanticType->kind == TypeKindSem::Struct &&
        ilType.kind == Type::Kind::Ptr) {
        return emitUnboxValue(result, ilType, semanticType);
    }
    return {result, ilType};
}

//=============================================================================
// Type Mapping
//=============================================================================

void Lowerer::reportLoweringInvariant(il::support::SourceLoc loc,
                                      std::string code,
                                      std::string message) {
    il::support::Diagnostic diag{
        il::support::Severity::Error,
        std::move(message),
        loc,
        std::move(code),
    };
    if (loc.isValid()) {
        diag.range = il::support::SourceRange{
            loc,
            il::support::SourceLoc{loc.file_id, loc.line, loc.column + 1},
        };
    }
    diag.stage = "lower";
    diag.help = "This indicates semantic analysis left an unresolved construct for lowering.";
    diag_.report(std::move(diag));
}

bool Lowerer::isInvalidLoweringType(TypeRef type) const {
    return !type;
}

LowerResult Lowerer::poisonValue(il::support::SourceLoc loc,
                                 std::string code,
                                 std::string message) {
    reportLoweringInvariant(loc, std::move(code), std::move(message));
    return {Value::constInt(0), Type(Type::Kind::Error)};
}

Lowerer::Type Lowerer::mapType(TypeRef type) {
    if (!type) {
        reportLoweringInvariant(
            curLoc_, "V-ZIA-LOWER-MISSING-TYPE", "missing semantic type during lowering");
        return Type(Type::Kind::Void);
    }

    if (isInvalidLoweringType(type)) {
        reportLoweringInvariant(curLoc_,
                                "V-ZIA-LOWER-UNRESOLVED-TYPE",
                                "unresolved semantic type '" + type->toString() +
                                    "' reached lowering");
        return Type(Type::Kind::Error);
    }

    return Type(toILType(*type));
}

TypeRef Lowerer::reverseMapType(Type ilType) {
    switch (ilType.kind) {
        case Type::Kind::I64:
            return types::integer();
        case Type::Kind::F64:
            return types::number();
        case Type::Kind::I1:
            return types::boolean();
        case Type::Kind::Str:
            return types::string();
        case Type::Kind::I32:
        case Type::Kind::I16:
            return types::byte();
        case Type::Kind::Ptr:
            return types::ptr();
        case Type::Kind::Void:
            return types::voidType();
        default:
            return types::unknown();
    }
}

/// @brief Return the size in bytes of an IL type.
/// @details Used for struct layout calculations and GEP offset computation.
///          Sizes follow x86-64 conventions: 8 bytes for pointers and 64-bit
///          integers/floats, 4 for i32, 2 for i16, 1 for i1.
/// @param type IL type to measure.
/// @return Size in bytes.
size_t Lowerer::getILTypeSize(Type type) {
    switch (type.kind) {
        case Type::Kind::I64:
        case Type::Kind::F64:
        case Type::Kind::Ptr:
        case Type::Kind::Str:
            return 8;
        case Type::Kind::I32:
            return 4;
        case Type::Kind::I16:
            return 2;
        case Type::Kind::I1:
            return 1;
        default:
            return 8;
    }
}

/// @brief Return the alignment requirement in bytes for an IL type.
/// @details Alignments follow x86-64 SysV ABI: types align to their natural
///          size, with booleans promoted to 8-byte alignment to prevent
///          misalignment when adjacent to pointer-sized fields.
/// @param type IL type to query.
/// @return Alignment in bytes.
size_t Lowerer::getILTypeAlignment(Type type) {
    // All types align to their size, with a minimum of 8 for pointer-sized types
    // This matches the x86-64 SysV ABI requirements
    switch (type.kind) {
        case Type::Kind::I64:
        case Type::Kind::F64:
        case Type::Kind::Ptr:
        case Type::Kind::Str:
            return 8;
        case Type::Kind::I32:
            return 4;
        case Type::Kind::I16:
            return 2;
        case Type::Kind::I1:
            // Boolean fields should be aligned to 8 bytes to avoid misalignment
            // when followed by 8-byte fields
            return 8;
        default:
            return 8;
    }
}

size_t Lowerer::getSemanticTypeSize(TypeRef type) {
    if (!type)
        return getILTypeSize(Type(Type::Kind::Ptr));

    switch (type->kind) {
        case TypeKindSem::Struct:
            if (const StructTypeInfo *info = getOrCreateStructTypeInfo(type->name))
                return info->totalSize;
            return getILTypeSize(mapType(type));
        case TypeKindSem::FixedArray: {
            TypeRef elemType = type->elementType();
            return getSemanticTypeSize(elemType) * type->elementCount;
        }
        case TypeKindSem::Tuple:
            return getTupleStorageSize(type);
        default:
            return getILTypeSize(mapType(type));
    }
}

size_t Lowerer::getSemanticTypeAlignment(TypeRef type) {
    if (!type)
        return getILTypeAlignment(Type(Type::Kind::Ptr));

    switch (type->kind) {
        case TypeKindSem::Struct: {
            const StructTypeInfo *info = getOrCreateStructTypeInfo(type->name);
            size_t alignment = 1;
            if (info) {
                for (const auto &field : info->fields)
                    alignment = std::max(alignment, getSemanticTypeAlignment(field.type));
                return alignment;
            }
            return getILTypeAlignment(mapType(type));
        }
        case TypeKindSem::FixedArray:
            return getSemanticTypeAlignment(type->elementType());
        case TypeKindSem::Tuple: {
            size_t alignment = 1;
            for (const auto &elemType : type->tupleElementTypes())
                alignment = std::max(alignment, getSemanticTypeAlignment(elemType));
            return alignment;
        }
        default:
            return getILTypeAlignment(mapType(type));
    }
}

size_t Lowerer::getTupleElementOffset(TypeRef tupleType, size_t index) {
    if (!tupleType || tupleType->kind != TypeKindSem::Tuple)
        return 0;

    const auto elements = tupleType->tupleElementTypes();
    size_t offset = 0;
    for (size_t i = 0; i < elements.size(); ++i) {
        offset = alignTo(offset, getSemanticTypeAlignment(elements[i]));
        if (i == index)
            return offset;
        offset += getSemanticTypeSize(elements[i]);
    }
    return offset;
}

size_t Lowerer::getTupleStorageSize(TypeRef tupleType) {
    if (!tupleType || tupleType->kind != TypeKindSem::Tuple)
        return 0;

    const auto elements = tupleType->tupleElementTypes();
    size_t offset = 0;
    size_t alignment = 1;
    for (const auto &elemType : elements) {
        const size_t elemAlignment = getSemanticTypeAlignment(elemType);
        alignment = std::max(alignment, elemAlignment);
        offset = alignTo(offset, elemAlignment);
        offset += getSemanticTypeSize(elemType);
    }
    return alignTo(offset, alignment);
}

/// @brief Round @p offset up to the next multiple of @p alignment.
/// @details Used during struct layout to ensure each field starts at a
///          properly aligned address. Delegates to il::support::alignUp.
/// @param offset Current byte offset to align.
/// @param alignment Required alignment (must be a power of 2).
/// @return Smallest value >= @p offset that is a multiple of @p alignment.
size_t Lowerer::alignTo(size_t offset, size_t alignment) {
    return il::support::alignUp(offset, alignment);
}

//=============================================================================
// Local Variable Management
//=============================================================================

void Lowerer::defineLocal(const std::string &name, Value value) {
    locals_[name] = value;
    if (value.kind == Value::Kind::Temp)
        nameTemp(value.id, name);
}

Lowerer::Value *Lowerer::lookupLocal(const std::string &name) {
    // Check regular locals first
    auto it = locals_.find(name);
    return it != locals_.end() ? &it->second : nullptr;
}

Lowerer::Value Lowerer::createSlot(const std::string &name, Type type) {
    // Allocate stack space for the variable
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)}; // 8 bytes for i64/f64/ptr
    allocaInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);

    Value slot = Value::temp(allocaId);
    nameTemp(allocaId, name);
    slots_[name] = slot;
    return slot;
}

void Lowerer::storeToSlot(const std::string &name, Value value, Type type) {
    auto it = slots_.find(name);
    if (it == slots_.end())
        return;

    il::core::Instr storeInstr;
    storeInstr.op = Opcode::Store;
    storeInstr.type = type;
    storeInstr.operands = {it->second, value};
    storeInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(storeInstr);
}

Lowerer::Value Lowerer::loadFromSlot(const std::string &name, Type type) {
    auto it = slots_.find(name);
    if (it == slots_.end())
        return Value::constInt(0);

    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = type;
    loadInstr.operands = {it->second};
    loadInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return Value::temp(loadId);
}

void Lowerer::removeSlot(const std::string &name) {
    slots_.erase(name);
}

bool Lowerer::getSelfPtr(Value &result) {
    // Check if self is stored in a slot (used in class/struct type methods)
    auto slotIt = slots_.find("self");
    if (slotIt != slots_.end()) {
        result = loadFromSlot("self", Type(Type::Kind::Ptr));
        return true;
    }

    // Check if self is a regular local
    Value *local = lookupLocal("self");
    if (local) {
        result = *local;
        return true;
    }

    return false;
}

//=============================================================================
// Helper Functions
//=============================================================================

std::string Lowerer::mangleFunctionName(const std::string &name) {
    // Entry point is special
    if (name == "start")
        return "main";
    return name;
}

std::string Lowerer::getStringGlobal(const std::string &value) {
    return stringTable_.intern(value);
}

bool Lowerer::equalsIgnoreCase(const std::string &a, const std::string &b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

//=============================================================================
// Deferred Release (Automatic Memory Management)
//=============================================================================

bool Lowerer::needsRelease(TypeRef type) const {
    if (!type)
        return false;
    switch (type->kind) {
        case TypeKindSem::Optional: {
            TypeRef inner = type->innerType();
            if (!inner)
                return false;
            switch (inner->kind) {
                case TypeKindSem::Struct:
                    return false;
                case TypeKindSem::Integer:
                case TypeKindSem::Number:
                case TypeKindSem::Boolean:
                case TypeKindSem::Byte:
                    return true; // Optional primitives are boxed.
                default:
                    return needsRelease(inner);
            }
        }
        case TypeKindSem::String:
        case TypeKindSem::Class:
        case TypeKindSem::List:
        case TypeKindSem::Map:
        case TypeKindSem::Set:
            return true;
        // Ptr with a non-empty name is likely a runtime class (Seq, etc.)
        case TypeKindSem::Ptr:
            return !type->name.empty();
        default:
            return false;
    }
}

bool Lowerer::isStringType(TypeRef type) const {
    if (!type)
        return false;
    if (type->kind == TypeKindSem::Optional)
        return isStringType(type->innerType());
    return type->kind == TypeKindSem::String;
}

Lowerer::Value Lowerer::emitManagedReleaseRet(Value value, bool isString) {
    if (isString) {
        emitCall(kStrReleaseMaybe, {value});
        return Value::constInt(0);
    }

    Value nextRefCount = emitCallRet(Type(Type::Kind::I64), "rt_heap_release_deferred", {value});

    unsigned slotId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = slotId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)};
    allocaInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value resultSlot = Value::temp(slotId);
    emitStore(resultSlot, nextRefCount, Type(Type::Kind::I64));

    Value isZero =
        emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), nextRefCount, Value::constInt(0));
    size_t destroyIdx = createBlock("release_destroy");
    size_t contIdx = createBlock("release_cont");
    emitCBr(isZero, destroyIdx, contIdx);

    setBlock(destroyIdx);
    emitCall("__zia_dtor_dispatch", {value});
    emitCall("rt_obj_free", {value});
    emitBr(contIdx);

    setBlock(contIdx);
    return emitLoad(resultSlot, Type(Type::Kind::I64));
}

void Lowerer::emitManagedRelease(Value value, bool isString) {
    (void)emitManagedReleaseRet(value, isString);
}

void Lowerer::deferRelease(Value v, bool isString) {
    deferredTemps_.push_back({v, isString, blockMgr_.currentBlockIndex()});
}

void Lowerer::consumeDeferred(Value v) {
    if (v.kind != Value::Kind::Temp)
        return;

    // Remove the LAST matching entry (most recently deferred)
    for (auto it = deferredTemps_.rbegin(); it != deferredTemps_.rend(); ++it) {
        if (it->value.kind == Value::Kind::Temp && it->value.id == v.id) {
            // Convert reverse iterator to forward iterator for erase
            deferredTemps_.erase(std::next(it).base());
            return;
        }
    }
}

void Lowerer::releaseDeferredTemps() {
    if (deferredTemps_.empty() || isTerminated()) {
        deferredTemps_.clear();
        return;
    }

    // Only release temps defined in the current block (SSA scoping).
    // Temps from other blocks cannot be referenced here without violating SSA;
    // they are dropped (accepted leak — the proper fix is spilling to alloca).
    size_t curBlock = blockMgr_.currentBlockIndex();
    for (auto &t : deferredTemps_) {
        if (t.blockIdx != curBlock)
            continue;
        emitManagedRelease(t.value, t.isString);
    }
    deferredTemps_.clear();
}

void Lowerer::emitDestructorDispatch() {
    std::vector<std::pair<int, std::string>> destructors;
    destructors.reserve(classTypes_.size());
    for (const auto &[typeName, info] : classTypes_) {
        const std::string dtorName = typeName + ".__dtor";
        if (definedFunctions_.count(dtorName) > 0)
            destructors.emplace_back(info.classId, dtorName);
    }

    std::sort(destructors.begin(), destructors.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
    });

    Function *savedFunc = currentFunc_;
    auto savedLocals = std::move(locals_);
    auto savedSlots = std::move(slots_);
    auto savedLocalTypes = std::move(localTypes_);
    auto savedDeferredTemps = std::move(deferredTemps_);
    TypeRef savedReturnType = currentReturnType_;
    const ClassTypeInfo *savedEntityType = currentClassType_;
    const StructTypeInfo *savedValueType = currentStructType_;

    auto &fn = builder_->startFunction(
        "__zia_dtor_dispatch", Type(Type::Kind::Void), {{"self", Type(Type::Kind::Ptr)}});
    currentFunc_ = &fn;
    currentReturnType_ = types::voidType();
    currentClassType_ = nullptr;
    currentStructType_ = nullptr;
    definedFunctions_.insert("__zia_dtor_dispatch");
    blockMgr_.bind(builder_.get(), &fn);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();
    deferredTemps_.clear();

    builder_->createBlock(fn, "entry_0", fn.params);
    setBlock(fn.blocks.size() - 1);

    Value selfValue = Value::temp(fn.blocks.back().params[0].id);
    if (destructors.empty()) {
        emitRetVoid();
    } else {
        Value classId = emitCallRet(Type(Type::Kind::I64), "rt_obj_class_id", {selfValue});
        size_t defaultIdx = createBlock("dtor_default");
        std::vector<size_t> testBlocks;
        std::vector<size_t> callBlocks;
        testBlocks.reserve(destructors.size());
        callBlocks.reserve(destructors.size());

        for (size_t i = 0; i < destructors.size(); ++i) {
            testBlocks.push_back(i == 0 ? blockMgr_.currentBlockIndex()
                                        : createBlock("dtor_test_" + std::to_string(i)));
            callBlocks.push_back(createBlock("dtor_call_" + std::to_string(i)));
        }

        for (size_t i = 0; i < destructors.size(); ++i) {
            if (i != 0)
                setBlock(testBlocks[i]);
            Value match = emitBinary(Opcode::ICmpEq,
                                     Type(Type::Kind::I1),
                                     classId,
                                     Value::constInt(destructors[i].first));
            size_t falseIdx = (i + 1 < destructors.size()) ? testBlocks[i + 1] : defaultIdx;
            emitCBr(match, callBlocks[i], falseIdx);
            setBlock(callBlocks[i]);
            emitCall(destructors[i].second, {selfValue});
            emitRetVoid();
        }

        setBlock(defaultIdx);
        emitRetVoid();
    }

    currentFunc_ = savedFunc;
    currentReturnType_ = savedReturnType;
    currentClassType_ = savedEntityType;
    currentStructType_ = savedValueType;
    locals_ = std::move(savedLocals);
    slots_ = std::move(savedSlots);
    localTypes_ = std::move(savedLocalTypes);
    deferredTemps_ = std::move(savedDeferredTemps);
    if (savedFunc)
        blockMgr_.bind(builder_.get(), savedFunc);
    else
        blockMgr_.reset(nullptr);
}

} // namespace il::frontends::zia
