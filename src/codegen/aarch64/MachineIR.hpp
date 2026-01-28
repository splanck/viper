//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/MachineIR.hpp
// Purpose: AArch64 Machine IR (MIR) data structures for code generation.
//
// This file defines the machine-level intermediate representation used between
// IL lowering and assembly emission. MIR instructions are target-specific and
// map closely to AArch64 instructions but still use virtual registers that
// must be allocated to physical registers before emission.
//
// Key invariants:
// - MInstr operands follow a consistent pattern: destination first, then sources.
// - Virtual registers (isPhys=false) must be resolved by register allocation.
// - Physical registers (isPhys=true) are final and correspond to AArch64 regs.
//
// Ownership/Lifetime:
// - MFunction owns all blocks, blocks own instructions.
// - Frame layout is computed during lowering and finalized after allocation.
//
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

/// @brief Machine IR opcodes for AArch64 code generation.
///
/// Each opcode represents a target-specific operation that will be emitted
/// as one or more AArch64 assembly instructions.
enum class MOpcode
{
    MovRR,
    MovRI,
    // Floating-point (64-bit, scalar)
    FMovRR,
    FMovRI,
    FMovGR, // fmov dDst, xSrc (transfer bits from GPR to FPR without conversion)
    FAddRRR,
    FSubRRR,
    FMulRRR,
    FDivRRR,
    FCmpRR,
    // Integer<->Float conversions (64-bit)
    SCvtF,  // scvtf dDst, xSrc
    FCvtZS, // fcvtzs xDst, dSrc
    UCvtF,  // ucvtf dDst, xSrc
    FCvtZU, // fcvtzu xDst, dSrc
    // Floating-point rounding
    FRintN, // frintn dDst, dSrc (round to nearest, ties to even)
    // Stack pointer adjust (for outgoing arg area)
    SubSpImm,
    AddSpImm,
    // Store to outgoing arg area at [sp, #imm]
    StrRegSpImm,
    StrFprSpImm,
    // Load/store from frame pointer (for locals)
    LdrRegFpImm, // dst, offset - ldr xN, [x29, #offset]
    StrRegFpImm, // src, offset - str xN, [x29, #offset]
    LdrFprFpImm, // dst(FPR), offset - ldr dN, [x29, #offset]
    StrFprFpImm, // src(FPR), offset - str dN, [x29, #offset]
    AddFpImm,    // dst, offset - add xN, x29, #offset (for alloca address computation)
    // Load/store from arbitrary base register (heap/global)
    LdrRegBaseImm, // dst, base, offset - ldr xN, [xM, #offset]
    StrRegBaseImm, // src, base, offset - str xN, [xM, #offset]
    LdrFprBaseImm, // dst(FPR), base, offset - ldr dN, [xM, #offset]
    StrFprBaseImm, // src(FPR), base, offset - str dN, [xM, #offset]
    AddRRR,
    SubRRR,
    MulRRR,
    SDivRRR,  // sdiv dst, lhs, rhs (signed division)
    UDivRRR,  // udiv dst, lhs, rhs (unsigned division)
    MSubRRRR, // msub dst, mul1, mul2, sub (dst = sub - mul1*mul2)
    Cbz,      // cbz reg, label (compare and branch if zero)
    AndRRR,
    OrrRRR,
    EorRRR,
    AddRI,
    SubRI,
    LslRI,
    LsrRI,
    AsrRI,
    LslvRRR, // lslv dst, lhs, rhs (shift left by register)
    LsrvRRR, // lsrv dst, lhs, rhs (logical shift right by register)
    AsrvRRR, // asrv dst, lhs, rhs (arithmetic shift right by register)
    CmpRR,
    CmpRI,
    TstRR, // tst lhs, rhs (bitwise AND, set flags, discard result)
    Cset,  // dst, cond(code)
    Br,    // b label
    BCond, // b.<cond> label
    Bl,    // bl <label> (call)
    Blr,   // blr <reg> (indirect call through register)
    Ret,   // ret (return from function)
    // Address materialisation for globals (Mach-O style)
    AdrPage,    // dst, label  => adrp dst, label@PAGE
    AddPageOff, // dst, base, label => add dst, base, label@PAGEOFF
};

/// @brief Represents a machine register (physical or virtual).
///
/// Before register allocation, isPhys=false and idOrPhys contains a virtual
/// register ID. After allocation, isPhys=true and idOrPhys contains the
/// PhysReg enum value cast to uint16_t.
struct MReg
{
    bool isPhys{false};          ///< True if this is a physical register.
    RegClass cls{RegClass::GPR}; ///< Register class (GPR or FPR).
    uint16_t idOrPhys{0U};       ///< Virtual reg ID or PhysReg enum value.
};

/// @brief Operand for a machine IR instruction.
///
/// Operands can be registers, immediates, condition codes, or labels.
/// The interpretation depends on the MOpcode of the containing MInstr.
struct MOperand
{
    enum class Kind
    {
        Reg,  ///< Physical or virtual register.
        Imm,  ///< Immediate constant.
        Cond, ///< Condition code (eq, ne, lt, etc.).
        Label ///< Symbol or basic block label.
    } kind{Kind::Imm};

    MReg reg{};                ///< Register operand (when kind==Reg).
    long long imm{0};          ///< Immediate value (when kind==Imm).
    const char *cond{nullptr}; ///< Condition code string (when kind==Cond).
    std::string label;         ///< Label name (when kind==Label).

