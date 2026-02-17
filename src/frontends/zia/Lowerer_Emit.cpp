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
#include <cctype>

namespace il::frontends::zia
{

using namespace runtime;

//=============================================================================
// Block Management
//=============================================================================

/// @brief Create a new basic block with a unique label derived from @p base.
/// @details Allocates a new block in the current function being lowered. The
///          block is not immediately set as the insertion point; use setBlock()
///          to begin emitting instructions into it.
/// @param base Base name for the block label (e.g., "then", "else", "loop").
/// @return Index of the newly created block.
size_t Lowerer::createBlock(const std::string &base)
{
    return blockMgr_.createBlock(base);
}

/// @brief Set the current insertion point to the block at @p blockIdx.
/// @details All subsequent instruction emissions will append to this block
///          until setBlock() is called again with a different index.
/// @param blockIdx Index of the block to make current (from createBlock()).
void Lowerer::setBlock(size_t blockIdx)
{
    blockMgr_.setBlock(blockIdx);
}

//=============================================================================
// Instruction Emission Helpers
//=============================================================================

Lowerer::Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = op;
    instr.type = ty;
    instr.operands = {lhs, rhs};
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitUnary(Opcode op, Type ty, Value operand)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = op;
    instr.type = ty;
    instr.operands = {operand};
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

Lowerer::Value Lowerer::widenByteToInteger(Value value)
{
    // Zero-extend i32 to i64 via alloca/store/load pattern
    // Store i32 value, load as i64 (upper bits will be zero due to alloca zeroing)
    unsigned slotId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = slotId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)}; // 8 bytes for i64
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value slot = Value::temp(slotId);

    // Store i32 value at offset 0
    emitStore(slot, value, Type(Type::Kind::I32));

    // Load as i64 - the upper 32 bits will be whatever was in memory (likely zero from alloca)
    // To ensure zeroing, we mask after loading
    Value loaded = emitLoad(slot, Type(Type::Kind::I64));
    return emitBinary(Opcode::And, Type(Type::Kind::I64), loaded, Value::constInt(0xFFFFFFFFLL));
}

Lowerer::Value Lowerer::emitCallRet(Type retTy,
                                    const std::string &callee,
                                    const std::vector<Value> &args)
{
    usedExterns_.insert(callee);
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::Call;
    instr.type = retTy;
    instr.callee = callee;
    instr.operands = args;
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    usedExterns_.insert(callee);
    il::core::Instr instr;
    instr.op = Opcode::Call;
    instr.type = Type(Type::Kind::Void);
    instr.callee = callee;
    instr.operands = args;
    blockMgr_.currentBlock()->instructions.push_back(instr);
}

void Lowerer::emitCallIndirect(Value funcPtr, const std::vector<Value> &args)
{
    il::core::Instr instr;
    instr.op = Opcode::CallIndirect;
    instr.type = Type(Type::Kind::Void);
    // For call.indirect, the function pointer is the first operand
    instr.operands.push_back(funcPtr);
    for (const auto &arg : args)
    {
        instr.operands.push_back(arg);
    }
    blockMgr_.currentBlock()->instructions.push_back(instr);
}

Lowerer::Value Lowerer::emitCallIndirectRet(Type retTy,
                                            Value funcPtr,
                                            const std::vector<Value> &args)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::CallIndirect;
    instr.type = retTy;
    // For call.indirect, the function pointer is the first operand
    instr.operands.push_back(funcPtr);
    for (const auto &arg : args)
    {
        instr.operands.push_back(arg);
    }
    blockMgr_.currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

LowerResult Lowerer::emitCallWithReturn(const std::string &callee,
                                        const std::vector<Value> &args,
                                        Type returnType)
{
    if (returnType.kind == Type::Kind::Void)
    {
        emitCall(callee, args);
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }
    return {emitCallRet(returnType, callee, args), returnType};
}

