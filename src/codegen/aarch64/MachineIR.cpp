//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/MachineIR.cpp
// Purpose: Provide implementations for the AArch64 Machine IR types.
//
// Key invariants: Helpers preserve operand invariants declared in the headers
//                 and maintain the stable textual form relied upon by
//                 debugging utilities.
//
// Ownership/Lifetime: All Machine IR nodes own their operands by value.
//
// Links: docs/codemap.md#codegen
//
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
        case MOpcode::AddFpImm:
            return "AddFpImm";
        case MOpcode::LdrRegBaseImm:
            return "LdrRegBaseImm";
        case MOpcode::StrRegBaseImm:
            return "StrRegBaseImm";
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
        case MOpcode::CmpRR:
            return "CmpRR";
        case MOpcode::CmpRI:
            return "CmpRI";
        case MOpcode::Cset:
            return "Cset";
        case MOpcode::Br:
            return "Br";
        case MOpcode::BCond:
            return "BCond";
        case MOpcode::Bl:
            return "Bl";
        case MOpcode::Ret:
            return "Ret";
        case MOpcode::AdrPage:
            return "AdrPage";
        case MOpcode::AddPageOff:
            return "AddPageOff";
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
