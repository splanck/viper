// src/codegen/x86_64/MachineIR.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Declare the minimal Machine IR representation required by the
//          x86-64 backend during Phase A bring-up.
// Invariants: Operands capture only the state needed for instruction
//             selection, scheduling, and emission; structures own their data
//             and remain valid until mutated explicitly.
// Ownership: All IR nodes own their contained data outright via value
//            semantics. No implicit sharing occurs.
// Notes: Standalone header — depends only on the C++ standard library and
//        TargetX64.hpp.

#pragma once

#include "TargetX64.hpp"

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <variant>
#include <vector>

namespace viper::codegen::x64
{

/// \brief Identifies a virtual register allocated by the Machine IR builder.
struct VReg
{
    uint16_t id{0U};             ///< Unique id within a function.
    RegClass cls{RegClass::GPR}; ///< Register class constraining the allocation.
};

/// \brief Describes a register operand that may reference a virtual or physical register.
struct OpReg
{
    bool isPhys{false};          ///< True when referencing a physical register.
    RegClass cls{RegClass::GPR}; ///< Register class of the operand.
    uint16_t idOrPhys{0U};       ///< Virtual id (if !isPhys) or PhysReg enum value.
};

/// \brief Immediate operand for integer values.
struct OpImm
{
    int64_t val{0};
};

/// \brief Memory operand using a base register plus displacement (RIP-less).
struct OpMem
{
    OpReg base{};    ///< Base register supplying the address.
    int32_t disp{0}; ///< Signed displacement in bytes.
};

/// \brief Symbolic label operand (basic blocks, functions, jump targets).
struct OpLabel
{
    std::string name{}; ///< Symbol name.
};

/// \brief Union over all supported operand kinds.
using Operand = std::variant<OpReg, OpImm, OpMem, OpLabel>;

/// \brief Enumerates the opcode set required for Phase A.
enum class MOpcode
{
    MOVrr,     ///< Move register to register.
    CMOVNErr,  ///< Conditional move when not equal (register-register).
    MOVri,     ///< Move immediate to register.
    LEA,       ///< Load effective address into register.
    ADDrr,     ///< Add registers.
    ADDri,     ///< Add immediate to register.
    SUBrr,     ///< Subtract registers.
    IMULrr,    ///< Signed multiply registers.
    DIVS64rr,  ///< Signed 64-bit division pseudo (dest <- lhs / rhs).
    REMS64rr,  ///< Signed 64-bit remainder pseudo (dest <- lhs % rhs).
    CQO,       ///< Sign-extend RAX into RDX:RAX.
    IDIVrm,    ///< Signed divide RDX:RAX by the given operand.
    XORrr32,   ///< 32-bit XOR to zero register.
    CMPrr,     ///< Compare registers.
    CMPri,     ///< Compare register with immediate.
    SETcc,     ///< Set byte on condition code.
    MOVZXrr32, ///< Zero-extend 32-bit register to 64-bit.
    TESTrr,    ///< Bitwise test between registers.
    JMP,       ///< Unconditional jump.
    JCC,       ///< Conditional jump.
    CALL,      ///< Call near label or register.
    RET,       ///< Return from function.
    PX_COPY,   ///< Parallel copy pseudo-instruction for phi lowering.
    FADD,      ///< Floating-point add (scalar double).
    FSUB,      ///< Floating-point subtract (scalar double).
    FMUL,      ///< Floating-point multiply (scalar double).
    FDIV,      ///< Floating-point divide (scalar double).
    UCOMIS,    ///< Unordered compare scalar double.
    CVTSI2SD,  ///< Convert signed integer to scalar double.
    CVTTSD2SI, ///< Convert scalar double to signed integer with truncation.
    MOVSDrr,   ///< Move scalar double register to register.
    MOVSDrm,   ///< Move scalar double register to memory.
    MOVSDmr    ///< Move scalar double memory to register.
};

/// \brief Machine instruction: opcode with ordered operands.
struct MInstr
{
    MOpcode opcode{MOpcode::MOVrr};  ///< Opcode for the instruction.
    std::vector<Operand> operands{}; ///< Operands in emission order.

    /// \brief Create an instruction with the given operands.
    [[nodiscard]] static MInstr make(MOpcode opc, std::vector<Operand> ops);

    /// \brief Create an instruction from an initializer list of operands.
    [[nodiscard]] static MInstr make(MOpcode opc, std::initializer_list<Operand> ops = {});

    /// \brief Append an operand and return a reference to enable chaining.
    MInstr &addOperand(Operand op);
};

/// \brief A sequence of machine instructions labelled for control flow.
struct MBasicBlock
{
    std::string label{};                ///< Symbolic label for the block.
    std::vector<MInstr> instructions{}; ///< Ordered list of instructions.

    /// \brief Append an instruction to the block and return a reference to it.
    MInstr &append(MInstr instr);
};

/// \brief Metadata associated with a machine function.
struct FunctionMetadata
{
    bool isVarArg{false}; ///< True when the function accepts variable arguments.
};

/// \brief Machine function: entry name, blocks, and metadata.
struct MFunction
{
    std::string name{};                ///< Symbolic name of the function.
    std::vector<MBasicBlock> blocks{}; ///< Basic blocks forming the body.
    FunctionMetadata metadata{};       ///< Ancillary metadata about the function.

    /// \brief Add a new basic block and return a reference to it.
    MBasicBlock &addBlock(MBasicBlock block);
};

// -----------------------------------------------------------------------------
// Operand helpers
// -----------------------------------------------------------------------------

/// \brief Construct an OpReg representing a virtual register.
[[nodiscard]] OpReg makeVReg(RegClass cls, uint16_t id) noexcept;

/// \brief Construct an OpReg representing a physical register.
[[nodiscard]] OpReg makePhysReg(RegClass cls, uint16_t phys) noexcept;

/// \brief Wrap a virtual register operand into the variant container.
[[nodiscard]] Operand makeVRegOperand(RegClass cls, uint16_t id);

/// \brief Wrap a physical register operand into the variant container.
[[nodiscard]] Operand makePhysRegOperand(RegClass cls, uint16_t phys);

/// \brief Construct an immediate operand.
[[nodiscard]] Operand makeImmOperand(int64_t value);

/// \brief Construct a memory operand from base register and displacement.
[[nodiscard]] Operand makeMemOperand(OpReg base, int32_t disp);

/// \brief Construct a label operand with the provided symbol name.
[[nodiscard]] Operand makeLabelOperand(std::string name);

// -----------------------------------------------------------------------------
// Pretty printing helpers (for debugging only)
// -----------------------------------------------------------------------------

/// \brief Render a register operand to string form.
[[nodiscard]] std::string toString(const OpReg &op);

/// \brief Render an immediate operand to string form.
[[nodiscard]] std::string toString(const OpImm &op);

/// \brief Render a memory operand to string form.
[[nodiscard]] std::string toString(const OpMem &op);

/// \brief Render a label operand to string form.
[[nodiscard]] std::string toString(const OpLabel &op);

/// \brief Render any operand to string form.
[[nodiscard]] std::string toString(const Operand &operand);

/// \brief Render an instruction to string form (opcode + operands).
[[nodiscard]] std::string toString(const MInstr &instr);

/// \brief Render a basic block to string form.
[[nodiscard]] std::string toString(const MBasicBlock &block);

/// \brief Render a function to string form.
[[nodiscard]] std::string toString(const MFunction &func);

} // namespace viper::codegen::x64
