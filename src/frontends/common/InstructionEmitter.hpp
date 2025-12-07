//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/InstructionEmitter.hpp
// Purpose: Common instruction emission helpers for all language frontends.
//
// This provides the core instruction emission methods shared by both BASIC
// and Pascal frontends. Each method constructs an IL instruction and appends
// it to the current basic block.
//
// Key Invariants:
//   - All emit methods produce valid IL instructions
//   - Temp IDs are allocated via the builder's reserveTempId()
//   - Source locations can be optionally attached to instructions
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/support/SourceLoc.hpp"
#include <string>
#include <vector>

namespace il::frontends::common
{

/// @brief Common instruction emission helpers for language frontends.
/// @details Provides methods for emitting IL instructions that are shared
///          across BASIC, Pascal, and future language frontends.
class InstructionEmitter
{
  public:
    using Type = il::core::Type;
    using Value = il::core::Value;
    using Opcode = il::core::Opcode;
    using BasicBlock = il::core::BasicBlock;
    using Function = il::core::Function;

    /// @brief Default constructor creates an unbound emitter.
    InstructionEmitter() = default;

    /// @brief Construct with required context.
    /// @param builder IR builder for temp ID allocation.
    /// @param currentBlock Pointer to pointer of current block (allows updates).
    /// @param currentFunc Current function being lowered.
    InstructionEmitter(il::build::IRBuilder *builder,
                       BasicBlock **currentBlock,
                       Function *currentFunc)
        : builder_(builder), currentBlock_(currentBlock), currentFunc_(currentFunc)
    {
    }

    /// @brief Bind to new context.
    void bind(il::build::IRBuilder *builder, BasicBlock **currentBlock, Function *currentFunc)
    {
        builder_ = builder;
        currentBlock_ = currentBlock;
        currentFunc_ = currentFunc;
    }

    /// @brief Set the current source location for emitted instructions.
    void setLocation(il::support::SourceLoc loc) { currentLoc_ = loc; }

    /// @brief Get the current source location.
    [[nodiscard]] il::support::SourceLoc location() const noexcept { return currentLoc_; }

    // =========================================================================
    // Memory Operations
    // =========================================================================

    /// @brief Emit a stack allocation.
    /// @param size Size in bytes to allocate.
    /// @return Pointer value to the allocated memory.
    [[nodiscard]] Value emitAlloca(int64_t size)
    {
        unsigned id = nextTempId();
        il::core::Instr instr;
        instr.result = id;
        instr.op = Opcode::Alloca;
        instr.type = Type(Type::Kind::Ptr);
        instr.operands.push_back(Value::constInt(size));
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        return Value::temp(id);
    }

    /// @brief Emit a load from memory.
    /// @param ty Type to load.
    /// @param addr Address to load from.
    /// @return Loaded value.
    [[nodiscard]] Value emitLoad(Type ty, Value addr)
    {
        unsigned id = nextTempId();
        il::core::Instr instr;
        instr.result = id;
        instr.op = Opcode::Load;
        instr.type = ty;
        instr.operands.push_back(addr);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        return Value::temp(id);
    }

    /// @brief Emit a store to memory.
    /// @param ty Type being stored.
    /// @param addr Address to store to.
    /// @param val Value to store.
    void emitStore(Type ty, Value addr, Value val)
    {
        il::core::Instr instr;
        instr.op = Opcode::Store;
        instr.type = ty;
        instr.operands.push_back(addr);
        instr.operands.push_back(val);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
    }

    // =========================================================================
    // Arithmetic Operations
    // =========================================================================

    /// @brief Emit a binary operation.
    /// @param op The opcode (e.g., IAdd, FMul).
    /// @param ty Result type.
    /// @param lhs Left operand.
    /// @param rhs Right operand.
    /// @return Result value.
    [[nodiscard]] Value emitBinary(Opcode op, Type ty, Value lhs, Value rhs)
    {
        unsigned id = nextTempId();
        il::core::Instr instr;
        instr.result = id;
        instr.op = op;
        instr.type = ty;
        instr.operands.push_back(lhs);
        instr.operands.push_back(rhs);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        return Value::temp(id);
    }

    /// @brief Emit a unary operation.
    /// @param op The opcode (e.g., Neg, Not).
    /// @param ty Result type.
    /// @param val Operand.
    /// @return Result value.
    [[nodiscard]] Value emitUnary(Opcode op, Type ty, Value val)
    {
        unsigned id = nextTempId();
        il::core::Instr instr;
        instr.result = id;
        instr.op = op;
        instr.type = ty;
        instr.operands.push_back(val);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        return Value::temp(id);
    }

    // =========================================================================
    // Type Conversions
    // =========================================================================