    /// @brief Create a physical register operand.
    static MOperand regOp(PhysReg r)
    {
        MOperand o{};
        o.kind = Kind::Reg;
        o.reg.isPhys = true;
        o.reg.cls = isGPR(r) ? RegClass::GPR : RegClass::FPR;
        o.reg.idOrPhys = static_cast<uint16_t>(r);
        return o;
    }

    /// @brief Create a virtual register operand.
    /// @param cls Register class (GPR or FPR).
    /// @param id Virtual register identifier.
    static MOperand vregOp(RegClass cls, uint16_t id)
    {
        MOperand o{};
        o.kind = Kind::Reg;
        o.reg.isPhys = false;
        o.reg.cls = cls;
        o.reg.idOrPhys = id;
        return o;
    }

    /// @brief Create an immediate operand.
    static MOperand immOp(long long v)
    {
        MOperand o{};
        o.kind = Kind::Imm;
        o.imm = v;
        return o;
    }

    /// @brief Create a condition code operand (e.g., "eq", "ne", "lt").
    static MOperand condOp(const char *c)
    {
        MOperand o{};
        o.kind = Kind::Cond;
        o.cond = c;
        return o;
    }

    /// @brief Create a label operand (function name or block label).
    static MOperand labelOp(std::string name)
    {
        MOperand o{};
        o.kind = Kind::Label;
        o.label = std::move(name);
        return o;
    }
};

/// @brief A single machine IR instruction.
///
/// Contains an opcode and a vector of operands. Operand interpretation
/// depends on the opcode - typically destination register first, then
/// source registers/immediates.
struct MInstr
{
    MOpcode opc{};               ///< The operation to perform.
    std::vector<MOperand> ops{}; ///< Instruction operands.
};

/// @brief A basic block containing machine IR instructions.
///
/// Basic blocks are named units of sequential code with a single entry
/// and (typically) ending in a branch or return instruction.
struct MBasicBlock
{
    std::string name;           ///< Block label (used for branches).
    std::vector<MInstr> instrs; ///< Instructions in program order.
};

/// @brief A function in machine IR form.
///
/// Contains all basic blocks, callee-saved register information, and
/// stack frame layout computed during lowering and register allocation.
struct MFunction
{
    std::string name;                ///< Function symbol name.
    std::vector<MBasicBlock> blocks; ///< Basic blocks in layout order.

    /// Callee-saved GPRs that must be preserved across calls.
    std::vector<PhysReg> savedGPRs;

    /// Callee-saved FPRs (D-registers) that must be preserved across calls.
    std::vector<PhysReg> savedFPRs;

    /// Total size of the local stack frame in bytes (16-byte aligned).
    int localFrameSize{0};

    /// @brief Describes a stack-allocated local variable.
    struct StackLocal
    {
        unsigned tempId{0}; ///< IL temporary ID this slot is for.
        int size{0};        ///< Size in bytes.
        int align{8};       ///< Alignment requirement.
        int offset{0};      ///< FP-relative offset (negative).
    };

    /// @brief Describes a spill slot for a virtual register.
    struct SpillSlot
    {
        uint16_t vreg{0}; ///< Virtual register ID.
        int size{8};      ///< Size in bytes.
        int align{8};     ///< Alignment requirement.
        int offset{0};    ///< FP-relative offset (negative).
    };

    /// @brief Stack frame layout information.
    struct FrameLayout
    {
        std::vector<StackLocal> locals; ///< Local variable slots.
        std::vector<SpillSlot> spills;  ///< Spill slots for virtual registers.
        int totalBytes{0};              ///< Total frame size (aligned to 16 bytes).
        int maxOutgoingBytes{0};        ///< Space reserved for outgoing call arguments.

        /// @brief Look up the FP-relative offset for a local variable.
        /// @param tempId The IL temporary identifier.
        /// @return FP-relative offset, or 0 if not found.
        int getLocalOffset(unsigned tempId) const
        {
            for (const auto &L : locals)
                if (L.tempId == tempId)
                    return L.offset;
            return 0;
        }

        /// @brief Look up the FP-relative offset for a spill slot.
        /// @param vreg Virtual register identifier.
        /// @return FP-relative offset, or 0 if not found.
        int getSpillOffset(uint16_t vreg) const
        {
            for (const auto &S : spills)
                if (S.vreg == vreg)
                    return S.offset;
            return 0;
        }
    } frame; ///< Stack frame layout.
};

// -----------------------------------------------------------------------------
// Pretty printing helpers (for debugging only)
// -----------------------------------------------------------------------------

/// @brief Map an opcode to its human-readable name.
[[nodiscard]] const char *opcodeName(MOpcode opc) noexcept;

/// @brief Map a register class to a short suffix for debug output.
[[nodiscard]] const char *regClassSuffix(RegClass cls) noexcept;

/// @brief Render a register operand to string form.
[[nodiscard]] std::string toString(const MReg &reg);

/// @brief Render any operand to string form.
[[nodiscard]] std::string toString(const MOperand &op);

/// @brief Render an instruction to string form.
[[nodiscard]] std::string toString(const MInstr &instr);

/// @brief Render a basic block to string form.
[[nodiscard]] std::string toString(const MBasicBlock &block);

/// @brief Render a function to string form.
[[nodiscard]] std::string toString(const MFunction &func);

} // namespace viper::codegen::aarch64
