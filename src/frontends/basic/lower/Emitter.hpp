// File: src/frontends/basic/lower/Emitter.hpp
// Purpose: Declares the IL emission helper composed by the BASIC lowerer.
// Key invariants: Appends instructions to the active basic block when one is set.
// Ownership/Lifetime: References Lowerer state without owning IR structures.
// Links: docs/codemap.md
#pragma once

#include "il/core/Instr.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace il::core
{
class BasicBlock;
class Function;
}

namespace il::frontends::basic
{
class Lowerer;
}

namespace il::frontends::basic::lower
{

/// @brief Centralizes IL emission primitives for BASIC lowering.
/// @invariant Each helper assumes the caller selected an active basic block.
/// @ownership Borrows Lowerer state; does not own emitted IR or runtime data.
class Emitter
{
  public:
    using Type = il::core::Type;
    using Value = il::core::Value;
    using BasicBlock = il::core::BasicBlock;
    using Function = il::core::Function;
    using Opcode = il::core::Opcode;

    explicit Emitter(Lowerer &lowerer) noexcept;

    Type ilBoolTy() const;

    Value emitBoolConst(bool v);

    Value emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                               const std::function<void(Value)> &emitElse,
                               std::string_view thenLabelBase,
                               std::string_view elseLabelBase,
                               std::string_view joinLabelBase);

    Value emitAlloca(int bytes);

    Value emitLoad(Type ty, Value addr);

    void emitStore(Type ty, Value addr, Value val);

    Value emitBinary(Opcode op, Type ty, Value lhs, Value rhs);

    Value emitUnary(Opcode op, Type ty, Value val);

    Value emitConstI64(std::int64_t v);

    Value emitZext1ToI64(Value val);

    Value emitISub(Value lhs, Value rhs);

    Value emitBasicLogicalI64(Value b1);

    Value emitCheckedNeg(Type ty, Value val);

    void emitBr(BasicBlock *target);

    void emitCBr(Value cond, BasicBlock *t, BasicBlock *f);

    Value emitCallRet(Type ty, const std::string &callee, const std::vector<Value> &args);

    void emitCall(const std::string &callee, const std::vector<Value> &args);

    Value emitConstStr(const std::string &globalName);

    void storeArray(Value slot, Value value);

    void releaseArrayLocals(const std::unordered_set<std::string> &paramNames);

    void releaseArrayParams(const std::unordered_set<std::string> &paramNames);

    void releaseObjectLocals(const std::unordered_set<std::string> &paramNames);

    void releaseObjectParams(const std::unordered_set<std::string> &paramNames);

    void emitTrap();

    void emitTrapFromErr(Value errCode);

    void emitEhPush(BasicBlock *handler);
    void emitEhPop();
    void emitEhPopForReturn();
    void clearActiveErrorHandler();
    BasicBlock *ensureErrorHandlerBlock(int targetLine);

    void emitRet(Value v);

    void emitRetVoid();

  private:
    Lowerer &lowerer_;
};

} // namespace il::frontends::basic::lower