    /// @brief Emit signed integer to floating-point conversion.
    [[nodiscard]] Value emitSitofp(Value intVal)
    {
        return emitUnary(Opcode::Sitofp, Type(Type::Kind::F64), intVal);
    }

    /// @brief Emit floating-point to signed integer conversion.
    [[nodiscard]] Value emitFptosi(Value floatVal)
    {
        return emitUnary(Opcode::Fptosi, Type(Type::Kind::I64), floatVal);
    }

    /// @brief Emit zero-extend from i1 to i64.
    [[nodiscard]] Value emitZext1(Value boolVal)
    {
        return emitUnary(Opcode::Zext1, Type(Type::Kind::I64), boolVal);
    }

    /// @brief Emit truncate from i64 to i1.
    [[nodiscard]] Value emitTrunc1(Value intVal)
    {
        return emitUnary(Opcode::Trunc1, Type(Type::Kind::I1), intVal);
    }

    // =========================================================================
    // Call Instructions
    // =========================================================================

    /// @brief Emit a function call with return value.
    /// @param retTy Return type.
    /// @param callee Name of the function to call.
    /// @param args Arguments to pass.
    /// @return Return value.
    [[nodiscard]] Value emitCallRet(Type retTy, const std::string &callee,
                                    const std::vector<Value> &args)
    {
        unsigned id = nextTempId();
        il::core::Instr instr;
        instr.result = id;
        instr.op = Opcode::Call;
        instr.type = retTy;
        instr.callee = callee;
        instr.operands = args;
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        return Value::temp(id);
    }

    /// @brief Emit a void function call.
    /// @param callee Name of the function to call.
    /// @param args Arguments to pass.
    void emitCall(const std::string &callee, const std::vector<Value> &args)
    {
        il::core::Instr instr;
        instr.op = Opcode::Call;
        instr.type = Type(Type::Kind::Void);
        instr.callee = callee;
        instr.operands = args;
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
    }

    /// @brief Emit an indirect call with return value.
    [[nodiscard]] Value emitCallIndirectRet(Type retTy, Value callee,
                                            const std::vector<Value> &args)
    {
        unsigned id = nextTempId();
        il::core::Instr instr;
        instr.result = id;
        instr.op = Opcode::CallIndirect;
        instr.type = retTy;
        instr.operands.push_back(callee);
        for (const auto &arg : args)
            instr.operands.push_back(arg);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        return Value::temp(id);
    }

    /// @brief Emit a void indirect call.
    void emitCallIndirect(Value callee, const std::vector<Value> &args)
    {
        il::core::Instr instr;
        instr.op = Opcode::CallIndirect;
        instr.type = Type(Type::Kind::Void);
        instr.operands.push_back(callee);
        for (const auto &arg : args)
            instr.operands.push_back(arg);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
    }

    // =========================================================================
    // Control Flow
    // =========================================================================

    /// @brief Emit an unconditional branch (by block index).
    /// @param targetIdx Index of the target block.
    void emitBr(std::size_t targetIdx)
    {
        il::core::Instr instr;
        instr.op = Opcode::Br;
        instr.type = Type(Type::Kind::Void);
        instr.labels.push_back(currentFunc_->blocks[targetIdx].label);
        instr.brArgs.push_back({});
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        block()->terminated = true;
    }

    /// @brief Emit an unconditional branch (by block pointer).
    /// @param target Pointer to the target block.
    void emitBr(BasicBlock *target)
    {
        il::core::Instr instr;
        instr.op = Opcode::Br;
        instr.type = Type(Type::Kind::Void);
        instr.labels.push_back(target->label);
        instr.brArgs.push_back({});
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        block()->terminated = true;
    }

    /// @brief Emit a conditional branch (by block indices).
    /// @param cond Condition value (i1).
    /// @param trueIdx Index of the true target block.
    /// @param falseIdx Index of the false target block.
    void emitCBr(Value cond, std::size_t trueIdx, std::size_t falseIdx)
    {
        il::core::Instr instr;
        instr.op = Opcode::CBr;
        instr.type = Type(Type::Kind::Void);
        instr.operands.push_back(cond);
        instr.labels.push_back(currentFunc_->blocks[trueIdx].label);
        instr.labels.push_back(currentFunc_->blocks[falseIdx].label);
        instr.brArgs.push_back({});
        instr.brArgs.push_back({});
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        block()->terminated = true;
    }

