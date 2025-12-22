//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Emit.cpp
/// @brief Instruction emission and helpers for the ViperLang IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Lowerer.hpp"
#include "frontends/viperlang/RuntimeNames.hpp"
#include <cctype>

namespace il::frontends::viperlang
{

using namespace runtime;

//=============================================================================
// Block Management
//=============================================================================

size_t Lowerer::createBlock(const std::string &base)
{
    return blockMgr_.createBlock(base);
}

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
            Value unboxed = emitCallRet(Type(Type::Kind::I1), kUnboxI1, {boxed});
            return {unboxed, Type(Type::Kind::I1)};
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

Lowerer::Value Lowerer::emitOptionalWrap(Value val, TypeRef innerType)
{
    Type ilType = mapType(innerType);
    if (ilType.kind == Type::Kind::Ptr)
        return val;
    return emitBox(val, ilType);
}

LowerResult Lowerer::emitOptionalUnwrap(Value val, TypeRef innerType)
{
    Type ilType = mapType(innerType);
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

//=============================================================================
// Type Mapping
//=============================================================================

Lowerer::Type Lowerer::mapType(TypeRef type)
{
    if (!type)
        return Type(Type::Kind::Void);

    return Type(toILType(*type));
}

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


} // namespace il::frontends::viperlang
