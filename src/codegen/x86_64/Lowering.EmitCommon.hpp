//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Lowering.EmitCommon.hpp
// Purpose: Provide shared emission helpers used by the lowering rule
//          translation units.  The façade centralises register materialisation
//          and instruction construction so that opcode-specific emitters remain
//          focused on sequencing rather than boilerplate.
// Key invariants: Helper routines never mutate MIRBuilder state outside the
//                 provided block and honour the register classes requested by
//                 the caller.  Immediate operands are materialised only when
//                 necessary for the target opcode.
// Ownership/Lifetime: EmitCommon borrows the MIRBuilder reference supplied at
//                     construction time; no ownership of IL or MIR nodes is
//                     transferred.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "LowerILToMIR.hpp"
#include "MachineIR.hpp"

#include <optional>
#include <string_view>

namespace viper::codegen::x64
{

/// @brief Shared helper façade that consolidates common lowering utilities.
class EmitCommon
{
  public:
    /// @brief Construct the helper façade bound to a concrete MIR builder.
    /// @details Stores the provided @p builder pointer so subsequent emission helpers can append
    ///          instructions to the current block without re-threading references through every
    ///          call.  The façade never takes ownership of the builder; callers must ensure the
    ///          builder outlives the helper.
    explicit EmitCommon(MIRBuilder &builder) noexcept;

    /// @brief Access the underlying MIR builder used to append Machine IR.
    /// @details Provides a reference for opcode-specific emitters that need direct access to
    ///          builder primitives.  The helper asserts in debug builds when the façade has not
    ///          been initialised with a valid builder pointer.
    [[nodiscard]] MIRBuilder &builder() const noexcept;

    /// @brief Duplicate an operand while preserving its kind and payload.
    /// @details Materialises a new operand instance, copying register identifiers, immediates,
    ///          or memory operands as needed so the caller can safely mutate the clone without
    ///          affecting the original instruction operand.
    [[nodiscard]] Operand clone(const Operand &operand) const;

    /// @brief Materialise an operand into a register suitable for the requested class.
    /// @details Reuses existing registers when @p operand already matches @p cls, otherwise emits
    ///          the appropriate move or constant materialisation so the result can be consumed by
    ///          register-only opcodes.
    [[nodiscard]] Operand materialise(Operand operand, RegClass cls);

    /// @brief Convenience wrapper that materialises an operand into a general-purpose register.
    /// @details Invokes @ref materialise with @ref RegClass::GPR to keep call sites concise when
    ///          lowering integer arithmetic and address calculations.
    [[nodiscard]] Operand materialiseGpr(Operand operand);

    /// @brief Emit a binary arithmetic operation with register or immediate forms.
    /// @details Selects between the register-register and register-immediate opcode variants,
    ///          materialising immediates when required and updating the MIR builder in-place with
    ///          the resulting instruction.
    void emitBinary(
        const ILInstr &instr, MOpcode opcRR, MOpcode opcRI, RegClass cls, bool requireImm32);

    /// @brief Emit a shift instruction handling both immediate and register counts.
    /// @details Uses @p opcImm when the shift amount is a constant that fits the encoding and
    ///          falls back to @p opcReg after materialising the count into CL when necessary.
    void emitShift(const ILInstr &instr, MOpcode opcImm, MOpcode opcReg);

    /// @brief Emit an integer compare, mapping high-level predicates to X86 condition codes.
    /// @details Materialises operands into the requested register class, emits the compare, and
    ///          returns condition codes via @ref icmpConditionCode when the opcode encodes a
    ///          specific predicate.
    void emitCmp(const ILInstr &instr, RegClass cls, int defaultCond);

    /// @brief Emit a SELECT instruction that implements ternary selection semantics.
    /// @details Materialises the condition and operands, emits the compare, and then builds the
    ///          conditional move or branch sequence required to produce the final value.
    void emitSelect(const ILInstr &instr);

    /// @brief Emit an unconditional branch to the instruction's successor block.
    /// @details Appends the jump to the MIR builder and records the target block so subsequent
    ///          passes can resolve edge metadata.
    void emitBranch(const ILInstr &instr);

    /// @brief Emit a conditional branch mapping IL predicates onto X86 condition codes.
    /// @details Queries @ref icmpConditionCode or @ref fcmpConditionCode to translate the IL
    ///          opcode and then emits the branch pair (compare plus Jcc) while materialising
    ///          operands as required.
    void emitCondBranch(const ILInstr &instr);

    /// @brief Emit a return sequence for the current function.
    /// @details Materialises the return value into the ABI-designated register and appends the
    ///          RET instruction, leaving control-flow to the target lowering pipeline.
    void emitReturn(const ILInstr &instr);

    /// @brief Emit a load from memory into the requested register class.
    /// @details Constructs the addressing mode from IL operands, materialises any index
    ///          registers, and emits the load instruction, returning the destination operand.
    void emitLoad(const ILInstr &instr, RegClass cls);

    /// @brief Emit a store that writes an operand to memory.
    /// @details Materialises the value operand, constructs the memory address, and appends the
    ///          appropriate store instruction into the MIR builder.
    void emitStore(const ILInstr &instr);

    /// @brief Emit a cast operation translating between register classes.
    /// @details Chooses the appropriate opcode @p opc, materialises the source operand into
    ///          @p srcCls, and produces a result in @p dstCls while handling sign/zero extension
    ///          requirements encoded by the opcode.
    void emitCast(const ILInstr &instr, MOpcode opc, RegClass dstCls, RegClass srcCls);

    /// @brief Emit a division or remainder sequence.
    /// @details Recognises whether the IL opcode expects quotient or remainder results and
    ///          materialises operands into the registers mandated by the X86 IDIV/FDIV semantics.
    void emitDivRem(const ILInstr &instr, std::string_view opcode);

    /// @brief Map an integer-compare opcode to the corresponding X86 condition code.
    /// @details Returns `std::nullopt` when the opcode does not carry sufficient predicate
    ///          information, allowing callers to fall back to default codes.
    [[nodiscard]] static std::optional<int> icmpConditionCode(std::string_view opcode) noexcept;

    /// @brief Map a floating-compare opcode to an X86 condition code.
    /// @details Provides the same semantics as @ref icmpConditionCode but operates over the
    ///          floating-point compare opcodes produced by the IL.
    [[nodiscard]] static std::optional<int> fcmpConditionCode(std::string_view opcode) noexcept;

    /// @brief Try to recognise (base + (idx << shift) + disp) addressing.
    /// @details Best-effort pattern matcher that returns an OpMem operand when
    ///          the address producer encodes a base pointer plus a scaled index
    ///          and optional displacement. Future improvements can plumb IL def
    ///          chains; the current implementation conservatively declines when
    ///          insufficient information is available.
    [[nodiscard]] std::optional<Operand> tryMakeIndexedMem(const ILInstr &addrProducer);

  private:
    MIRBuilder *builder_{nullptr};
};

} // namespace viper::codegen::x64