    /// @brief Emit a conditional branch (by block pointers).
    /// @param cond Condition value (i1).
    /// @param trueTarget Pointer to the true target block.
    /// @param falseTarget Pointer to the false target block.
    void emitCBr(Value cond, BasicBlock *trueTarget, BasicBlock *falseTarget)
    {
        il::core::Instr instr;
        instr.op = Opcode::CBr;
        instr.type = Type(Type::Kind::Void);
        instr.operands.push_back(cond);
        instr.labels.push_back(trueTarget->label);
        instr.labels.push_back(falseTarget->label);
        instr.brArgs.push_back({});
        instr.brArgs.push_back({});
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        block()->terminated = true;
    }

    /// @brief Emit a return with value.
    /// @param val Value to return.
    void emitRet(Value val)
    {
        il::core::Instr instr;
        instr.op = Opcode::Ret;
        instr.type = Type(Type::Kind::Void);
        instr.operands.push_back(val);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        block()->terminated = true;
    }

    /// @brief Emit a void return.
    void emitRetVoid()
    {
        il::core::Instr instr;
        instr.op = Opcode::Ret;
        instr.type = Type(Type::Kind::Void);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        block()->terminated = true;
    }

    // =========================================================================
    // String Operations
    // =========================================================================

    /// @brief Emit a constant string reference.
    /// @param globalName Name of the global string.
    /// @return String value.
    [[nodiscard]] Value emitConstStr(const std::string &globalName)
    {
        unsigned id = nextTempId();
        il::core::Instr instr;
        instr.result = id;
        instr.op = Opcode::ConstStr;
        instr.type = Type(Type::Kind::Str);
        instr.operands.push_back(Value::global(globalName));
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        return Value::temp(id);
    }

    // =========================================================================
    // Exception Handling
    // =========================================================================

    /// @brief Emit an EH push instruction (by block index).
    /// @param handlerBlockIdx Index of the handler block.
    void emitEhPush(std::size_t handlerBlockIdx)
    {
        il::core::Instr instr;
        instr.op = Opcode::EhPush;
        instr.type = Type(Type::Kind::Void);
        instr.labels.push_back(currentFunc_->blocks[handlerBlockIdx].label);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
    }

    /// @brief Emit an EH push instruction (by block pointer).
    /// @param handler Pointer to the handler block.
    void emitEhPush(BasicBlock *handler)
    {
        il::core::Instr instr;
        instr.op = Opcode::EhPush;
        instr.type = Type(Type::Kind::Void);
        instr.labels.push_back(handler->label);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
    }

    /// @brief Emit an EH pop instruction.
    void emitEhPop()
    {
        il::core::Instr instr;
        instr.op = Opcode::EhPop;
        instr.type = Type(Type::Kind::Void);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
    }

    /// @brief Emit a resume-same instruction.
    /// @param resumeTok Resume token from handler.
    void emitResumeSame(Value resumeTok)
    {
        il::core::Instr instr;
        instr.op = Opcode::ResumeSame;
        instr.type = Type(Type::Kind::Void);
        instr.operands.push_back(resumeTok);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        block()->terminated = true;
    }

    /// @brief Emit a resume-label instruction (by block index).
    /// @param resumeTok Resume token from handler.
    /// @param targetBlockIdx Index of the target block.
    void emitResumeLabel(Value resumeTok, std::size_t targetBlockIdx)
    {
        il::core::Instr instr;
        instr.op = Opcode::ResumeLabel;
        instr.type = Type(Type::Kind::Void);
        instr.operands.push_back(resumeTok);
        instr.labels.push_back(currentFunc_->blocks[targetBlockIdx].label);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        block()->terminated = true;
    }

    // =========================================================================
    // Miscellaneous
    // =========================================================================

    /// @brief Emit a trap instruction.
    void emitTrap()
    {
        il::core::Instr instr;
        instr.op = Opcode::Trap;
        instr.type = Type(Type::Kind::Void);
        instr.loc = currentLoc_;
        block()->instructions.push_back(std::move(instr));
        block()->terminated = true;
    }

    /// @brief Emit an integer constant.
    /// @param value The integer value.
    /// @return Constant value.
    [[nodiscard]] static Value emitConstI64(std::int64_t value)
    {
        return Value::constInt(value);
    }

    /// @brief Emit a floating-point constant.
    /// @param value The floating-point value.
    /// @return Constant value.
    [[nodiscard]] static Value emitConstF64(double value)
    {
        return Value::constFloat(value);
    }

    /// @brief Reserve the next temp ID from the builder.
    [[nodiscard]] unsigned nextTempId() { return builder_->reserveTempId(); }

  private:
    /// @brief Get the current block.
    [[nodiscard]] BasicBlock *block() const { return *currentBlock_; }

    il::build::IRBuilder *builder_{nullptr};
    BasicBlock **currentBlock_{nullptr};
    Function *currentFunc_{nullptr};
    il::support::SourceLoc currentLoc_{};
};

} // namespace il::frontends::common
