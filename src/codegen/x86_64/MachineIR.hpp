//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/MachineIR.hpp
// Purpose: Declare the minimal Machine IR representation for x86-64 codegen.
// Key invariants: Operand lists preserve emission order; virtual registers are
//                 unique per function (ids start at 1); physical registers use
//                 PhysReg enum values; block labels are unique within a function.
// Ownership/Lifetime: All IR nodes own their contained data outright via value
//                     semantics (vectors, strings); no external resource ownership.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "TargetX64.hpp"

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
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

/// \brief Memory operand using a base (+ optional index*scale) plus displacement (RIP-less).
struct OpMem
{
    OpReg base{};         ///< Base register supplying the address.
    OpReg index{};        ///< Optional index register (cls must be GPR when used).
    uint8_t scale{1};     ///< Scale for the index (1, 2, 4, 8).
    int32_t disp{0};      ///< Signed displacement in bytes.
    bool hasIndex{false}; ///< True when index participates.
};

/// \brief Symbolic label operand (basic blocks, functions, jump targets).
struct OpLabel
{
    std::string name{}; ///< Symbol name.
};

/// \brief RIP-relative label operand representing a memory reference without a base register.
struct OpRipLabel
{
    std::string name{}; ///< Symbol name referenced relative to RIP.
};

/// \brief Union over all supported operand kinds.
using Operand = std::variant<OpReg, OpImm, OpMem, OpLabel, OpRipLabel>;

/// \brief Enumerates the opcode set required for Phase A.
enum class MOpcode
{
    MOVrr,     ///< Move register to register.
    MOVrm,     ///< Move register to memory.
    MOVmr,     ///< Move memory to register.
    CMOVNErr,  ///< Conditional move when not equal (register-register).
    MOVri,     ///< Move immediate to register.
    LEA,       ///< Load effective address into register.
    ADDrr,     ///< Add registers.
    ADDri,     ///< Add immediate to register.
    ANDrr,     ///< Bitwise AND register with register.
    ANDri,     ///< Bitwise AND register with immediate.
    ORrr,      ///< Bitwise OR register with register.
    ORri,      ///< Bitwise OR register with immediate.
    XORrr,     ///< Bitwise XOR register with register.
    XORri,     ///< Bitwise XOR register with immediate.
    SUBrr,     ///< Subtract registers.
    SHLri,     ///< Shift left by immediate (imm8).
    SHLrc,     ///< Shift left by CL register.
    SHRri,     ///< Logical shift right by immediate (imm8).
    SHRrc,     ///< Logical shift right by CL register.
    SARri,     ///< Arithmetic shift right by immediate (imm8).
    SARrc,     ///< Arithmetic shift right by CL register.
    IMULrr,    ///< Signed multiply registers.
    DIVS64rr,  ///< Signed 64-bit division pseudo (dest <- lhs / rhs).
    REMS64rr,  ///< Signed 64-bit remainder pseudo (dest <- lhs % rhs).
    DIVU64rr,  ///< Unsigned 64-bit division pseudo (dest <- lhs / rhs).
    REMU64rr,  ///< Unsigned 64-bit remainder pseudo (dest <- lhs % rhs).
    CQO,       ///< Sign-extend RAX into RDX:RAX.
    IDIVrm,    ///< Signed divide RDX:RAX by the given operand.
    DIVrm,     ///< Unsigned divide RDX:RAX by the given operand.
    XORrr32,   ///< 32-bit XOR to zero register.
    CMPrr,     ///< Compare registers.
    CMPri,     ///< Compare register with immediate.
    SETcc,     ///< Set byte on condition code.
    MOVZXrr32, ///< Zero-extend 32-bit register to 64-bit.
    TESTrr,    ///< Bitwise test between registers.
    JMP,       ///< Unconditional jump.
    JCC,       ///< Conditional jump.
    LABEL,     ///< In-block label definition.
    CALL,      ///< Call near label or register.
    UD2,       ///< Undefined instruction used to flag hard failures (alignment trap).
    RET,       ///< Return from function.
    PX_COPY,   ///< Parallel copy pseudo-instruction for phi lowering.
    FADD,      ///< Floating-point add (scalar double).
    FSUB,      ///< Floating-point subtract (scalar double).
    FMUL,      ///< Floating-point multiply (scalar double).
    FDIV,      ///< Floating-point divide (scalar double).
    UCOMIS,    ///< Unordered compare scalar double.
    CVTSI2SD,  ///< Convert signed integer to scalar double.
    CVTTSD2SI, ///< Convert scalar double to signed integer with truncation.
    MOVQrx,    ///< Move 64-bit GPR to XMM (bit-pattern transfer, no conversion).
    MOVSDrr,   ///< Move scalar double register to register.
    MOVSDrm,   ///< Move scalar double register to memory.
    MOVSDmr,   ///< Move scalar double memory to register.
    MOVUPSrm,  ///< Store 128-bit XMM to memory (unaligned).
    MOVUPSmr,  ///< Load 128-bit XMM from memory (unaligned).
    ADDOvfrr,  ///< Signed addition pseudo with overflow check (dest, lhs, rhs).
    SUBOvfrr,  ///< Signed subtraction pseudo with overflow check (dest, lhs, rhs).
    IMULOvfrr  ///< Signed multiplication pseudo with overflow check (dest, lhs, rhs).
};

