//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/LowerILToMIR.hpp
// Purpose: Declare a bridge that adapts the front-end IL abstraction to the 
// Key invariants: To be documented.
// Ownership/Lifetime: The adapter borrows IL inputs, constructs fresh MIR graphs by
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "AsmEmitter.hpp"
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
        LABEL,
        STR
    };

    Kind kind{Kind::I64};    ///< Static type of the value.
    int id{-1};              ///< SSA identifier (>= 0) or -1 for immediates.
    double f64{0.0};         ///< Payload for floating constants.
    int64_t i64{0};          ///< Payload for integer constants.
    std::string label{};     ///< Payload for label references.
    std::string str{};       ///< Payload for string literal bytes.
    std::uint64_t strLen{0}; ///< Length in bytes for string literals.
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
class LowerILToMIR;

/// \brief Thin façade that exposes MIR construction helpers to lowering rules.
class MIRBuilder
{
  public:
    MIRBuilder(LowerILToMIR &lower, MBasicBlock &block) noexcept;

    [[nodiscard]] MBasicBlock &block() noexcept;
    [[nodiscard]] const MBasicBlock &block() const noexcept;

    [[nodiscard]] LowerILToMIR &lower() noexcept;
    [[nodiscard]] const LowerILToMIR &lower() const noexcept;

    [[nodiscard]] const TargetInfo &target() const noexcept;
    [[nodiscard]] AsmEmitter::RoDataPool &roData() const noexcept;
    [[nodiscard]] RegClass regClassFor(ILValue::Kind kind) const noexcept;

    [[nodiscard]] VReg ensureVReg(int id, ILValue::Kind kind);
    [[nodiscard]] VReg makeTempVReg(RegClass cls);
    [[nodiscard]] Operand makeOperandForValue(const ILValue &value, RegClass cls);
    [[nodiscard]] Operand makeLabelOperand(const ILValue &value) const;
    [[nodiscard]] bool isImmediate(const ILValue &value) const noexcept;

    void append(MInstr instr);
    void recordCallPlan(CallLoweringPlan plan);

  private:
    LowerILToMIR *lower_{nullptr};
    MBasicBlock *block_{nullptr};
};

class LowerILToMIR
{
  public:
    explicit LowerILToMIR(const TargetInfo &target, AsmEmitter::RoDataPool &roData) noexcept;

    /// \brief Lower a single IL function into Machine IR.
    [[nodiscard]] MFunction lower(const ILFunction &func);

    /// \brief Retrieve the collected call lowering plans emitted during lowering.
    [[nodiscard]] const std::vector<CallLoweringPlan> &callPlans() const noexcept;

    friend class MIRBuilder;

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
    AsmEmitter::RoDataPool *roDataPool_{nullptr};

    void resetFunctionState();
    [[nodiscard]] static RegClass regClassFor(ILValue::Kind kind) noexcept;
    [[nodiscard]] VReg ensureVReg(int id, ILValue::Kind kind);
    [[nodiscard]] VReg makeTempVReg(RegClass cls);
    [[nodiscard]] Operand makeOperandForValue(MBasicBlock &block,
                                              const ILValue &value,
                                              RegClass cls);
    [[nodiscard]] Operand makeLabelOperand(const ILValue &value) const;
    [[nodiscard]] bool isImmediate(const ILValue &value) const noexcept;

    void emitEdgeCopies(const ILBlock &source, MBasicBlock &block);
};

} // namespace viper::codegen::x64
