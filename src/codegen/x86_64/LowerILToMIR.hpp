// src/codegen/x86_64/LowerILToMIR.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Declare a bridge that adapts the front-end IL abstraction to the
//          Machine IR (MIR) form consumed by the x86-64 backend during Phase A.
// Invariants: Lowering preserves SSA ids by mapping them to stable virtual
//             registers within a function and materialises PX_COPY nodes for
//             block parameter transfers.
// Ownership: The adapter borrows IL inputs, constructs fresh MIR graphs by
//            value, and retains call plans internally for later lowering.
// Notes: IL structures declared here are temporary scaffolding until the real
//        IL headers are integrated.

#pragma once

#include "CallLowering.hpp"
#include "MachineIR.hpp"
#include "TargetX64.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace viper::codegen::x64
{

/// \brief Minimal IL value placeholder until the canonical IL headers are wired.
struct ILValue
{
    enum class Kind
    {
        I64,
        F64,
        I1,
        PTR,
        LABEL
    };

    Kind kind{Kind::I64}; ///< Static type of the value.
    int id{-1};           ///< SSA identifier (>= 0) or -1 for immediates.
    double f64{0.0};      ///< Payload for floating constants.
    int64_t i64{0};       ///< Payload for integer constants.
    std::string label{};  ///< Payload for label references.
};

/// \brief IL instruction placeholder containing opcode, operands, and result info.
struct ILInstr
{
    std::string opcode{};                         ///< Mnemonic name of the IL opcode.
    std::vector<ILValue> ops{};                   ///< Ordered operands.
    int resultId{-1};                             ///< SSA identifier assigned to the result.
    ILValue::Kind resultKind{ILValue::Kind::I64}; ///< Static type of the result.
};

/// \brief IL basic block placeholder with parameters and outgoing edges.
struct ILBlock
{
    struct EdgeArg
    {
        std::string to{};          ///< Destination block label.
        std::vector<int> argIds{}; ///< SSA ids mapped onto destination params.
    };

    std::string name{};                      ///< Block label.
    std::vector<ILInstr> instrs{};           ///< Instruction body.
    std::vector<int> paramIds{};             ///< SSA ids for block parameters.
    std::vector<ILValue::Kind> paramKinds{}; ///< Kinds for block parameters.
    std::vector<EdgeArg> terminatorEdges{};  ///< Successor edges for block terminators.
};

/// \brief IL function placeholder containing blocks.
struct ILFunction
{
    std::string name{};            ///< Function symbol.
    std::vector<ILBlock> blocks{}; ///< Ordered blocks.
};

/// \brief IL module placeholder containing multiple functions.
struct ILModule
{
    std::vector<ILFunction> funcs{}; ///< Module-level functions.
};

/// \brief Adapter that lowers temporary IL structures into Machine IR.
class LowerILToMIR
{
  public:
    explicit LowerILToMIR(const TargetInfo &target) noexcept;

    /// \brief Lower a single IL function into Machine IR.
    [[nodiscard]] MFunction lower(const ILFunction &func);

    /// \brief Retrieve the collected call lowering plans emitted during lowering.
    [[nodiscard]] const std::vector<CallLoweringPlan> &callPlans() const noexcept;

  private:
    struct BlockInfo
    {
        std::size_t index{0};           ///< Index within the MIR function.
        std::vector<VReg> paramVRegs{}; ///< Destination vregs for block params.
    };

    const TargetInfo *target_{nullptr};
    uint16_t nextVReg_{1};
    std::unordered_map<int, VReg> valueToVReg_{};
    std::unordered_map<std::string, BlockInfo> blockInfo_{};
    std::vector<CallLoweringPlan> callPlans_{};

    void resetFunctionState();
    [[nodiscard]] static RegClass regClassFor(ILValue::Kind kind) noexcept;
    [[nodiscard]] VReg ensureVReg(int id, ILValue::Kind kind);
    [[nodiscard]] Operand makeOperandForValue(MBasicBlock &block,
                                              const ILValue &value,
                                              RegClass cls);
    [[nodiscard]] Operand makeLabelOperand(const ILValue &value) const;
    [[nodiscard]] bool isImmediate(const ILValue &value) const noexcept;

    void lowerInstruction(const ILInstr &instr, MBasicBlock &block);
    void lowerBinary(
        const ILInstr &instr, MBasicBlock &block, MOpcode opcRR, MOpcode opcRI, RegClass cls);
    void lowerCmp(const ILInstr &instr, MBasicBlock &block, RegClass cls);
    void lowerSelect(const ILInstr &instr, MBasicBlock &block);
    void lowerBranch(const ILInstr &instr, MBasicBlock &block);
    void lowerCondBranch(const ILInstr &instr, MBasicBlock &block);
    void lowerReturn(const ILInstr &instr, MBasicBlock &block);
    void lowerCall(const ILInstr &instr, MBasicBlock &block);
    void lowerLoad(const ILInstr &instr, MBasicBlock &block, RegClass cls);
    void lowerStore(const ILInstr &instr, MBasicBlock &block);
    void lowerCast(
        const ILInstr &instr, MBasicBlock &block, MOpcode opc, RegClass dstCls, RegClass srcCls);
    void emitEdgeCopies(const ILBlock &source, MBasicBlock &block);
};

} // namespace viper::codegen::x64
