//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file MachineIR.cpp
/// @brief AArch64 Machine IR type implementations and utilities.
///
/// This file provides the runtime support for Viper's AArch64 Machine IR (MIR),
/// a low-level intermediate representation used between IL lowering and
/// assembly emission. MIR represents code at the instruction level with
/// explicit register operands and machine opcodes.
///
/// **What is Machine IR?**
/// Machine IR is a target-specific representation that closely models the
/// AArch64 instruction set. Unlike high-level IL which uses typed values and
/// SSA temporaries, MIR uses:
/// - Virtual registers (vregs) that are later allocated to physical registers
/// - Physical registers for ABI-mandated operands (args, return values)
/// - Machine opcodes that map 1:1 or 1:few to AArch64 instructions
///
/// **MIR Hierarchy:**
/// ```
/// MFunction           ← Function being compiled
///   └─ blocks[]       ← Vector of MBasicBlock
///        └─ instrs[]  ← Vector of MInstr (machine instructions)
///             └─ operands[] ← MOperand values (regs, imms, labels)
/// ```
///
/// **Operand Types (MOperand):**
/// | Kind       | Description                          | Example        |
/// |------------|--------------------------------------|----------------|
/// | VReg       | Virtual register (to be allocated)   | v42:gpr        |
/// | PhysReg    | Physical AArch64 register            | x0, d0, sp     |
/// | Immediate  | Signed integer constant              | #-16, #4096    |
/// | Label      | Basic block or symbol reference      | .LBB0_3        |
/// | FrameSlot  | Stack frame offset (FP-relative)     | [fp, #-32]     |
///
/// **Register Classes:**
/// | Class | Description              | Physical Registers |
/// |-------|--------------------------|-------------------|
/// | GPR   | General-purpose (64-bit) | x0-x28, x30 (lr) |
/// | FPR   | Floating-point (64-bit)  | d0-d31           |
///
/// **Machine Opcodes (MOpcode):**
/// MIR opcodes abstract over specific instruction encodings:
/// | MOpcode     | AArch64 Instruction | Description              |
/// |-------------|---------------------|--------------------------|
/// | MovRR       | mov xd, xn          | GPR-to-GPR move          |
/// | MovRI       | mov xd, #imm        | Load immediate to GPR    |
/// | AddRRR      | add xd, xn, xm      | Integer addition         |
/// | FAddRRR     | fadd dd, dn, dm     | FP addition              |
/// | LdrRegFpImm | ldr xd, [fp, #off]  | Load from stack          |
/// | Bl          | bl <symbol>         | Branch with link (call)  |
/// | BCond       | b.<cond> <label>    | Conditional branch       |
///
/// **Lowering Pipeline Position:**
/// ```
/// IL (SSA)
///    │
///    ▼
/// ┌──────────────────┐
/// │ LowerILToMIR     │ ← Convert IL to MIR (this representation)
/// └──────────────────┘
///    │
///    ▼
/// MIR (virtual registers)
///    │
///    ▼
/// ┌──────────────────┐
/// │ RegAllocLinear   │ ← Assign physical registers, insert spills
/// └──────────────────┘
///    │
///    ▼
/// MIR (physical registers)
///    │
///    ▼
/// ┌──────────────────┐
/// │ AsmEmitter       │ ← Emit assembly text
/// └──────────────────┘
///    │
///    ▼
/// Assembly (.s)
/// ```
///
/// **Key Invariants:**
/// - All MIR nodes own their operands by value (no pointers)
/// - Virtual registers have a register class (GPR or FPR)
/// - Physical registers are fully determined by ABI and lowering rules
/// - Immediate values fit in the instruction's immediate field
///
/// @see MachineIR.hpp For type definitions
/// @see LowerILToMIR.cpp For IL→MIR conversion
/// @see RegAllocLinear.cpp For register allocation
/// @see AsmEmitter.cpp For assembly emission
///
//===----------------------------------------------------------------------===//

#include "MachineIR.hpp"
#include "TargetAArch64.hpp"

#include <sstream>

