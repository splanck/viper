//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/common/CommonLowering.hpp
// Purpose: Declare shared IL emission helpers used by BASIC lowering utilities.
// Key invariants: Helpers respect the Lowerer procedure context and only append
//                 instructions to the active block when one is selected.
// Ownership/Lifetime: Borrows Lowerer state; does not own IR objects or AST.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#pragma once

#include "viper/il/Module.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
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

namespace il::frontends::basic::lower::common
{

/// @brief Provides reusable IL emission helpers shared across BASIC lowering components.
/// @invariant Each helper assumes the caller established an active basic block in the
///            @ref Lowerer procedure context before invocation.
/// @ownership Does not own IR objects; borrows Lowerer state that outlives the helper.
class CommonLowering
{
  public:
    using Type = il::core::Type;
    using Value = il::core::Value;
    using BasicBlock = il::core::BasicBlock;
    using Function = il::core::Function;
    using Opcode = il::core::Opcode;

    explicit CommonLowering(Lowerer &lowerer) noexcept;

    [[nodiscard]] Type ilBoolTy() const;

    [[nodiscard]] Value emitBoolConst(bool v);

    [[nodiscard]] Value emitBoolFromBranches(const std::function<void(Value)> &emitThen,
                                             const std::function<void(Value)> &emitElse,
                                             std::string_view thenLabelBase,
                                             std::string_view elseLabelBase,
                                             std::string_view joinLabelBase);

    [[nodiscard]] Value emitAlloca(int bytes);

    [[nodiscard]] Value emitLoad(Type ty, Value addr);

    void emitStore(Type ty, Value addr, Value val);

    [[nodiscard]] Value emitBinary(Opcode op, Type ty, Value lhs, Value rhs);

    [[nodiscard]] Value emitUnary(Opcode op, Type ty, Value val);

    [[nodiscard]] Value emitConstI64(std::int64_t v) const;

    [[nodiscard]] Value emitZext1ToI64(Value val);

    [[nodiscard]] Value emitISub(Value lhs, Value rhs);

    [[nodiscard]] Value emitBasicLogicalI64(Value b1);

    [[nodiscard]] Value emitCheckedNeg(Type ty, Value val);

    void emitBr(BasicBlock *target);

    void emitCBr(Value cond, BasicBlock *t, BasicBlock *f);

    [[nodiscard]] Value emitCallRet(Type ty,
                                    const std::string &callee,
                                    const std::vector<Value> &args);

    void emitCall(const std::string &callee, const std::vector<Value> &args);

    /// @brief Emit an indirect call that returns a value.
    /// @details Appends a `CallIndirect` instruction with the callee operand followed by args.
    [[nodiscard]] Value emitCallIndirectRet(Type ty, Value callee, const std::vector<Value> &args);

    /// @brief Emit an indirect call that does not return a value.
    /// @details Appends a void-typed `CallIndirect` instruction with operands.
    void emitCallIndirect(Value callee, const std::vector<Value> &args);

    [[nodiscard]] Value emitConstStr(const std::string &globalName);

    [[nodiscard]] std::string makeBlockLabel(std::string_view base) const;

  private:
    Lowerer *lowerer_;
};

} // namespace il::frontends::basic::lower::common
