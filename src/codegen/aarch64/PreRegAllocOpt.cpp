//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/PreRegAllocOpt.cpp
// Purpose: Conservative AArch64 MIR cleanup before register allocation:
//          identity-copy removal, single-use copy forwarding. The traversal
//          and safety conditions live in the shared PreRAForwardCopy
//          template; this file supplies the AArch64 MIR queries (copy
//          shapes, def positions, boundary opcodes).
//
// Key invariants:
//   - Operates on virtual registers only; no physical register assignment.
//   - Only eliminates copies that are provably safe (single use, no call crosses,
//     no aliasing in the def/use chain).
//
// Ownership/Lifetime:
//   - Borrows MFunction for the duration of the call; no persistent state.
//
// Links: codegen/common/PreRAForwardCopy.hpp,
//        codegen/aarch64/PreRegAllocOpt.hpp,
//        codegen/aarch64/passes/PreRegAllocOptPass.cpp
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/PreRegAllocOpt.hpp"

#include "codegen/aarch64/ra/OpcodeClassify.hpp"
#include "codegen/aarch64/ra/OperandRoles.hpp"
#include "codegen/common/PreRAForwardCopy.hpp"

#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace viper::codegen::aarch64 {
namespace {

/// @brief Return true if @p lhs and @p rhs refer to the same physical or virtual register.
[[nodiscard]] bool sameReg(const MReg &lhs, const MReg &rhs) noexcept {
    return lhs.isPhys == rhs.isPhys && lhs.cls == rhs.cls && lhs.idOrPhys == rhs.idOrPhys;
}

/// @brief Return true if @p opcode is a register-to-register copy (MovRR or FMovRR).
[[nodiscard]] bool isCopyOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MovRR || opcode == MOpcode::FMovRR;
}

/// @brief Return true if @p opcode ends sequential flow within a block (calls excluded).
[[nodiscard]] bool isNonCallBoundaryOpcode(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::Br:
        case MOpcode::BCond:
        case MOpcode::Cbz:
        case MOpcode::Cbnz:
        case MOpcode::Tbz:
        case MOpcode::Tbnz:
        case MOpcode::JumpTable:
        case MOpcode::Ret:
            return true;
        default:
            return false;
    }
}

/// @brief Return true if @p opcode writes its result into the first operand slot (ops[0]).
[[nodiscard]] bool definesFirstOperand(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
        case MOpcode::FMovGR:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
        case MOpcode::FRintN:
        case MOpcode::LdrRegFpImm:
        case MOpcode::Ldr8RegFpImm:
        case MOpcode::Ldr16RegFpImm:
        case MOpcode::Ldr32RegFpImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::Ldr8RegBaseImm:
        case MOpcode::Ldr16RegBaseImm:
        case MOpcode::Ldr32RegBaseImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::LdrRegBaseRegLsl:
        case MOpcode::Ldr32RegBaseRegLsl:
        case MOpcode::LdrFprBaseRegLsl:
        case MOpcode::AddRRRLsl:
        case MOpcode::SubRRRLsl:
        case MOpcode::AndRRRLsl:
        case MOpcode::OrrRRRLsl:
        case MOpcode::EorRRRLsl:
        case MOpcode::AddFpImm:
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SmulhRRR:
        case MOpcode::UmulhRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::MSubRRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::AndRI:
        case MOpcode::OrrRI:
        case MOpcode::EorRI:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::LslvRRR:
        case MOpcode::LsrvRRR:
        case MOpcode::AsrvRRR:
        case MOpcode::Cset:
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        case MOpcode::MAddRRRR:
        case MOpcode::Csel:
        case MOpcode::AddsRRR:
        case MOpcode::SubsRRR:
        case MOpcode::AddsRI:
        case MOpcode::SubsRI:
        case MOpcode::AddOvfRRR:
        case MOpcode::SubOvfRRR:
        case MOpcode::AddOvfRI:
        case MOpcode::SubOvfRI:
        case MOpcode::MulOvfRRR:
            return true;
        default:
            return false;
    }
}

/// @brief Return true if @p operand is a register operand naming the same register as @p reg.
[[nodiscard]] bool operandIsReg(const MOperand &operand, const MReg &reg) noexcept {
    return operand.kind == MOperand::Kind::Reg && sameReg(operand.reg, reg);
}

/// @brief Return true if operand at @p operandIndex in @p instr is a register use (not a def).
[[nodiscard]] bool operandIsUse(const MInstr &instr, std::size_t operandIndex) noexcept {
    if (instr.ops[operandIndex].kind != MOperand::Kind::Reg)
        return false;
    if (definesFirstOperand(instr.opc) && operandIndex == 0)
        return false;
    if ((instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) &&
        operandIndex < 2) {
        return false;
    }
    return true;
}

