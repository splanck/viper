//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer_Emit.cpp
// Purpose: IL instruction emission helpers for Pascal lowering.
// Key invariants: Produces valid IL instructions with proper typing.
// Ownership/Lifetime: Part of Lowerer; operates on IRBuilder.
//
//===----------------------------------------------------------------------===//

#include "frontends/common/CharUtils.hpp"
#include "frontends/pascal/Lowerer.hpp"
#include "il/core/Instr.hpp"
#include <sstream>

namespace il::frontends::pascal
{

using common::char_utils::toLowercase;

inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// Block Management
//===----------------------------------------------------------------------===//

size_t Lowerer::createBlock(const std::string &base)
{
    std::ostringstream oss;
    oss << base << "_" << blockCounter_++;
    builder_->createBlock(*currentFunc_, oss.str());
    return currentFunc_->blocks.size() - 1;
}

void Lowerer::setBlock(size_t blockIdx)
{
    currentBlockIdx_ = blockIdx;
    builder_->setInsertPoint(currentFunc_->blocks[blockIdx]);
}

std::string Lowerer::getStringGlobal(const std::string &value)
{
    return stringTable_.intern(value);
}

//===----------------------------------------------------------------------===//
// Type Mapping
//===----------------------------------------------------------------------===//

Lowerer::Type Lowerer::mapType(const PasType &pasType)
{
    switch (pasType.kind)
    {
        case PasTypeKind::Void:
            return Type(Type::Kind::Void);
        case PasTypeKind::Integer:
        case PasTypeKind::Enum:
            return Type(Type::Kind::I64);
        case PasTypeKind::Real:
            return Type(Type::Kind::F64);
        case PasTypeKind::Boolean:
            return Type(Type::Kind::I1);
        case PasTypeKind::String:
            return Type(Type::Kind::Str);
        case PasTypeKind::Pointer:
        case PasTypeKind::Class:
        case PasTypeKind::Interface:
        case PasTypeKind::Array:
            return Type(Type::Kind::Ptr);
        case PasTypeKind::Optional:
            if (pasType.innerType && pasType.innerType->isReference())
                return Type(Type::Kind::Ptr);
            return Type(Type::Kind::Ptr);
        case PasTypeKind::Nil:
            return Type(Type::Kind::Ptr);
        default:
            return Type(Type::Kind::I64);
    }
}

int64_t Lowerer::sizeOf(const PasType &pasType)
{
    switch (pasType.kind)
    {
        case PasTypeKind::Integer:
        case PasTypeKind::Enum:
            return 8;
        case PasTypeKind::Real:
            return 8;
        case PasTypeKind::Boolean:
            return 1;
        case PasTypeKind::String:
        case PasTypeKind::Pointer:
        case PasTypeKind::Class:
        case PasTypeKind::Array:
            return 8;
        case PasTypeKind::Interface:
            // Interface is a fat pointer: { objPtr, itablePtr }
            return 16;
        case PasTypeKind::Optional:
            if (pasType.innerType)
                return 8 + sizeOf(*pasType.innerType);
            return 16;
        default:
            return 8;
    }
}

PasType Lowerer::typeOfExpr(const Expr &expr)
{
    // For NameExpr, check our localTypes_ first (which persists after semantic analysis)
    if (expr.kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(expr);
        std::string key = nameExpr.name;
        // Convert to lowercase for case-insensitive lookup
        for (auto &c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        // Check local variables first
        auto it = localTypes_.find(key);
        if (it != localTypes_.end())
        {
            return it->second;
        }

        // Handle 'self' when inside a class method
        if (key == "self" && !currentClassName_.empty())
        {
            return PasType::classType(currentClassName_);
        }

        // Check class fields when inside a class method
        if (!currentClassName_.empty())
        {
            auto *classInfo = sema_->lookupClass(toLower(currentClassName_));
            if (classInfo)
            {
                auto fieldIt = classInfo->fields.find(key);
                if (fieldIt != classInfo->fields.end())
                {
                    return fieldIt->second.type;
                }
            }
        }
    }
    // For FieldExpr, get the base type and look up the field
    else if (expr.kind == ExprKind::Field)
    {
        const auto &fieldExpr = static_cast<const FieldExpr &>(expr);
        if (fieldExpr.base)
        {
            PasType baseType = typeOfExpr(*fieldExpr.base);
            std::string fieldKey = fieldExpr.field;
            for (auto &c : fieldKey)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (baseType.kind == PasTypeKind::Class)
            {
                auto *classInfo = sema_->lookupClass(toLower(baseType.name));
                if (classInfo)
                {
                    auto fieldIt = classInfo->fields.find(fieldKey);
                    if (fieldIt != classInfo->fields.end())
                    {
                        return fieldIt->second.type;
                    }
                }
            }
            else if (baseType.kind == PasTypeKind::Record)
            {
                auto fieldIt = baseType.fields.find(fieldKey);
                if (fieldIt != baseType.fields.end() && fieldIt->second)
                {
                    return *fieldIt->second;
                }
            }
        }
    }
    // Fall back to semantic analyzer for everything else
    // Note: const_cast is safe here as typeOf doesn't actually modify the expression
    return sema_->typeOf(const_cast<Expr &>(expr));
}

//===----------------------------------------------------------------------===//
// Memory Operations
//===----------------------------------------------------------------------===//

Lowerer::Value Lowerer::emitAlloca(int64_t size)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::Alloca;
    instr.type = Type(Type::Kind::Ptr);
    instr.operands.push_back(Value::constInt(size));
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitLoad(Type ty, Value addr)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::Load;
    instr.type = ty;
    instr.operands.push_back(addr);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

void Lowerer::emitStore(Type ty, Value addr, Value val)
{
    il::core::Instr instr;
    instr.op = Opcode::Store;
    instr.type = ty;
    instr.operands.push_back(addr);
    instr.operands.push_back(val);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
}

Lowerer::Value Lowerer::emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = op;
    instr.type = ty;
    instr.operands.push_back(lhs);
    instr.operands.push_back(rhs);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitUnary(Opcode op, Type ty, Value operand)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = op;
    instr.type = ty;
    instr.operands.push_back(operand);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitCallRet(Type retTy,
                                    const std::string &callee,
                                    const std::vector<Value> &args)
{
    // Track runtime externs for later declaration
    if (callee.substr(0, 3) == "rt_")
    {
        usedExterns_.insert(callee);
    }

    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::Call;
    instr.type = retTy;
    instr.callee = callee;
    instr.operands = args;
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

void Lowerer::emitCall(const std::string &callee, const std::vector<Value> &args)
{
    // Track runtime externs for later declaration
    if (callee.substr(0, 3) == "rt_")
    {
        usedExterns_.insert(callee);
    }

    il::core::Instr instr;
    instr.op = Opcode::Call;
    instr.type = Type(Type::Kind::Void);
    instr.callee = callee;
    instr.operands = args;
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
}

void Lowerer::emitBr(size_t targetIdx)
{
    il::core::Instr instr;
    instr.op = Opcode::Br;
    instr.type = Type(Type::Kind::Void);
    instr.labels.push_back(currentFunc_->blocks[targetIdx].label);
    instr.brArgs.push_back({});
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

void Lowerer::emitCBr(Value cond, size_t trueIdx, size_t falseIdx)
{
    il::core::Instr instr;
    instr.op = Opcode::CBr;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(cond);
    instr.labels.push_back(currentFunc_->blocks[trueIdx].label);
    instr.labels.push_back(currentFunc_->blocks[falseIdx].label);
    instr.brArgs.push_back({});
    instr.brArgs.push_back({});
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

void Lowerer::emitRet(Value val)
{
    il::core::Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(val);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

void Lowerer::emitRetVoid()
{
    il::core::Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

Lowerer::Value Lowerer::emitConstStr(const std::string &globalName)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::ConstStr;
    instr.type = Type(Type::Kind::Str);
    instr.operands.push_back(Value::global(globalName));
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

Lowerer::Value Lowerer::emitSitofp(Value intVal)
{
    return emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), intVal);
}

Lowerer::Value Lowerer::emitFptosi(Value floatVal)
{
    return emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), floatVal);
}

Lowerer::Value Lowerer::emitZext1(Value boolVal)
{
    return emitUnary(Opcode::Zext1, Type(Type::Kind::I64), boolVal);
}

Lowerer::Value Lowerer::emitTrunc1(Value intVal)
{
    return emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), intVal);
}

Lowerer::Value Lowerer::emitGep(Value base, Value offset)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::GEP;
    instr.type = Type(Type::Kind::Ptr);
    instr.operands = {base, offset};
    currentBlock()->instructions.push_back(instr);
    return Value::temp(id);
}

