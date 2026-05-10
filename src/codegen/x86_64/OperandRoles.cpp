//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/OperandRoles.cpp
// Purpose: Shared x86-64 Machine IR operand role classification.
// Key invariants:
//   - Covers all defined MIR opcodes; unknown opcodes return conservative roles.
// Ownership/Lifetime:
//   - Stateless free functions; no dynamic allocation.
// Links: codegen/x86_64/OperandRoles.hpp,
//        codegen/x86_64/MachineIR.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/OperandRoles.hpp"

namespace viper::codegen::x64 {

std::pair<bool, bool> operandRoles(const MInstr &instr, std::size_t idx) noexcept {
    switch (instr.opcode) {
        case MOpcode::PUSH:
            return {idx == 0, false};
        case MOpcode::POP:
            return {false, idx == 0};

        case MOpcode::MOVrr:
        case MOpcode::MOVri:
        case MOpcode::MOVmr:
        case MOpcode::LEA:
        case MOpcode::MOVZXrr8:
        case MOpcode::MOVZXrr32:
        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
        case MOpcode::MOVQrx:
        case MOpcode::MOVQxr:
        case MOpcode::MOVSDrr:
        case MOpcode::MOVSDmr:
        case MOpcode::MOVUPSmr:
            return {idx == 1, idx == 0};

        case MOpcode::XORrr32:
            return {false, idx == 0};

        case MOpcode::MOVrm:
        case MOpcode::MOVSDrm:
        case MOpcode::MOVUPSrm:
            return {idx == 0 || idx == 1, false};

        case MOpcode::ADDrr:
        case MOpcode::ADDri:
        case MOpcode::ANDrr:
        case MOpcode::ANDri:
        case MOpcode::ORrr:
        case MOpcode::ORri:
        case MOpcode::XORrr:
        case MOpcode::XORri:
        case MOpcode::SUBrr:
        case MOpcode::SHLri:
        case MOpcode::SHLrc:
        case MOpcode::SHRri:
        case MOpcode::SHRrc:
        case MOpcode::SARri:
        case MOpcode::SARrc:
        case MOpcode::IMULrr:
        case MOpcode::CMOVNErr:
        case MOpcode::FADD:
        case MOpcode::FSUB:
        case MOpcode::FMUL:
        case MOpcode::FDIV:
            if (idx == 0)
                return {true, true};
            if (idx == 1)
                return {true, false};
            return {false, false};

        case MOpcode::DIVS64rr:
        case MOpcode::REMS64rr:
        case MOpcode::DIVS64Chk0rr:
        case MOpcode::REMS64Chk0rr:
        case MOpcode::DIVU64rr:
        case MOpcode::REMU64rr:
        case MOpcode::DIVU64Chk0rr:
        case MOpcode::REMU64Chk0rr:
        case MOpcode::ADDOvfrr:
        case MOpcode::SUBOvfrr:
        case MOpcode::IMULOvfrr:
            if (idx == 0)
                return {false, true};
            if (idx == 1 || idx == 2)
                return {true, false};
            return {false, false};

        case MOpcode::CMPrr:
        case MOpcode::TESTrr:
        case MOpcode::UCOMIS:
            return {idx <= 1, false};
        case MOpcode::CMPri:
            return {idx == 0, false};

        case MOpcode::SETcc:
            return {false, idx == 1};

        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
            return {idx == 0, false};

        case MOpcode::CQO:
            return {false, false};

        case MOpcode::CALL:
            return {idx == 0, false};
        case MOpcode::RET:
        case MOpcode::JMP:
        case MOpcode::JCC:
        case MOpcode::LABEL:
        case MOpcode::UD2:
            return {false, false};

        case MOpcode::PX_COPY:
            return {(idx % 2) == 1, (idx % 2) == 0};

        case MOpcode::SELECT_GPR:
        case MOpcode::SELECT_XMM:
            if (idx == 0)
                return {false, true};
            if (idx == 1 || idx == 2 || idx == 3)
                return {true, false};
            return {false, false};
    }
    return {true, false};
}

bool usesEFlags(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::JCC:
        case MOpcode::SETcc:
        case MOpcode::CMOVNErr:
            return true;
        default:
            return false;
    }
}

bool definesEFlags(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::ADDrr:
        case MOpcode::ADDri:
        case MOpcode::ANDrr:
        case MOpcode::ANDri:
        case MOpcode::ORrr:
        case MOpcode::ORri:
        case MOpcode::XORrr:
        case MOpcode::XORri:
        case MOpcode::XORrr32:
        case MOpcode::SUBrr:
        case MOpcode::SHLri:
        case MOpcode::SHLrc:
        case MOpcode::SHRri:
        case MOpcode::SHRrc:
        case MOpcode::SARri:
        case MOpcode::SARrc:
        case MOpcode::IMULrr:
        case MOpcode::CMPrr:
        case MOpcode::CMPri:
        case MOpcode::TESTrr:
        case MOpcode::UCOMIS:
        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
            return true;
        default:
            return false;
    }
}

bool hasObservableSideEffects(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::PUSH:
        case MOpcode::POP:
        case MOpcode::MOVrm:
        case MOpcode::MOVSDrm:
        case MOpcode::MOVUPSrm:
        case MOpcode::CALL:
        case MOpcode::RET:
        case MOpcode::JMP:
        case MOpcode::JCC:
        case MOpcode::LABEL:
        case MOpcode::UD2:
        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
        case MOpcode::CQO:
        case MOpcode::ADDOvfrr:
        case MOpcode::SUBOvfrr:
        case MOpcode::IMULOvfrr:
        case MOpcode::PX_COPY:
            return true;
        default:
            return false;
    }
}

} // namespace viper::codegen::x64
