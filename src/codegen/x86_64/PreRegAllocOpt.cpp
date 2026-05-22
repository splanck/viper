//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/PreRegAllocOpt.cpp
// Purpose: Implement conservative x86-64 MIR cleanup before register allocation.
// Key invariants:
//   - Operates on virtual-register MIR only; physical sources are not forwarded.
//   - Single-use copy elimination does not cross block boundaries.
// Ownership/Lifetime:
//   - Stateless; mutates caller-owned MFunction in place.
// Links: codegen/x86_64/PreRegAllocOpt.hpp,
//        codegen/x86_64/OperandRoles.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/PreRegAllocOpt.hpp"

#include "codegen/x86_64/OperandRoles.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace viper::codegen::x64 {
namespace {

/// @brief Position of a single use within a basic block.
/// @details Used by the pre-RA copy elimination to record the unique use of
///          a copy's destination so the use can be rewritten to refer to
///          the copy's source instead.
struct UseSite {
    std::size_t instrIndex{0};    ///< Index of the consuming instruction.
    std::size_t operandIndex{0};  ///< Operand index within that instruction.
};

/// @brief Compare two register operands for equality (phys flag, class, id).
[[nodiscard]] bool sameReg(const OpReg &lhs, const OpReg &rhs) noexcept {
    return lhs.isPhys == rhs.isPhys && lhs.cls == rhs.cls && lhs.idOrPhys == rhs.idOrPhys;
}

/// @brief Predicate: is @p opcode a pure register-to-register copy?
/// @details MOVrr handles GPRs; MOVSDrr handles XMMs. Other moves (e.g.
///          MOVri) involve immediates and aren't subject to the same
///          forwarding logic.
[[nodiscard]] bool isCopyOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MOVrr || opcode == MOpcode::MOVSDrr;
}

/// @brief Predicate: does @p opcode terminate a straight-line region?
/// @details Returns true for the opcodes the analysis treats as block
///          boundaries — including CALL, since the calling convention can
///          clobber registers and we must not forward across.
[[nodiscard]] bool isBlockBoundary(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::CALL:
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

/// @brief View an operand as an @c OpReg pointer (const overload).
[[nodiscard]] const OpReg *asReg(const Operand &operand) noexcept {
    return std::get_if<OpReg>(&operand);
}

/// @brief View an operand as an @c OpReg pointer (mutable overload).
[[nodiscard]] OpReg *asReg(Operand &operand) noexcept {
    return std::get_if<OpReg>(&operand);
}

/// @brief Predicate: does @p mem reference @p reg via base or index?
[[nodiscard]] bool memUsesReg(const OpMem &mem, const OpReg &reg) noexcept {
    if (sameReg(mem.base, reg))
        return true;
    return mem.hasIndex && sameReg(mem.index, reg);
}

/// @brief Predicate: does @p operand read @p reg (direct register or via memory)?
[[nodiscard]] bool operandUsesReg(const Operand &operand, const OpReg &reg) noexcept {
    if (const auto *opReg = asReg(operand))
        return sameReg(*opReg, reg);
    if (const auto *mem = std::get_if<OpMem>(&operand))
        return memUsesReg(*mem, reg);
    return false;
}

/// @brief Predicate: does @p instr re-define @p reg?
/// @details Uses the centralised operand-roles table to ask whether any
///          def-position operand names @p reg, which is the safest way to
///          ask "is this value clobbered here?" without hard-coding opcode
///          knowledge.
[[nodiscard]] bool definesReg(const MInstr &instr, const OpReg &reg) noexcept {
    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isUse;
        if (!isDef)
            continue;
        const auto *opReg = asReg(instr.operands[idx]);
        if (opReg && sameReg(*opReg, reg))
            return true;
    }
    return false;
}

/// @brief Predicate: is @p instr a copy whose source and destination are identical?
/// @details Identity copies survive forwarding rewrites and are removed by
///          the final compaction pass.
[[nodiscard]] bool isIdentityCopy(const MInstr &instr) noexcept {
    if (!isCopyOpcode(instr.opcode) || instr.operands.size() != 2)
        return false;
    const auto *dst = asReg(instr.operands[0]);
    const auto *src = asReg(instr.operands[1]);
    return dst && src && sameReg(*dst, *src);
}

