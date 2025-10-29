// src/codegen/x86_64/AsmEmitter.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Declare the x86-64 assembly emitter responsible for translating
//          Machine IR into textual AT&T syntax and managing associated
//          read-only data pools.
// Invariants: Emission routines preserve the operand ordering semantics of the
//             Machine IR while adapting them to the two-operand x86 encoding;
//             rodata pools deduplicate literals and provide stable labels.
// Ownership: AsmEmitter holds a non-owning reference to a mutable rodata pool;
//            clients retain ownership of pools and streams.
// Notes: Standalone header depending only on MachineIR.hpp and the C++
//        standard library.

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

    static void emitBlock(std::ostream &os, const MBasicBlock &block, const TargetInfo &target);
    static void emitInstruction(std::ostream &os, const MInstr &instr, const TargetInfo &target);
    static void emit_from_row(const EncodingRow &row,
                              std::span<const Operand> operands,
                              std::ostream &os,
                              const TargetInfo &target);

    [[nodiscard]] static std::string formatOperand(const Operand &operand,
                                                   const TargetInfo &target);
    [[nodiscard]] static std::string formatReg(const OpReg &reg, const TargetInfo &target);
    [[nodiscard]] static std::string formatReg8(const OpReg &reg, const TargetInfo &target);
    [[nodiscard]] static std::string formatImm(const OpImm &imm);
    [[nodiscard]] static std::string formatMem(const OpMem &mem, const TargetInfo &target);
    [[nodiscard]] static std::string formatLabel(const OpLabel &label);
    [[nodiscard]] static std::string formatRipLabel(const OpRipLabel &label);
    [[nodiscard]] static std::string formatShiftCount(const Operand &operand,
                                                      const TargetInfo &target);

    [[nodiscard]] static std::string formatLeaSource(const Operand &operand,
                                                     const TargetInfo &target);
    [[nodiscard]] static std::string formatCallTarget(const Operand &operand,
                                                      const TargetInfo &target);

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
