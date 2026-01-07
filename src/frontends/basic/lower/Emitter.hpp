//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/lower/Emitter.hpp
// Purpose: Declares the IL emission helper composed by the BASIC lowerer.
// Key invariants: Appends instructions to the active basic block when one is set.
// Ownership/Lifetime: References Lowerer state without owning IR structures.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"
#include "frontends/basic/lower/common/CommonLowering.hpp"

#include "viper/il/Module.hpp"

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
} // namespace il::core

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
    using AstType = il::frontends::basic::Type;

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

    /// @brief Emit an indirect call with a return value.
    Value emitCallIndirectRet(Type ty, Value callee, const std::vector<Value> &args);

    /// @brief Emit an indirect call with no return value.
    void emitCallIndirect(Value callee, const std::vector<Value> &args);

    Value emitConstStr(const std::string &globalName);

    void storeArray(Value slot,
                    Value value,
                    AstType elementType = AstType::I64,
                    bool isObjectArray = false);

    void releaseArrayLocals(const std::unordered_set<std::string> &paramNames);

    void releaseArrayParams(const std::unordered_set<std::string> &paramNames);

    void releaseObjectLocals(const std::unordered_set<std::string> &paramNames);

    void releaseObjectParams(const std::unordered_set<std::string> &paramNames);

    // Temporary lifetime management
    void deferReleaseStr(Value v);
    void deferReleaseObj(Value v, const std::string &className = {});
    void releaseDeferredTemps();
    void clearDeferredTemps();

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
    common::CommonLowering common_;

    struct TempRelease
    {
        Value v;
        bool isString{false};
        std::string className; // optional, for object destructors
    };

    std::vector<TempRelease> deferredTemps_;
};

} // namespace il::frontends::basic::lower