/// \brief Machine instruction: opcode with ordered operands.
struct MInstr
{
    MOpcode opcode{MOpcode::MOVrr};  ///< Opcode for the instruction.
    std::vector<Operand> operands{}; ///< Operands in emission order.

    /// \brief Create an instruction with the given operands.
    /// @param opc The machine opcode for the instruction.
    /// @param ops Operands in emission order, moved into the instruction.
    /// @return A fully constructed MInstr instance.
    [[nodiscard]] static MInstr make(MOpcode opc, std::vector<Operand> ops);

    /// \brief Create an instruction from an initializer list of operands.
    /// @param opc The machine opcode for the instruction.
    /// @param ops Brace-enclosed operands in emission order (defaults to empty).
    /// @return A fully constructed MInstr instance.
    [[nodiscard]] static MInstr make(MOpcode opc, std::initializer_list<Operand> ops = {});

    /// \brief Append an operand and return a reference to enable chaining.
    /// @param op The operand to append to the operand list.
    /// @return Reference to this instruction for fluent-style chaining.
    MInstr &addOperand(Operand op);
};

/// \brief A sequence of machine instructions labelled for control flow.
struct MBasicBlock
{
    std::string label{};                ///< Symbolic label for the block.
    std::vector<MInstr> instructions{}; ///< Ordered list of instructions.

    /// \brief Append an instruction to the block and return a reference to it.
    /// @param instr The instruction to move-append to the block.
    /// @return Reference to the newly appended instruction.
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
    std::size_t localLabelCounter{0};  ///< Counter used to mint unique local labels.

    /// \brief Add a new basic block and return a reference to it.
    /// @param block The basic block to move-append to the function body.
    /// @return Reference to the newly appended basic block.
    MBasicBlock &addBlock(MBasicBlock block);

    /// \brief Generate a function-local unique label using the provided prefix.
    /// @param prefix Short descriptive prefix for the label (e.g. "if_true").
    /// @return A label string guaranteed unique within this MFunction.
    [[nodiscard]] std::string makeLocalLabel(std::string_view prefix);
};

// -----------------------------------------------------------------------------
// Operand helpers
// -----------------------------------------------------------------------------

/// \brief Construct an OpReg representing a virtual register.
/// @param cls  Register class constraining the allocation.
/// @param id   Virtual register identifier (unique per function).
/// @return An OpReg with isPhys == false.
[[nodiscard]] OpReg makeVReg(RegClass cls, uint16_t id) noexcept;

/// \brief Construct an OpReg representing a physical register.
/// @param cls  Register class of the physical register.
/// @param phys PhysReg enum value cast to uint16_t.
/// @return An OpReg with isPhys == true.
[[nodiscard]] OpReg makePhysReg(RegClass cls, uint16_t phys) noexcept;

