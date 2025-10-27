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

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace viper::codegen::x64
{

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

/// \brief Descriptor tying an opcode to its textual mnemonic and operand policy.
struct EmitterSpec
{
    MOpcode op;             ///< Opcode handled by this specification.
    const char *mnem;       ///< Canonical mnemonic without condition suffixes.
    OperandOrder order;     ///< Operand emission policy for the opcode.
    std::uint32_t flags;    ///< Reserved for width/sign/predicate metadata.
};

/// \brief Locate the emitter specification for a given opcode.
[[nodiscard]] const EmitterSpec *lookup_emitter_spec(MOpcode op) noexcept;

} // namespace viper::codegen::x64