Lowerer::Value Lowerer::emitToString(Value val, TypeRef sourceType)
{
    if (!sourceType)
        return val;

    switch (sourceType->kind)
    {
        case TypeKindSem::String:
            return val;
        case TypeKindSem::Integer:
            return emitCallRet(Type(Type::Kind::Str), kStringFromInt, {val});
        case TypeKindSem::Number:
            return emitCallRet(Type(Type::Kind::Str), kStringFromNum, {val});
        case TypeKindSem::Boolean:
            return emitCallRet(Type(Type::Kind::Str), kFmtBool, {val});
        default:
            return emitCallRet(Type(Type::Kind::Str), kObjectToString, {val});
    }
}

void Lowerer::emitBr(size_t targetIdx)
{
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::Br;
    instr.type = Type(Type::Kind::Void);
    instr.labels.push_back(currentFunc_->blocks[targetIdx].label);
    instr.brArgs.push_back({});
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

void Lowerer::emitCBr(Value cond, size_t trueIdx, size_t falseIdx)
{
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::CBr;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(cond);
    instr.labels.push_back(currentFunc_->blocks[trueIdx].label);
    instr.labels.push_back(currentFunc_->blocks[falseIdx].label);
    instr.brArgs.push_back({});
    instr.brArgs.push_back({});
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

void Lowerer::emitRet(Value val)
{
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(val);
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

void Lowerer::emitRetVoid()
{
    // Use index-based access to avoid stale pointer after vector reallocation
    il::core::Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    blockMgr_.currentBlock()->instructions.push_back(std::move(instr));
    blockMgr_.currentBlock()->terminated = true;
}

Lowerer::Value Lowerer::emitConstStr(const std::string &globalName)
{
    return builder_->emitConstStr(globalName, il::support::SourceLoc{});
}

unsigned Lowerer::nextTempId()
{
    return builder_->reserveTempId();
}

//=============================================================================
// Boxing/Unboxing Helpers
//=============================================================================

Lowerer::Value Lowerer::emitBox(Value val, Type type)
{
    switch (type.kind)
    {
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

Lowerer::Value Lowerer::emitBoxValue(Value val, Type ilType, TypeRef semanticType)
{
    // Check if this is a value type that needs heap allocation
    if (semanticType && semanticType->kind == TypeKindSem::Value && ilType.kind == Type::Kind::Ptr)
    {
        // Look up the value type info
        const ValueTypeInfo *info = getOrCreateValueTypeInfo(semanticType->name);
        if (info && info->totalSize > 0)
        {
            // Allocate heap memory via runtime
            Value heapPtr = emitCallRet(Type(Type::Kind::Ptr),
                                        kBoxValueType,
                                        {Value::constInt(static_cast<int64_t>(info->totalSize))});

            // Copy all fields from stack to heap
            for (const auto &field : info->fields)
            {
                Value srcValue = emitFieldLoad(&field, val);
                emitFieldStore(&field, heapPtr, srcValue);
            }

            return heapPtr;
        }
    }

    // Fall back to standard boxing
    return emitBox(val, ilType);
}

LowerResult Lowerer::emitUnbox(Value boxed, Type expectedType)
{
    switch (expectedType.kind)
    {
        case Type::Kind::I64:
        case Type::Kind::I32:
        case Type::Kind::I16:
        {
            Value unboxed = emitCallRet(Type(Type::Kind::I64), kUnboxI64, {boxed});
            return {unboxed, Type(Type::Kind::I64)};
        }
        case Type::Kind::F64:
        {
            Value unboxed = emitCallRet(Type(Type::Kind::F64), kUnboxF64, {boxed});
            return {unboxed, Type(Type::Kind::F64)};
        }
        case Type::Kind::I1:
        {
            // The runtime function rt_unbox_i1 returns i64 (0 or 1), not i1.
            // Use I64 as the IL return type to match the runtime signature "i64(obj)".
            Value unboxed = emitCallRet(Type(Type::Kind::I64), kUnboxI1, {boxed});
            return {unboxed, Type(Type::Kind::I64)};
        }
        case Type::Kind::Str:
        {
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

LowerResult Lowerer::emitUnboxValue(Value boxed, Type ilType, TypeRef semanticType)
{
    // Check if this is a value type that needs copying from heap to stack
    if (semanticType && semanticType->kind == TypeKindSem::Value && ilType.kind == Type::Kind::Ptr)
    {
        // Look up the value type info
        const ValueTypeInfo *info = getOrCreateValueTypeInfo(semanticType->name);
        if (info && info->totalSize > 0)
        {
            // Allocate stack memory for the copy
            Value stackCopy = emitValueTypeCopy(*info, boxed);
            return {stackCopy, Type(Type::Kind::Ptr)};
        }
    }

    // Fall back to standard unboxing
    return emitUnbox(boxed, ilType);
}

Lowerer::Value Lowerer::emitOptionalWrap(Value val, TypeRef innerType)
{
    Type ilType = mapType(innerType);
    // Reference types (Ptr, Str) are already nullable pointers — wrapping is a no-op.
    if (ilType.kind == Type::Kind::Ptr || ilType.kind == Type::Kind::Str)
        return val;
    return emitBox(val, ilType);
}

LowerResult Lowerer::emitOptionalUnwrap(Value val, TypeRef innerType)
{
    Type ilType = mapType(innerType);
    // Reference types (Ptr, Str) are already the underlying value — no unboxing needed.
    // Optional reference types use null to represent None, so the pointer IS the value.
    if (ilType.kind == Type::Kind::Ptr || ilType.kind == Type::Kind::Str)
        return {val, ilType};
    return emitUnbox(val, ilType);
}

//=============================================================================
// Low-Level Instruction Emission
//=============================================================================

Lowerer::Value Lowerer::emitGEP(Value ptr, int64_t offset)
{
    unsigned gepId = nextTempId();
    il::core::Instr gepInstr;
    gepInstr.result = gepId;
    gepInstr.op = Opcode::GEP;
    gepInstr.type = Type(Type::Kind::Ptr);
    gepInstr.operands = {ptr, Value::constInt(offset)};
    blockMgr_.currentBlock()->instructions.push_back(std::move(gepInstr));
    return Value::temp(gepId);
}

Lowerer::Value Lowerer::emitLoad(Value ptr, Type type)
{
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = type;
    loadInstr.operands = {ptr};
    blockMgr_.currentBlock()->instructions.push_back(std::move(loadInstr));
    return Value::temp(loadId);
}

void Lowerer::emitStore(Value ptr, Value val, Type type)
{
    il::core::Instr storeInstr;
    storeInstr.op = Opcode::Store;
    storeInstr.type = type;
    storeInstr.operands = {ptr, val};
    blockMgr_.currentBlock()->instructions.push_back(std::move(storeInstr));
}

Lowerer::Value Lowerer::emitFieldLoad(const FieldLayout *field, Value selfPtr)
{
    Value fieldAddr = emitGEP(selfPtr, static_cast<int64_t>(field->offset));
    Type fieldType = mapType(field->type);
    return emitLoad(fieldAddr, fieldType);
}

void Lowerer::emitFieldStore(const FieldLayout *field, Value selfPtr, Value val)
{
    Value fieldAddr = emitGEP(selfPtr, static_cast<int64_t>(field->offset));
    Type fieldType = mapType(field->type);
    emitStore(fieldAddr, val, fieldType);
}

Lowerer::Value Lowerer::emitValueTypeCopy(const ValueTypeInfo &info, Value sourcePtr)
{
    // Allocate stack space for the copy
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(static_cast<int64_t>(info.totalSize))};
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value destPtr = Value::temp(allocaId);

    // Copy all fields from source to destination
    for (const auto &field : info.fields)
    {
        Value srcValue = emitFieldLoad(&field, sourcePtr);
        emitFieldStore(&field, destPtr, srcValue);
    }

    return destPtr;
}

Lowerer::Value Lowerer::emitValueTypeAlloc(const ValueTypeInfo &info)
{
    // Allocate stack space for the value type
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(static_cast<int64_t>(info.totalSize))};
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value destPtr = Value::temp(allocaId);

    // Zero-initialize all fields
    for (const auto &field : info.fields)
    {
        Value zeroVal;
        Type fieldType = mapType(field.type);
        switch (fieldType.kind)
        {
            case Type::Kind::I64:
            case Type::Kind::I32:
            case Type::Kind::I16:
            case Type::Kind::I1:
                zeroVal = Value::constInt(0);
                break;
            case Type::Kind::F64:
                zeroVal = Value::constFloat(0.0);
                break;
            case Type::Kind::Str:
                zeroVal = Value::constStr("");
                break;
            case Type::Kind::Ptr:
            default:
                zeroVal = Value::null();
                break;
        }
        emitFieldStore(&field, destPtr, zeroVal);
    }

    return destPtr;
}

//=============================================================================
// Type Mapping
//=============================================================================

Lowerer::Type Lowerer::mapType(TypeRef type)
{
    if (!type)
        return Type(Type::Kind::Void);

    return Type(toILType(*type));
}

TypeRef Lowerer::reverseMapType(Type ilType)
{
    switch (ilType.kind)
    {
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
size_t Lowerer::getILTypeSize(Type type)
{
    switch (type.kind)
    {
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
size_t Lowerer::getILTypeAlignment(Type type)
{
    // All types align to their size, with a minimum of 8 for pointer-sized types
    // This matches the x86-64 SysV ABI requirements
    switch (type.kind)
    {
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

/// @brief Round @p offset up to the next multiple of @p alignment.
/// @details Used during struct layout to ensure each field starts at a
///          properly aligned address. Delegates to il::support::alignUp.
/// @param offset Current byte offset to align.
/// @param alignment Required alignment (must be a power of 2).
/// @return Smallest value >= @p offset that is a multiple of @p alignment.
size_t Lowerer::alignTo(size_t offset, size_t alignment)
{
    return il::support::alignUp(offset, alignment);
}

//=============================================================================
// Local Variable Management
//=============================================================================

void Lowerer::defineLocal(const std::string &name, Value value)
{
    locals_[name] = value;
}

Lowerer::Value *Lowerer::lookupLocal(const std::string &name)
{
    // Check regular locals first
    auto it = locals_.find(name);
    return it != locals_.end() ? &it->second : nullptr;
}

Lowerer::Value Lowerer::createSlot(const std::string &name, Type type)
{
    // Allocate stack space for the variable
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)}; // 8 bytes for i64/f64/ptr
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);

    Value slot = Value::temp(allocaId);
    slots_[name] = slot;
    return slot;
}

void Lowerer::storeToSlot(const std::string &name, Value value, Type type)
{
    auto it = slots_.find(name);
    if (it == slots_.end())
        return;

    il::core::Instr storeInstr;
    storeInstr.op = Opcode::Store;
    storeInstr.type = type;
    storeInstr.operands = {it->second, value};
    blockMgr_.currentBlock()->instructions.push_back(storeInstr);
}

Lowerer::Value Lowerer::loadFromSlot(const std::string &name, Type type)
{
    auto it = slots_.find(name);
    if (it == slots_.end())
        return Value::constInt(0);

    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = type;
    loadInstr.operands = {it->second};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return Value::temp(loadId);
}

void Lowerer::removeSlot(const std::string &name)
{
    slots_.erase(name);
}

bool Lowerer::getSelfPtr(Value &result)
{
    // Check if self is stored in a slot (used in entity/value type methods)
    auto slotIt = slots_.find("self");
    if (slotIt != slots_.end())
    {
        result = loadFromSlot("self", Type(Type::Kind::Ptr));
        return true;
    }

    // Check if self is a regular local
    Value *local = lookupLocal("self");
    if (local)
    {
        result = *local;
        return true;
    }

    return false;
}

//=============================================================================
// Helper Functions
//=============================================================================

std::string Lowerer::mangleFunctionName(const std::string &name)
{
    // Entry point is special
    if (name == "start")
        return "main";
    return name;
}

std::string Lowerer::getStringGlobal(const std::string &value)
{
    return stringTable_.intern(value);
}

bool Lowerer::equalsIgnoreCase(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}


} // namespace il::frontends::zia