/// \brief Wrap a virtual register operand into the variant container.
/// @param cls Register class constraining the allocation.
/// @param id  Virtual register identifier.
/// @return An Operand holding an OpReg with isPhys == false.
[[nodiscard]] Operand makeVRegOperand(RegClass cls, uint16_t id);

/// \brief Wrap a physical register operand into the variant container.
/// @param cls  Register class of the physical register.
/// @param phys PhysReg enum value cast to uint16_t.
/// @return An Operand holding an OpReg with isPhys == true.
[[nodiscard]] Operand makePhysRegOperand(RegClass cls, uint16_t phys);

/// \brief Construct an immediate operand.
/// @param value The 64-bit signed immediate value.
/// @return An Operand holding an OpImm.
[[nodiscard]] Operand makeImmOperand(int64_t value);

/// \brief Construct a memory operand from base register and displacement.
/// @param base Base register supplying the address.
/// @param disp Signed byte displacement from the base.
/// @return An Operand holding an OpMem without an index register.
[[nodiscard]] Operand makeMemOperand(OpReg base, int32_t disp);

/// \brief Construct a scaled-index memory operand.
/// @param base  Base register supplying the address.
/// @param index Index register multiplied by @p scale.
/// @param scale Scale factor applied to the index (1, 2, 4, or 8).
/// @param disp  Signed byte displacement from base + index*scale.
/// @return An Operand holding an OpMem with hasIndex == true.
[[nodiscard]] Operand makeMemOperand(OpReg base, OpReg index, uint8_t scale, int32_t disp);

/// \brief Construct a label operand with the provided symbol name.
/// @param name The symbolic name of the label.
/// @return An Operand holding an OpLabel.
[[nodiscard]] Operand makeLabelOperand(std::string name);

/// \brief Construct a RIP-relative label operand with the provided symbol name.
/// @param name The symbolic name referenced relative to RIP.
/// @return An Operand holding an OpRipLabel.
[[nodiscard]] Operand makeRipLabelOperand(std::string name);

// -----------------------------------------------------------------------------
// Pretty printing helpers (for debugging only)
// -----------------------------------------------------------------------------

/// \brief Render a register operand to string form.
/// @param op The register operand to format.
/// @return Human-readable string such as "%vreg3" or "%rax".
[[nodiscard]] std::string toString(const OpReg &op);

/// \brief Render an immediate operand to string form.
/// @param op The immediate operand to format.
/// @return Human-readable string such as "$42".
[[nodiscard]] std::string toString(const OpImm &op);

/// \brief Render a memory operand to string form.
/// @param op The memory operand to format.
/// @return Human-readable string such as "8(%rbp)" or "(%rdi,%rsi,4)".
[[nodiscard]] std::string toString(const OpMem &op);

/// \brief Render a label operand to string form.
/// @param op The label operand to format.
/// @return The symbol name string.
[[nodiscard]] std::string toString(const OpLabel &op);

/// \brief Render a RIP-relative label operand to string form.
/// @param op The RIP-relative label operand to format.
/// @return Human-readable string such as "name(%rip)".
[[nodiscard]] std::string toString(const OpRipLabel &op);

/// \brief Render any operand to string form.
/// @param operand The variant operand to format.
/// @return Human-readable representation dispatched by operand kind.
[[nodiscard]] std::string toString(const Operand &operand);

/// \brief Render an instruction to string form (opcode + operands).
/// @param instr The machine instruction to format.
/// @return Multi-part string showing opcode and all operands.
[[nodiscard]] std::string toString(const MInstr &instr);

/// \brief Render a basic block to string form.
/// @param block The basic block to format (label + all instructions).
/// @return Multi-line string representation.
[[nodiscard]] std::string toString(const MBasicBlock &block);

/// \brief Render a function to string form.
/// @param func The machine function to format (name + all blocks).
/// @return Multi-line string representation.
[[nodiscard]] std::string toString(const MFunction &func);

} // namespace viper::codegen::x64