unsigned Lowerer::nextTempId()
{
    return builder_->reserveTempId();
}

//===----------------------------------------------------------------------===//
// Exception Handling Lowering
//===----------------------------------------------------------------------===//

size_t Lowerer::createHandlerBlock(const std::string &base)
{
    std::ostringstream oss;
    oss << base << "_" << blockCounter_++;

    // Create block with handler parameters: %err : Error, %tok : ResumeTok
    // IRBuilder::createBlock with params properly assigns IDs and registers names
    std::vector<il::core::Param> params = {{"err", Type(Type::Kind::Error)},
                                           {"tok", Type(Type::Kind::ResumeTok)}};
    BasicBlock &blk = builder_->createBlock(*currentFunc_, oss.str(), params);
    size_t idx = currentFunc_->blocks.size() - 1;

    // Emit eh.entry as the first instruction (required for handler blocks)
    il::core::Instr entry;
    entry.op = Opcode::EhEntry;
    entry.type = Type(Type::Kind::Void);
    entry.loc = {};
    blk.instructions.push_back(entry);

    return idx;
}

void Lowerer::emitEhPush(size_t handlerBlockIdx)
{
    il::core::Instr instr;
    instr.op = Opcode::EhPush;
    instr.type = Type(Type::Kind::Void);
    instr.labels.push_back(currentFunc_->blocks[handlerBlockIdx].label);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
}

void Lowerer::emitEhPop()
{
    il::core::Instr instr;
    instr.op = Opcode::EhPop;
    instr.type = Type(Type::Kind::Void);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
}

void Lowerer::emitResumeSame(Value resumeTok)
{
    il::core::Instr instr;
    instr.op = Opcode::ResumeSame;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(resumeTok);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}

void Lowerer::emitResumeLabel(Value resumeTok, size_t targetBlockIdx)
{
    il::core::Instr instr;
    instr.op = Opcode::ResumeLabel;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(resumeTok);
    instr.labels.push_back(currentFunc_->blocks[targetBlockIdx].label);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    currentBlock()->terminated = true;
}


} // namespace il::frontends::pascal
