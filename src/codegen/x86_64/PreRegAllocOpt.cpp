//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/PreRegAllocOpt.cpp
// Purpose: Implement conservative x86-64 MIR cleanup before register
//          allocation. The traversal and safety conditions live in the shared
//          PreRAForwardCopy template; this file supplies the x86-64 MIR
//          queries (copy shapes, operand roles, boundary opcodes).
// Key invariants:
//   - Operates on virtual-register MIR only; physical sources are not forwarded.
//   - Single-use copy elimination does not cross block boundaries.
// Ownership/Lifetime:
//   - Stateless; mutates caller-owned MFunction in place.
// Links: codegen/common/PreRAForwardCopy.hpp,
//        codegen/x86_64/PreRegAllocOpt.hpp,
//        codegen/x86_64/OperandRoles.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/PreRegAllocOpt.hpp"

#include "codegen/common/PreRAForwardCopy.hpp"
#include "codegen/x86_64/OperandRoles.hpp"

#include <variant>

namespace viper::codegen::x64 {
namespace {

/// @brief Compare two register operands for equality (phys flag, class, id).
[[nodiscard]] bool sameReg(const OpReg &lhs, const OpReg &rhs) noexcept {
    return lhs.isPhys == rhs.isPhys && lhs.cls == rhs.cls && lhs.idOrPhys == rhs.idOrPhys;
}

/// @brief Predicate: is @p opcode a pure register-to-register copy?
[[nodiscard]] bool isCopyOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MOVrr || opcode == MOpcode::MOVSDrr;
}

/// @brief Predicate: does @p opcode terminate a straight-line region (calls excluded)?
/// @details Implicit-register sequences (CQO/IDIV), pseudos expanded later
///          (PX_COPY, SELECT, checked arithmetic), and control flow all stop
///          the forwarding scan.
[[nodiscard]] bool isNonCallBoundaryOpcode(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::RET:
        case MOpcode::JMP:
        case MOpcode::JCC:
        case MOpcode::LABEL:
        case MOpcode::UD2:
        case MOpcode::PUSH:
        case MOpcode::POP:
        case MOpcode::CQO:
        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
        case MOpcode::MULr:
        case MOpcode::IMULr:
        case MOpcode::PX_COPY:
        case MOpcode::SELECT_GPR:
        case MOpcode::SELECT_XMM:
        case MOpcode::DIVS64rr:
        case MOpcode::REMS64rr:
        case MOpcode::DIVU64rr:
        case MOpcode::REMU64rr:
        case MOpcode::DIVS64Chk0rr:
        case MOpcode::REMS64Chk0rr:
        case MOpcode::DIVU64Chk0rr:
        case MOpcode::REMU64Chk0rr:
        case MOpcode::ADDOvfrr:
        case MOpcode::SUBOvfrr:
        case MOpcode::IMULOvfrr:
            return true;
        default:
            return false;
    }
}

/// @brief Predicate: does @p mem reference @p reg via base or index?
[[nodiscard]] bool memUsesReg(const OpMem &mem, const OpReg &reg) noexcept {
    if (sameReg(mem.base, reg))
        return true;
    return mem.hasIndex && sameReg(mem.index, reg);
}

/// @brief x86-64 traits for the shared pre-RA copy forwarding template.
struct X64PreRATraits {
    using BlockT = MBasicBlock;
    using InstrT = MInstr;
    using RegT = OpReg;

    static std::vector<MInstr> &instrs(MBasicBlock &block) {
        return block.instructions;
    }

    static const std::vector<MInstr> &instrs(const MBasicBlock &block) {
        return block.instructions;
    }

    static bool isIdentityCopy(const MInstr &instr) {
        if (!isCopyOpcode(instr.opcode) || instr.operands.size() != 2)
            return false;
        const auto *dst = std::get_if<OpReg>(&instr.operands[0]);
        const auto *src = std::get_if<OpReg>(&instr.operands[1]);
        return dst && src && sameReg(*dst, *src);
    }

    static bool isForwardableCopy(const MInstr &instr, OpReg &dst, OpReg &src) {
        if (!isCopyOpcode(instr.opcode) || instr.operands.size() != 2)
            return false;
        const auto *dstReg = std::get_if<OpReg>(&instr.operands[0]);
        const auto *srcReg = std::get_if<OpReg>(&instr.operands[1]);
        // Physical sources such as ABI return registers are not tracked as
        // live ranges here. Forwarding them would let register allocation
        // reuse that physical register before the forwarded use.
        if (!dstReg || !srcReg || dstReg->isPhys || srcReg->isPhys ||
            dstReg->cls != srcReg->cls || sameReg(*dstReg, *srcReg))
            return false;
        dst = *dstReg;
        src = *srcReg;
        return true;
    }

    static bool definesReg(const MInstr &instr, const OpReg &reg) {
        for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
            const auto [isUse, isDef] = operandRoles(instr, idx);
            (void)isUse;
            if (!isDef)
                continue;
            const auto *opReg = std::get_if<OpReg>(&instr.operands[idx]);
            if (opReg && sameReg(*opReg, reg))
                return true;
        }
        return false;
    }

    static bool isCall(const MInstr &instr) {
        return instr.opcode == MOpcode::CALL;
    }

    static bool isNonCallBoundary(const MInstr &instr) {
        return isNonCallBoundaryOpcode(instr.opcode);
    }

    static common::PreRAUseScan scanUses(const MInstr &instr, const OpReg &dst) {
        common::PreRAUseScan scan{};
        for (std::size_t opIdx = 0; opIdx < instr.operands.size(); ++opIdx) {
            const auto [isUse, isDef] = operandRoles(instr, opIdx);
            if (!isUse)
                continue;

            const auto &operand = instr.operands[opIdx];
            bool usesDst = false;
            bool direct = false;
            if (const auto *opReg = std::get_if<OpReg>(&operand)) {
                usesDst = sameReg(*opReg, dst);
                direct = usesDst && !isDef;
            } else if (const auto *mem = std::get_if<OpMem>(&operand)) {
                usesDst = memUsesReg(*mem, dst);
            }
            if (!usesDst)
                continue;

            ++scan.useCount;
            if (direct) {
                ++scan.directUseCount;
                scan.directOperand = opIdx;
            }
        }
        return scan;
    }

    static void forwardUse(MInstr &use, std::size_t operandIndex, const MInstr &copy) {
        use.operands[operandIndex] = copy.operands[1];
    }
};

} // namespace

/// @brief Top-level entry point for pre-register-allocation MIR cleanup.
/// @details Delegates to the shared template: identity copies are removed and
///          single-use virtual-to-virtual copies are forwarded. Returning a
///          count lets callers report the reduction in statistics output.
/// @param fn Function to rewrite in place.
/// @return Number of MIR instructions removed.
std::size_t runPreRegAllocOpt(MFunction &fn) {
    return common::runPreRAForwardCopy<X64PreRATraits>(fn);
}

} // namespace viper::codegen::x64