/// @brief AArch64 traits for the shared pre-RA copy forwarding template.
struct A64PreRATraits {
    using BlockT = MBasicBlock;
    using InstrT = MInstr;
    using RegT = MReg;

    static std::vector<MInstr> &instrs(MBasicBlock &block) {
        return block.instrs;
    }

    static const std::vector<MInstr> &instrs(const MBasicBlock &block) {
        return block.instrs;
    }

    static bool isIdentityCopy(const MInstr &instr) {
        if (!isCopyOpcode(instr.opc) || instr.ops.size() != 2)
            return false;
        if (instr.ops[0].kind != MOperand::Kind::Reg || instr.ops[1].kind != MOperand::Kind::Reg)
            return false;
        return sameReg(instr.ops[0].reg, instr.ops[1].reg);
    }

    static bool isForwardableCopy(const MInstr &instr, MReg &dst, MReg &src) {
        if (!isCopyOpcode(instr.opc) || instr.ops.size() != 2)
            return false;
        if (instr.ops[0].kind != MOperand::Kind::Reg || instr.ops[1].kind != MOperand::Kind::Reg)
            return false;
        const MReg &dstReg = instr.ops[0].reg;
        const MReg &srcReg = instr.ops[1].reg;
        // Physical sources such as ABI return registers are not tracked as
        // live ranges here. Forwarding them would let register allocation
        // reuse that physical register before the forwarded use.
        if (dstReg.isPhys || srcReg.isPhys || dstReg.cls != srcReg.cls || sameReg(dstReg, srcReg))
            return false;
        dst = dstReg;
        src = srcReg;
        return true;
    }

    static bool definesReg(const MInstr &instr, const MReg &reg) {
        if (definesFirstOperand(instr.opc) && !instr.ops.empty() && operandIsReg(instr.ops[0], reg))
            return true;
        if ((instr.opc == MOpcode::LdpRegFpImm || instr.opc == MOpcode::LdpFprFpImm) &&
            instr.ops.size() >= 2) {
            return operandIsReg(instr.ops[0], reg) || operandIsReg(instr.ops[1], reg);
        }
        return false;
    }

    static bool isCall(const MInstr &instr) {
        return instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr;
    }

    static bool isNonCallBoundary(const MInstr &instr) {
        return isNonCallBoundaryOpcode(instr.opc);
    }

    static common::PreRAUseScan scanUses(const MInstr &instr, const MReg &dst) {
        common::PreRAUseScan scan{};
        for (std::size_t opIdx = 0; opIdx < instr.ops.size(); ++opIdx) {
            if (!operandIsUse(instr, opIdx))
                continue;
            if (!operandIsReg(instr.ops[opIdx], dst))
                continue;
            ++scan.useCount;
            ++scan.directUseCount;
            scan.directOperand = opIdx;
        }
        return scan;
    }

    static void forwardUse(MInstr &use, std::size_t operandIndex, const MInstr &copy) {
        use.ops[operandIndex] = copy.ops[1];
    }
};