namespace viper::codegen::aarch64
{

/// @brief Map a Machine IR opcode to a descriptive string for diagnostics.
[[nodiscard]] const char *opcodeName(MOpcode opc) noexcept
{
    switch (opc)
    {
        case MOpcode::MovRR:
            return "MovRR";
        case MOpcode::MovRI:
            return "MovRI";
        case MOpcode::FMovRR:
            return "FMovRR";
        case MOpcode::FMovRI:
            return "FMovRI";
        case MOpcode::FMovGR:
            return "FMovGR";
        case MOpcode::FAddRRR:
            return "FAddRRR";
        case MOpcode::FSubRRR:
            return "FSubRRR";
        case MOpcode::FMulRRR:
            return "FMulRRR";
        case MOpcode::FDivRRR:
            return "FDivRRR";
        case MOpcode::FCmpRR:
            return "FCmpRR";
        case MOpcode::SCvtF:
            return "SCvtF";
        case MOpcode::FCvtZS:
            return "FCvtZS";
        case MOpcode::UCvtF:
            return "UCvtF";
        case MOpcode::FCvtZU:
            return "FCvtZU";
        case MOpcode::FRintN:
            return "FRintN";
        case MOpcode::SubSpImm:
            return "SubSpImm";
        case MOpcode::AddSpImm:
            return "AddSpImm";
        case MOpcode::StrRegSpImm:
            return "StrRegSpImm";
        case MOpcode::StrFprSpImm:
            return "StrFprSpImm";
        case MOpcode::LdrRegFpImm:
            return "LdrRegFpImm";
        case MOpcode::StrRegFpImm:
            return "StrRegFpImm";
        case MOpcode::LdrFprFpImm:
            return "LdrFprFpImm";
        case MOpcode::StrFprFpImm:
            return "StrFprFpImm";
        case MOpcode::PhiStoreGPR:
            return "PhiStoreGPR";
        case MOpcode::PhiStoreFPR:
            return "PhiStoreFPR";
        case MOpcode::AddFpImm:
            return "AddFpImm";
        case MOpcode::LdrRegBaseImm:
            return "LdrRegBaseImm";
        case MOpcode::StrRegBaseImm:
            return "StrRegBaseImm";
        case MOpcode::LdrFprBaseImm:
            return "LdrFprBaseImm";
        case MOpcode::StrFprBaseImm:
            return "StrFprBaseImm";
        case MOpcode::AddRRR:
            return "AddRRR";
        case MOpcode::SubRRR:
            return "SubRRR";
        case MOpcode::MulRRR:
            return "MulRRR";
        case MOpcode::SDivRRR:
            return "SDivRRR";
        case MOpcode::UDivRRR:
            return "UDivRRR";
        case MOpcode::MSubRRRR:
            return "MSubRRRR";
        case MOpcode::Cbz:
            return "Cbz";
        case MOpcode::AndRRR:
            return "AndRRR";
        case MOpcode::OrrRRR:
            return "OrrRRR";
        case MOpcode::EorRRR:
            return "EorRRR";
        case MOpcode::AndRI:
            return "AndRI";
        case MOpcode::OrrRI:
            return "OrrRI";
        case MOpcode::EorRI:
            return "EorRI";
        case MOpcode::AddRI:
            return "AddRI";
        case MOpcode::SubRI:
            return "SubRI";
        case MOpcode::LslRI:
            return "LslRI";
        case MOpcode::LsrRI:
            return "LsrRI";
        case MOpcode::AsrRI:
            return "AsrRI";
        case MOpcode::LslvRRR:
            return "LslvRRR";
        case MOpcode::LsrvRRR:
            return "LsrvRRR";
        case MOpcode::AsrvRRR:
            return "AsrvRRR";
        case MOpcode::CmpRR:
            return "CmpRR";
        case MOpcode::CmpRI:
            return "CmpRI";
        case MOpcode::TstRR:
            return "TstRR";
        case MOpcode::Cset:
            return "Cset";
        case MOpcode::Br:
            return "Br";
        case MOpcode::BCond:
            return "BCond";
        case MOpcode::Bl:
            return "Bl";
        case MOpcode::Blr:
            return "Blr";
        case MOpcode::Ret:
            return "Ret";
        case MOpcode::AdrPage:
            return "AdrPage";
        case MOpcode::AddPageOff:
            return "AddPageOff";
        case MOpcode::Cbnz:
            return "Cbnz";
        case MOpcode::MAddRRRR:
            return "MAddRRRR";
        case MOpcode::Csel:
            return "Csel";
        case MOpcode::LdpRegFpImm:
            return "LdpRegFpImm";
        case MOpcode::StpRegFpImm:
            return "StpRegFpImm";
        case MOpcode::LdpFprFpImm:
            return "LdpFprFpImm";
        case MOpcode::StpFprFpImm:
            return "StpFprFpImm";
    }
    return "<unknown>";
}

/// @brief Map a register class to the textual suffix used in debug output.
[[nodiscard]] const char *regClassSuffix(RegClass cls) noexcept
{
    switch (cls)
    {
        case RegClass::GPR:
            return "gpr";
        case RegClass::FPR:
            return "fpr";
    }
    return "unknown";
}

/// @brief Render a register operand as human-readable text.
std::string toString(const MReg &reg)
{
    std::ostringstream os;
    if (reg.isPhys)
    {
        const auto phys = static_cast<PhysReg>(reg.idOrPhys);
        os << '@' << regName(phys);
    }
    else
    {
        os << "%v" << static_cast<unsigned>(reg.idOrPhys);
    }
    os << ':' << regClassSuffix(reg.cls);
    return os.str();
}

/// @brief Render any operand as human-readable text.
std::string toString(const MOperand &op)
{
    switch (op.kind)
    {
        case MOperand::Kind::Reg:
            return toString(op.reg);
        case MOperand::Kind::Imm:
        {
            std::ostringstream os;
            os << '#' << op.imm;
            return os.str();
        }
        case MOperand::Kind::Cond:
            return op.cond ? op.cond : "<cond?>";
        case MOperand::Kind::Label:
            return op.label;
    }
    return "<unknown>";
}

/// @brief Render an instruction as human-readable text.
std::string toString(const MInstr &instr)
{
    std::ostringstream os;
    os << opcodeName(instr.opc);
    bool first = true;
    for (const auto &op : instr.ops)
    {
        if (first)
        {
            os << ' ' << toString(op);
            first = false;
        }
        else
        {
            os << ", " << toString(op);
        }
    }
    return os.str();
}

/// @brief Render a basic block as human-readable text.
std::string toString(const MBasicBlock &block)
{
    std::ostringstream os;
    os << block.name << ":\n";
    for (const auto &instr : block.instrs)
    {
        os << "  " << toString(instr) << '\n';
    }
    return os.str();
}

/// @brief Render a function as human-readable text.
std::string toString(const MFunction &func)
{
    std::ostringstream os;
    os << "function " << func.name << '\n';
    for (const auto &block : func.blocks)
    {
        os << toString(block);
    }
    return os.str();
}

} // namespace viper::codegen::aarch64
