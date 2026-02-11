//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/LowerILToMIR.hpp
// Purpose: Declare a bridge that adapts front-end IL to Machine IR.
// Key invariants: SSA identities are preserved during lowering; virtual register
//                 ids are assigned deterministically starting from 1; block
//                 parameter copies are emitted as parallel copy pseudo-ops.
// Ownership/Lifetime: The adapter borrows IL inputs, constructs fresh MIR graphs by
//                     value, and records call plans for later frame lowering.
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
/// \details Lowering rules receive a MIRBuilder bound to the current block and
///          use it to emit machine instructions, allocate virtual registers, and
///          convert IL values to MIR operands. The builder delegates to the
///          LowerILToMIR adapter for state management (vreg allocation, block
///          info lookup, call plan recording).
class MIRBuilder
{
  public:
    /// @brief Construct a builder targeting a specific block within a lowering session.
    /// @param lower The owning LowerILToMIR adapter (provides vreg allocation, target info).
    /// @param block The machine basic block to append instructions to.
    MIRBuilder(LowerILToMIR &lower, MBasicBlock &block) noexcept;

    /// @brief Get the active machine basic block (mutable).
    /// @return Reference to the block being built.
    [[nodiscard]] MBasicBlock &block() noexcept;

    /// @brief Get the active machine basic block (const).
    /// @return Const reference to the block being built.
    [[nodiscard]] const MBasicBlock &block() const noexcept;

    /// @brief Get the owning LowerILToMIR adapter (mutable).
    /// @return Reference to the adapter managing the lowering session.
    [[nodiscard]] LowerILToMIR &lower() noexcept;

    /// @brief Get the owning LowerILToMIR adapter (const).
    /// @return Const reference to the adapter.
    [[nodiscard]] const LowerILToMIR &lower() const noexcept;

    /// @brief Get the target description (register classes, calling convention, etc.).
    /// @return Const reference to the TargetInfo for x86-64.
    [[nodiscard]] const TargetInfo &target() const noexcept;

    /// @brief Get the read-only data pool for string literals and constants.
    /// @return Reference to the RoDataPool managed by the assembly emitter.
    [[nodiscard]] AsmEmitter::RoDataPool &roData() const noexcept;

    /// @brief Map an IL value kind to the appropriate machine register class.
    /// @param kind The IL value kind (I64, F64, I1, PTR, etc.).
    /// @return The RegClass to use for values of this kind (GPR or XMM).
    [[nodiscard]] RegClass regClassFor(ILValue::Kind kind) const noexcept;

    /// @brief Get or create a virtual register for a given IL SSA identifier.
    /// @details If the SSA id already has a vreg allocated, returns it. Otherwise
    ///          allocates a new vreg of the appropriate register class.
    /// @param id The IL SSA identifier (>= 0).
    /// @param kind The IL value kind, used to determine the register class.
    /// @return The virtual register assigned to this SSA value.
    [[nodiscard]] VReg ensureVReg(int id, ILValue::Kind kind);

    /// @brief Allocate a fresh temporary virtual register.
    /// @param cls The register class for the temporary (GPR or XMM).
    /// @return A newly allocated virtual register.
    [[nodiscard]] VReg makeTempVReg(RegClass cls);

    /// @brief Convert an IL value to a MIR operand (register, immediate, or address).
    /// @details Handles immediates inline. For SSA references, ensures a vreg exists
    ///          and returns a register operand. For labels, returns a label operand.
    /// @param value The IL value to convert.
    /// @param cls The expected register class (used for vreg allocation).
    /// @return The MIR operand representing this value.
    [[nodiscard]] Operand makeOperandForValue(const ILValue &value, RegClass cls);

    /// @brief Convert an IL label value to a MIR label operand.
    /// @param value The IL value (must have kind == LABEL).
    /// @return A label operand referencing the named block.
    [[nodiscard]] Operand makeLabelOperand(const ILValue &value) const;

    /// @brief Check whether an IL value can be represented as an immediate operand.
    /// @param value The IL value to test.
    /// @return True if the value is an inline constant (no vreg needed).
    [[nodiscard]] bool isImmediate(const ILValue &value) const noexcept;

    /// @brief Append a machine instruction to the active basic block.
    /// @param instr The MInstr to append (moved into the block's instruction list).
    void append(MInstr instr);

    /// @brief Record a call lowering plan for later frame lowering.
    /// @details Call plans describe argument passing, return handling, and callee info.
    ///          They are collected during lowering and consumed by the frame builder.
    /// @param plan The call lowering plan to record.
    void recordCallPlan(CallLoweringPlan plan);

  private:
    LowerILToMIR *lower_{nullptr};
    MBasicBlock *block_{nullptr};
};

/// @brief Adapter that lowers temporary IL structures into Machine IR (MIR).
/// @details Processes each IL function by walking its blocks and instructions,
///          assigning virtual registers, emitting machine instructions via
///          lowering rules, and resolving block parameter copies as parallel
///          copy pseudo-ops. The resulting MFunction is ready for register
///          allocation and assembly emission.
class LowerILToMIR
{
  public:
    /// @brief Construct a lowering adapter for the given target and rodata pool.
    /// @param target The x86-64 target description (registers, calling convention).
    /// @param roData The read-only data pool for string literals and constants.
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

    /// @brief Reset all per-function lowering state (vreg counter, maps, call plans).
    void resetFunctionState();

    /// @brief Map an IL value kind to the appropriate machine register class.
    /// @param kind The IL value kind.
    /// @return RegClass::GPR for integer/pointer/bool, RegClass::XMM for floats.
    [[nodiscard]] static RegClass regClassFor(ILValue::Kind kind) noexcept;

    /// @brief Get or create a virtual register for a given IL SSA identifier.
    /// @param id The IL SSA identifier (>= 0).
    /// @param kind The IL value kind for register class selection.
    /// @return The virtual register mapped to this SSA id.
    [[nodiscard]] VReg ensureVReg(int id, ILValue::Kind kind);

    /// @brief Allocate a fresh temporary virtual register in the given class.
    /// @param cls The register class (GPR or XMM).
    /// @return A newly allocated virtual register.
    [[nodiscard]] VReg makeTempVReg(RegClass cls);

    /// @brief Convert an IL value to a MIR operand within a specific block context.
    /// @param block The machine block for context (used for immediate materialization).
    /// @param value The IL value to convert.
    /// @param cls The expected register class.
    /// @return The MIR operand representing this value.
    [[nodiscard]] Operand makeOperandForValue(MBasicBlock &block,
                                              const ILValue &value,
                                              RegClass cls);

    /// @brief Convert an IL label value to a MIR label operand.
    /// @param value The IL value (must be a LABEL kind).
    /// @return A label operand for the named block.
    [[nodiscard]] Operand makeLabelOperand(const ILValue &value) const;

    /// @brief Check if an IL value is an inline immediate (no register needed).
    /// @param value The IL value to test.
    /// @return True if the value can be encoded as an immediate operand.
    [[nodiscard]] bool isImmediate(const ILValue &value) const noexcept;

    /// @brief Emit parallel copy pseudo-ops for block parameter passing along edges.
    /// @details Walks the terminator edges of @p source and emits PCOPY instructions
    ///          that move argument vregs to the destination block's parameter vregs.
    /// @param source The IL block whose terminator edges define the copies.
    /// @param block The machine block to append the copy instructions to.
    void emitEdgeCopies(const ILBlock &source, MBasicBlock &block);
};

} // namespace viper::codegen::x64