/// @brief Locate the unique direct use of a copy's destination.
/// @details Walks forward from @p copyIndex looking for the single
///          instruction that reads @p dst as a plain register operand
///          (not embedded in a memory expression as base/index — those
///          aren't substitutable because the use is implicit).
///          Bails out as soon as a definition of @p src is encountered or
///          when more than one use is found, both of which would make
///          forwarding unsafe.
/// @param block Block to scan.
/// @param copyIndex Index of the candidate copy instruction.
/// @param dst Destination register of the copy.
/// @param src Source register of the copy.
/// @return The unique use position or @c std::nullopt when forwarding is unsafe.
[[nodiscard]] std::optional<UseSite> findSingleDirectUse(const MBasicBlock &block,
                                                         std::size_t copyIndex,
                                                         const OpReg &dst,
                                                         const OpReg &src) {
    std::optional<UseSite> site;

    bool crossedCall = false;
    for (std::size_t idx = copyIndex + 1; idx < block.instructions.size(); ++idx) {
        const auto &instr = block.instructions[idx];

        if (definesReg(instr, src) && !site)
            return std::nullopt;

        std::size_t useCount = 0;
        std::size_t directUseCount = 0;
        std::size_t directOperand = 0;

        for (std::size_t opIdx = 0; opIdx < instr.operands.size(); ++opIdx) {
            const auto [isUse, isDef] = operandRoles(instr, opIdx);
            if (!isUse)
                continue;
            if (!operandUsesReg(instr.operands[opIdx], dst))
                continue;

            ++useCount;
            if (const auto *opReg = asReg(instr.operands[opIdx]); opReg && !isDef) {
                ++directUseCount;
                directOperand = opIdx;
            }
        }

        if (useCount != 0) {
            if (crossedCall)
                return std::nullopt;
            if (useCount != 1 || directUseCount != 1 || definesReg(instr, dst))
                return std::nullopt;
            if (site)
                return std::nullopt;
            site = UseSite{idx, directOperand};
        }

        if (definesReg(instr, dst))
            break;
        if (instr.opcode == MOpcode::CALL)
            crossedCall = true;
        if (isBlockBoundary(instr.opcode) && instr.opcode != MOpcode::CALL)
            break;
    }

    return site;
}

/// @brief Forward each virtual-to-virtual copy whose destination has exactly one use.
/// @details For every register-to-register move @c "MOV vDst, vSrc", if the
///          block contains a single direct read of @c vDst and the
///          intervening instructions do not redefine @c vSrc, the use is
///          rewritten to read @c vSrc and the copy itself is dropped.
///          Physical-register sources are skipped to avoid widening their
///          live range past the original copy.
/// @param block Block to mutate in place.
/// @return Number of copies eliminated.
std::size_t rewriteSingleUseCopies(MBasicBlock &block) {
    std::vector<bool> erase(block.instructions.size(), false);
    std::size_t removed = 0;

    for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
        auto &instr = block.instructions[idx];
        if (!isCopyOpcode(instr.opcode) || instr.operands.size() != 2)
            continue;

        const auto *dst = asReg(instr.operands[0]);
        const auto *src = asReg(instr.operands[1]);
        // Physical sources such as ABI return registers are not tracked as
        // live ranges here. Forwarding them would let register allocation
        // reuse that physical register before the forwarded use.
        if (!dst || !src || dst->isPhys || src->isPhys || dst->cls != src->cls ||
            sameReg(*dst, *src))
            continue;

        auto site = findSingleDirectUse(block, idx, *dst, *src);
        if (!site)
            continue;

        block.instructions[site->instrIndex].operands[site->operandIndex] = instr.operands[1];
        erase[idx] = true;
        ++removed;
    }

    if (removed == 0)
        return 0;

    std::vector<MInstr> kept;
    kept.reserve(block.instructions.size() - removed);
    for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
        if (!erase[idx])
            kept.push_back(std::move(block.instructions[idx]));
    }
    block.instructions = std::move(kept);
    return removed;
}

} // namespace

/// @brief Top-level entry point for pre-register-allocation MIR cleanup.
/// @details Walks every basic block, first removing identity copies and
///          then forwarding single-use virtual-to-virtual copies. Returning
///          a count lets callers report the reduction in statistics output.
/// @param fn Function to rewrite in place.
/// @return Number of MIR instructions removed.
std::size_t runPreRegAllocOpt(MFunction &fn) {
    std::size_t removed = 0;
    for (auto &block : fn.blocks) {
        const auto oldSize = block.instructions.size();
        block.instructions.erase(std::remove_if(block.instructions.begin(),
                                                block.instructions.end(),
                                                isIdentityCopy),
                                 block.instructions.end());
        removed += oldSize - block.instructions.size();
        removed += rewriteSingleUseCopies(block);
    }
    return removed;
}

} // namespace viper::codegen::x64