/// @brief Fold shift+add address arithmetic into scaled addressing forms.
/// @details Rewrites, within a block and over single-def/single-use vregs:
///            lsl t, x, #k ; add p, base, t ; ldr d, [p, #0]
///              -> ldr d, [base, x, lsl #k]       (k in {0, log2(size)})
///            lsl t, x, #k ; add d, y, t
///              -> add d, y, x, lsl #k            (any k)
///          plus the str / 32-bit / FPR load-store variants and the
///          sub/and/orr/eor shifted-operand forms. Def/use counts are taken
///          over the whole function so deleting the feeding instructions can
///          never orphan another consumer.
std::size_t runAddressingFolds(MFunction &fn) {
    // Whole-function def/use counts per GPR vreg.
    std::unordered_map<uint16_t, unsigned> defCount;
    std::unordered_map<uint16_t, unsigned> useCount;
    for (const auto &bb : fn.blocks) {
        for (const auto &mi : bb.instrs) {
            for (std::size_t oi = 0; oi < mi.ops.size(); ++oi) {
                const auto &op = mi.ops[oi];
                if (op.kind != MOperand::Kind::Reg || op.reg.isPhys ||
                    op.reg.cls != RegClass::GPR)
                    continue;
                const auto [isUse, isDef] = ra::operandRoles(mi, oi);
                if (isUse)
                    ++useCount[op.reg.idOrPhys];
                if (isDef)
                    ++defCount[op.reg.idOrPhys];
            }
        }
    }
    const auto singleDefUse = [&](uint16_t vreg) {
        auto d = defCount.find(vreg);
        auto u = useCount.find(vreg);
        return d != defCount.end() && d->second == 1 && u != useCount.end() && u->second == 1;
    };
    const auto vregOf = [](const MOperand &op) -> uint16_t {
        return op.reg.idOrPhys;
    };
    const auto isVGpr = [](const MOperand &op) {
        return op.kind == MOperand::Kind::Reg && !op.reg.isPhys && op.reg.cls == RegClass::GPR;
    };

    std::size_t folded = 0;
    for (auto &bb : fn.blocks) {
        struct ShiftDef {
            std::size_t idx{};
            MOperand src{};
            long long amount{};
        };
        struct AddrDef {
            std::size_t idx{};
            std::size_t shiftIdx{};
            MOperand base{};
            MOperand index{};
            long long amount{};
        };
        std::unordered_map<uint16_t, ShiftDef> shiftDefs;
        std::unordered_map<uint16_t, AddrDef> addrDefs;
        std::vector<char> removed(bb.instrs.size(), 0);

        // Invalidate any recorded pattern whose inputs are redefined.
        const auto invalidateOnDef = [&](const MInstr &mi) {
            for (std::size_t oi = 0; oi < mi.ops.size(); ++oi) {
                const auto [isUse, isDef] = ra::operandRoles(mi, oi);
                (void)isUse;
                if (!isDef || mi.ops[oi].kind != MOperand::Kind::Reg)
                    continue;
                const auto &defReg = mi.ops[oi].reg;
                for (auto it = shiftDefs.begin(); it != shiftDefs.end();) {
                    if ((defReg.isPhys == it->second.src.reg.isPhys &&
                         defReg.cls == it->second.src.reg.cls &&
                         defReg.idOrPhys == it->second.src.reg.idOrPhys) ||
                        (!defReg.isPhys && defReg.cls == RegClass::GPR &&
                         defReg.idOrPhys == it->first)) {
                        it = shiftDefs.erase(it);
                    } else {
                        ++it;
                    }
                }
                for (auto it = addrDefs.begin(); it != addrDefs.end();) {
                    const auto &entry = it->second;
                    const auto matches = [&](const MOperand &src) {
                        return defReg.isPhys == src.reg.isPhys && defReg.cls == src.reg.cls &&
                               defReg.idOrPhys == src.reg.idOrPhys;
                    };
                    if (matches(entry.base) || matches(entry.index) ||
                        (!defReg.isPhys && defReg.cls == RegClass::GPR &&
                         defReg.idOrPhys == it->first)) {
                        it = addrDefs.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        };

        for (std::size_t i = 0; i < bb.instrs.size(); ++i) {
            MInstr &mi = bb.instrs[i];

            // Load/store through a single-use shift+add address: fold to the
            // scaled register-offset form when the amount is legal.
            const bool isBaseLd = mi.opc == MOpcode::LdrRegBaseImm ||
                                  mi.opc == MOpcode::Ldr32RegBaseImm ||
                                  mi.opc == MOpcode::LdrFprBaseImm;
            const bool isBaseSt = mi.opc == MOpcode::StrRegBaseImm ||
                                  mi.opc == MOpcode::Str32RegBaseImm ||
                                  mi.opc == MOpcode::StrFprBaseImm;
            if ((isBaseLd || isBaseSt) && mi.ops.size() >= 3 && isVGpr(mi.ops[1]) &&
                mi.ops[2].kind == MOperand::Kind::Imm && mi.ops[2].imm == 0) {
                auto addrIt = addrDefs.find(vregOf(mi.ops[1]));
                if (addrIt != addrDefs.end()) {
                    const bool is32 = mi.opc == MOpcode::Ldr32RegBaseImm ||
                                      mi.opc == MOpcode::Str32RegBaseImm;
                    const long long legal = is32 ? 2 : 3;
                    const long long amount = addrIt->second.amount;
                    if (amount == 0 || amount == legal) {
                        MOpcode replacement = MOpcode::LdrRegBaseRegLsl;
                        if (mi.opc == MOpcode::StrRegBaseImm)
                            replacement = MOpcode::StrRegBaseRegLsl;
                        else if (mi.opc == MOpcode::Ldr32RegBaseImm)
                            replacement = MOpcode::Ldr32RegBaseRegLsl;
                        else if (mi.opc == MOpcode::Str32RegBaseImm)
                            replacement = MOpcode::Str32RegBaseRegLsl;
                        else if (mi.opc == MOpcode::LdrFprBaseImm)
                            replacement = MOpcode::LdrFprBaseRegLsl;
                        else if (mi.opc == MOpcode::StrFprBaseImm)
                            replacement = MOpcode::StrFprBaseRegLsl;
                        const AddrDef entry = addrIt->second;
                        mi.opc = replacement;
                        mi.ops = {mi.ops[0], entry.base, entry.index, MOperand::immOp(entry.amount)};
                        removed[entry.idx] = 1;
                        removed[entry.shiftIdx] = 1;
                        addrDefs.erase(addrIt);
                        ++folded;
                        invalidateOnDef(mi);
                        continue;
                    }
                }
            }

            // Shift feeding an add: remember the pair as an address candidate
            // (a later load/store may absorb it) — or fold the ALU directly.
            if (mi.opc == MOpcode::AddRRR && mi.ops.size() >= 3 && isVGpr(mi.ops[0]) &&
                mi.ops[1].kind == MOperand::Kind::Reg && isVGpr(mi.ops[2])) {
                auto shiftIt = shiftDefs.find(vregOf(mi.ops[2]));
                if (shiftIt != shiftDefs.end() && singleDefUse(vregOf(mi.ops[2]))) {
                    const ShiftDef entry = shiftIt->second;
                    shiftDefs.erase(shiftIt);
                    // Invalidate BEFORE recording: this add's def must clear
                    // stale entries keyed by its destination, not the fresh
                    // candidate recorded below.
                    invalidateOnDef(mi);
                    if (singleDefUse(vregOf(mi.ops[0]))) {
                        // Candidate address computation; defer to a load fold.
                        addrDefs[vregOf(mi.ops[0])] =
                            AddrDef{i, entry.idx, mi.ops[1], entry.src, entry.amount};
                    } else {
                        // Fold the shift into the add's second operand.
                        mi.opc = MOpcode::AddRRRLsl;
                        mi.ops = {mi.ops[0], mi.ops[1], entry.src, MOperand::immOp(entry.amount)};
                        removed[entry.idx] = 1;
                        ++folded;
                    }
                    continue;
                }
            }

            // Sub/And/Orr/Eor with a single-use shifted second operand.
            if ((mi.opc == MOpcode::SubRRR || mi.opc == MOpcode::AndRRR ||
                 mi.opc == MOpcode::OrrRRR || mi.opc == MOpcode::EorRRR) &&
                mi.ops.size() >= 3 && isVGpr(mi.ops[2])) {
                auto shiftIt = shiftDefs.find(vregOf(mi.ops[2]));
                if (shiftIt != shiftDefs.end() && singleDefUse(vregOf(mi.ops[2]))) {
                    const ShiftDef entry = shiftIt->second;
                    mi.opc = mi.opc == MOpcode::SubRRR   ? MOpcode::SubRRRLsl
                             : mi.opc == MOpcode::AndRRR ? MOpcode::AndRRRLsl
                             : mi.opc == MOpcode::OrrRRR ? MOpcode::OrrRRRLsl
                                                         : MOpcode::EorRRRLsl;
                    mi.ops = {mi.ops[0], mi.ops[1], entry.src, MOperand::immOp(entry.amount)};
                    removed[entry.idx] = 1;
                    ++folded;
                    shiftDefs.erase(shiftIt);
                    invalidateOnDef(mi);
                    continue;
                }
            }

            // Record fresh shift defs AFTER matching so a shift never feeds
            // itself; calls and other clobbers invalidate via operand defs.
            if (ra::isCall(mi.opc)) {
                shiftDefs.clear();
                addrDefs.clear();
                continue;
            }
            invalidateOnDef(mi);
            if (mi.opc == MOpcode::LslRI && mi.ops.size() >= 3 && isVGpr(mi.ops[0]) &&
                mi.ops[1].kind == MOperand::Kind::Reg &&
                mi.ops[2].kind == MOperand::Kind::Imm && mi.ops[2].imm >= 0 &&
                mi.ops[2].imm < 64) {
                shiftDefs[vregOf(mi.ops[0])] = ShiftDef{i, mi.ops[1], mi.ops[2].imm};
            }
        }

        if (folded > 0) {
            std::vector<MInstr> kept;
            kept.reserve(bb.instrs.size());
            for (std::size_t i = 0; i < bb.instrs.size(); ++i) {
                if (!removed[i])
                    kept.push_back(std::move(bb.instrs[i]));
            }
            bb.instrs = std::move(kept);
        }
    }
    return folded;
}

} // namespace

std::size_t runPreRegAllocOpt(MFunction &fn) {
    const std::size_t forwarded = common::runPreRAForwardCopy<A64PreRATraits>(fn);
    // VIPER_NO_ADDR_FOLDS=1 disables the addressing folds for triage.
    if (std::getenv("VIPER_NO_ADDR_FOLDS") != nullptr)
        return forwarded;
    return forwarded + runAddressingFolds(fn);
}

} // namespace viper::codegen::aarch64
