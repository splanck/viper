//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/AsmEmitter.hpp
// Purpose: Declare the x86-64 assembly emitter for Machine IR to AT&T syntax.
// Key invariants: Emission preserves operand ordering and branch destinations;
//                 encoding rows are matched deterministically by opcode and
//                 operand pattern; rodata labels are unique per literal kind.
// Ownership/Lifetime: AsmEmitter holds a non-owning reference to a mutable rodata pool;
//                     emit methods write to ostream and do not retain state.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace viper::codegen::x64
{

enum class OperandOrder;
enum class OperandKind;
enum class EncodingForm;
enum class EncodingFlag : std::uint32_t;
struct OperandPattern;
struct EncodingRow;

/// \brief Emits AT&T-style assembly for Machine IR functions and rodata pools.
/// \details Includes support for trap sequences (UD2) and alignment masks (ANDri).
class AsmEmitter
{
  public:
    /// \brief Literal pool owning the module-level .rodata contents.
    class RoDataPool
    {
      public:
        /// \brief Add a byte string literal to the pool, returning its index.
        [[nodiscard]] int addStringLiteral(std::string bytes);

        /// \brief Add a 64-bit floating point literal to the pool, returning its index.
        [[nodiscard]] int addF64Literal(double value);

        /// \brief Retrieve the canonical label for a stored string literal.
        [[nodiscard]] std::string stringLabel(int index) const;

        /// \brief Retrieve the byte length for a stored string literal.
        [[nodiscard]] std::size_t stringByteLength(int index) const;

        /// \brief Retrieve the canonical label for a stored f64 literal.
        [[nodiscard]] std::string f64Label(int index) const;

        /// \brief Emit the .rodata section containing all stored literals.
        void emit(std::ostream &os) const;

        /// \brief Determine whether the pool currently holds any literals.
        [[nodiscard]] bool empty() const noexcept;

      private:
        std::vector<std::string> stringLiterals_{};
        std::vector<std::size_t> stringLengths_{};
        std::vector<double> f64Literals_{};
        std::unordered_map<std::string, int> stringLookup_{};
        std::unordered_map<std::uint64_t, int> f64Lookup_{};
    };

    /// \brief Construct an emitter operating on the provided literal pool.
    explicit AsmEmitter(RoDataPool &pool) noexcept;

    /// \brief Emit the assembly for the supplied Machine IR function.
    void emitFunction(std::ostream &os, const MFunction &func, const TargetInfo &target) const;

    /// \brief Emit the module-level .rodata section once per translation unit.
    void emitRoData(std::ostream &os) const;

    /// \brief Access the underlying rodata pool.
    [[nodiscard]] RoDataPool &roDataPool() noexcept;

    /// \brief Access the underlying rodata pool (const overload).
    [[nodiscard]] const RoDataPool &roDataPool() const noexcept;

  private:
    RoDataPool *pool_{nullptr};

    /// \brief Emit assembly for a single basic block including its label.
    /// \param os Output stream for AT&T syntax assembly.
    /// \param block Machine basic block to emit.
    /// \param target Target information for register naming.
    static void emitBlock(std::ostream &os, const MBasicBlock &block, const TargetInfo &target);

    /// \brief Emit assembly for a single machine instruction.
    /// \param os Output stream for AT&T syntax assembly.
    /// \param instr Machine instruction to emit.
    /// \param target Target information for register naming.
    static void emitInstruction(std::ostream &os, const MInstr &instr, const TargetInfo &target);

    /// \brief Emit instruction text from a matched encoding row.
    /// \param row Encoding specification describing mnemonic and operand ordering.
    /// \param operands Instruction operands to format.
    /// \param os Output stream for assembly text.
    /// \param target Target information for register naming.
    static void emit_from_row(const EncodingRow &row,
                              std::span<const Operand> operands,
                              std::ostream &os,
                              const TargetInfo &target);

    /// \brief Format an operand for AT&T assembly output.
    /// \param operand Operand to format (reg, imm, mem, or label).
    /// \param target Target information for register naming.
    /// \return Formatted string (e.g., "%rax", "$42", "8(%rbp)").
    [[nodiscard]] static std::string formatOperand(const Operand &operand,
                                                   const TargetInfo &target);

    /// \brief Format a 64-bit register operand.
    /// \param reg Register operand.
    /// \param target Target information for register naming.
    /// \return AT&T format register (e.g., "%rax", "%r8").
    [[nodiscard]] static std::string formatReg(const OpReg &reg, const TargetInfo &target);

    /// \brief Format a register as its 8-bit low byte variant.
    /// \param reg Register operand.
    /// \param target Target information for register naming.
    /// \return AT&T format 8-bit register (e.g., "%al", "%r8b").
    [[nodiscard]] static std::string formatReg8(const OpReg &reg, const TargetInfo &target);

    /// \brief Format a register as its 32-bit variant.
    /// \param reg Register operand.
    /// \param target Target information for register naming.
    /// \return AT&T format 32-bit register (e.g., "%eax", "%r8d").
    [[nodiscard]] static std::string formatReg32(const OpReg &reg, const TargetInfo &target);

    /// \brief Format an immediate operand.
    /// \param imm Immediate operand containing integer value.
    /// \return AT&T format immediate (e.g., "$42", "$-1").
    [[nodiscard]] static std::string formatImm(const OpImm &imm);

    /// \brief Format a memory operand with base+displacement addressing.
    /// \param mem Memory operand containing base, index, scale, and displacement.
    /// \param target Target information for register naming.
    /// \return AT&T format memory reference (e.g., "8(%rbp)", "(%rax,%rcx,4)").
    [[nodiscard]] static std::string formatMem(const OpMem &mem, const TargetInfo &target);

    /// \brief Format a symbolic label operand.
    /// \param label Label operand containing symbol name.
    /// \return Label string for use in jumps/calls.
    [[nodiscard]] static std::string formatLabel(const OpLabel &label);

    /// \brief Format a RIP-relative label operand.
    /// \param label RIP-relative label operand.
    /// \return RIP-relative format (e.g., "_symbol(%rip)").
    [[nodiscard]] static std::string formatRipLabel(const OpRipLabel &label);

    /// \brief Format a shift/rotate count operand.
    /// \details Handles both immediate counts and %cl register counts.
    /// \param operand Shift count (immediate or register).
    /// \param target Target information for register naming.
    /// \return Formatted shift count (e.g., "$3", "%cl").
    [[nodiscard]] static std::string formatShiftCount(const Operand &operand,
                                                      const TargetInfo &target);

    /// \brief Format the source operand for LEA instructions.
    /// \param operand LEA source (typically memory-style addressing).
    /// \param target Target information for register naming.
    /// \return Formatted LEA source address expression.
    [[nodiscard]] static std::string formatLeaSource(const Operand &operand,
                                                     const TargetInfo &target);

    /// \brief Format a CALL target operand.
    /// \details Handles direct symbols, indirect registers, and memory targets.
    /// \param operand Call target (label, register, or memory).
    /// \param target Target information for register naming.
    /// \return Formatted call target (e.g., "_func", "*%rax").
    [[nodiscard]] static std::string formatCallTarget(const Operand &operand,
                                                      const TargetInfo &target);

    /// \brief Get the condition code suffix for Jcc/SETcc instructions.
    /// \param code Condition code value (0=EQ, 1=NE, etc.).
    /// \return Condition suffix string (e.g., "e", "ne", "l", "g").
    [[nodiscard]] static std::string_view conditionSuffix(std::int64_t code) noexcept;
};

/// \brief Enumerates operand orderings handled by the emitter table.
enum class OperandOrder
{
    NONE,      ///< Instruction does not print operands.
    DIRECT,    ///< Emit operands exactly as provided.
    R,         ///< Single register operand.
    M,         ///< Single memory operand.
    I,         ///< Single immediate operand.
    R_R,       ///< Destination register with register source.
    R_M,       ///< Destination register with memory source.
    M_R,       ///< Destination memory with register source.
    R_I,       ///< Destination register with immediate source.
    M_I,       ///< Destination memory with immediate source.
    R_R_R,     ///< Three operands following src2, src1, dest ordering.
    SHIFT,     ///< Shift/rotate with specialised count formatting.
    MOVZX_RR8, ///< movzbq-like instruction requiring 8-bit source formatting.
    LEA,       ///< LEA with custom source handling.
    CALL,      ///< CALL-style operand formatting.
    JUMP,      ///< JMP-style operand formatting.
    JCC,       ///< Conditional branch formatting with suffix.
    SETCC      ///< SETcc formatting with suffix.
};

/// \brief Categorises operand variants for encoding table matching.
enum class OperandKind
{
    None,            ///< No operand expected in this slot.
    Reg,             ///< Register operand.
    Imm,             ///< Immediate operand.
    Mem,             ///< Memory operand using base+disp addressing.
    Label,           ///< Symbolic label operand.
    RipLabel,        ///< RIP-relative label operand.
    RegOrMem,        ///< Register or memory operand.
    RegOrImm,        ///< Register or immediate operand.
    LabelOrRegOrMem, ///< Label, RIP label, register, or memory operand.
    Any              ///< Any operand kind accepted.
};

/// \brief Describes high-level encoding forms used to differentiate rows.
enum class EncodingForm
{
    Nullary,   ///< No explicit operands (e.g. CQO, RET).
    Unary,     ///< Single explicit operand (e.g. JMP target).
    RegReg,    ///< Register destination with register source.
    RegImm,    ///< Register destination with immediate source.
    RegMem,    ///< Register destination with memory source.
    MemReg,    ///< Memory destination with register source.
    Lea,       ///< LEA-style operand combination.
    ShiftImm,  ///< Shift/rotate with immediate count.
    ShiftReg,  ///< Shift/rotate with register count (CL).
    Call,      ///< CALL target operand.
    Jump,      ///< JMP target operand.
    Condition, ///< Conditional branch (Jcc) using a predicate immediate.
    Setcc      ///< SETcc predicate with destination operand.
};

/// \brief Flags describing ModRM, SIB, immediate, and prefix requirements.
enum class EncodingFlag : std::uint32_t
{
    None = 0U,
    RequiresModRM = 1U << 0, ///< Instruction consumes a ModRM byte.
    UsesImm8 = 1U << 1,      ///< Instruction encodes an 8-bit immediate.
    UsesImm32 = 1U << 2,     ///< Instruction encodes a 32-bit immediate.
    UsesImm64 = 1U << 3,     ///< Instruction encodes a 64-bit immediate.
    REXW = 1U << 4,          ///< Requires a REX.W prefix.
    UsesCondition = 1U << 5  ///< Reads a condition-code immediate.
};

/// \brief Operand pattern describing the expected operand kinds for a row.
struct OperandPattern
{
    std::array<OperandKind, 3> kinds{}; ///< Ordered operand kinds.
    std::uint8_t count{0U};             ///< Number of operands expected.
};

/// \brief Descriptor tying opcodes to textual mnemonics and operand policies.
struct EncodingRow
{
    MOpcode opcode;            ///< Opcode handled by this specification.
    std::string_view mnemonic; ///< Canonical mnemonic without condition suffixes.
    EncodingForm form;         ///< Encoding form describing operand semantics.
    OperandOrder order;        ///< Operand emission policy for the opcode.
    OperandPattern pattern;    ///< Operand kind pattern used for lookup.
    EncodingFlag flags;        ///< ModRM/SIB/immediate/prefix metadata.
};

/// \brief Locate the encoding row matching an opcode and operand sequence.
[[nodiscard]] const EncodingRow *find_encoding(MOpcode op,
                                               std::span<const Operand> operands) noexcept;

} // namespace viper::codegen::x64
