//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/MachineIR.cpp
// Purpose: AArch64 Machine IR pretty-printing and opcode name table.
// Key invariants:
//   - All toString() functions are debug-only; they are not called on hot paths.
//   - opcodeName() is generated from MOpcodeDef.inc to stay in sync with the enum.
// Ownership/Lifetime:
//   - Returns owned std::string values; callers are responsible for lifetime.
// Links: codegen/aarch64/MachineIR.hpp, codegen/aarch64/MOpcodeDef.inc
//
//===----------------------------------------------------------------------===//

#include "MachineIR.hpp"
#include "TargetAArch64.hpp"

#include <sstream>

namespace viper::codegen::aarch64 {

/// @brief Map a Machine IR opcode to a descriptive string for diagnostics.
/// @details Generated from MOpcodeDef.inc — the single source of truth.
[[nodiscard]] const char *opcodeName(MOpcode opc) noexcept {
    switch (opc) {
#define VIPER_MIR_OPCODE(name)                                                                     \
    case MOpcode::name:                                                                            \
        return #name;
#include "codegen/aarch64/MOpcodeDef.inc"
    }
    return "<unknown>";
}

/// @brief Map a register class to the textual suffix used in debug output.
[[nodiscard]] const char *regClassSuffix(RegClass cls) noexcept {
    switch (cls) {
        case RegClass::GPR:
            return "gpr";
        case RegClass::FPR:
            return "fpr";
    }
    return "unknown";
}

/// @brief Render a register operand as human-readable text.
std::string toString(const MReg &reg) {
    std::ostringstream os;
    if (reg.isPhys) {
        const auto phys = static_cast<PhysReg>(reg.idOrPhys);
        os << '@' << regName(phys);
    } else {
        os << "%v" << static_cast<unsigned>(reg.idOrPhys);
    }
    os << ':' << regClassSuffix(reg.cls);
    return os.str();
}

/// @brief Render any operand as human-readable text.
std::string toString(const MOperand &op) {
    switch (op.kind) {
        case MOperand::Kind::Reg:
            return toString(op.reg);
        case MOperand::Kind::Imm: {
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
std::string toString(const MInstr &instr) {
    std::ostringstream os;
    os << opcodeName(instr.opc);
    bool first = true;
    for (const auto &op : instr.ops) {
        if (first) {
            os << ' ' << toString(op);
            first = false;
        } else {
            os << ", " << toString(op);
        }
    }
    return os.str();
}

/// @brief Render a basic block as human-readable text.
std::string toString(const MBasicBlock &block) {
    std::ostringstream os;
    os << block.name << ":\n";
    for (const auto &instr : block.instrs) {
        os << "  " << toString(instr) << '\n';
    }
    return os.str();
}

/// @brief Render a function as human-readable text.
std::string toString(const MFunction &func) {
    std::ostringstream os;
    os << "function " << func.name << '\n';
    for (const auto &block : func.blocks) {
        os << toString(block);
    }
    return os.str();
}

} // namespace viper::codegen::aarch64
