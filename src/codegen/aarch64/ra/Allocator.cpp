//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/ra/Allocator.cpp
// Purpose: Core linear-scan register allocator implementation for AArch64.
//          Contains CFG construction, liveness analysis, spill logic,
//          register materialization, and per-block allocation.
//
// Key invariants:
//   - All virtual registers are resolved to physical registers after run().
//   - Spill/reload code is inserted in-place via prefix/suffix vectors.
//   - Cross-block persistence uses single-predecessor exit states.
//
// Ownership/Lifetime:
//   - See Allocator.hpp.
//
// Links: codegen/aarch64/ra/Allocator.hpp,
//        codegen/aarch64/ra/InstrBuilders.hpp,
//        codegen/aarch64/ra/OpcodeClassify.hpp,
//        codegen/aarch64/ra/OperandRoles.hpp
//
//===----------------------------------------------------------------------===//

#include "Allocator.hpp"

#include "InstrBuilders.hpp"
#include "OpcodeClassify.hpp"
#include "OperandRoles.hpp"
#include "il/runtime/RuntimeNameMap.hpp"

#include <algorithm>
#include <cassert>
#include <climits>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace viper::codegen::aarch64::ra {

namespace {

/// @brief Return true if @p pr is one of the reserved emergency scratch registers
///        (kScratchGPR/GPR2/GPR3 or kScratchFPR/FPR2) that must not be released to the pool.
bool isReservedScratch(PhysReg pr) {
    return pr == kScratchGPR || pr == kScratchGPR2 || pr == kScratchGPR3 || pr == kScratchFPR ||
           pr == kScratchFPR2;
}

/// @brief Return true if @p pr already appears in the @p scratch list (already acquired).
bool scratchAlreadyUsed(const std::vector<PhysReg> &scratch, PhysReg pr) {
    return std::find(scratch.begin(), scratch.end(), pr) != scratch.end();
}

/// @brief Pick the first available scratch register from @p candidates.
/// @details If @p canReuseDefScratch is true, prefers a register already in @p scratch
///          (safe when the use and def are the same operand slot). Otherwise picks one
///          not yet in @p scratch. Registers listed in @p blocked are never selected,
///          which prevents emergency scratch reloads from clobbering explicit physical
///          operands on the current instruction. Throws if all candidates conflict.
PhysReg chooseFromScratchSet(std::initializer_list<PhysReg> candidates,
                             bool canReuseDefScratch,
                             const std::vector<PhysReg> &scratch,
                             const std::unordered_set<PhysReg> &blocked) {
    if (canReuseDefScratch) {
        for (PhysReg candidate : candidates) {
            if (blocked.find(candidate) == blocked.end() && scratchAlreadyUsed(scratch, candidate)) {
                return candidate;
            }
        }
    }
    for (PhysReg candidate : candidates) {
        if (blocked.find(candidate) == blocked.end() && !scratchAlreadyUsed(scratch, candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("AArch64 register allocator: no reserved emergency scratch register "
                             "available for spilled operand reload");
}

/// @brief Select an emergency scratch register when the normal pool is exhausted.
/// @param fprClass         True to pick an FPR scratch; false for GPR.
/// @param canReuseDefScratch True when the spilled operand is def-only (safe to reuse).
/// @param scratch          Registers already acquired this instruction.
/// @param blocked          Explicit physical operands that must not be clobbered.
/// @return A reserved scratch register suitable for the operand class.
PhysReg chooseEmergencyScratch(bool fprClass,
                               bool canReuseDefScratch,
                               const std::vector<PhysReg> &scratch,
                               const std::unordered_set<PhysReg> &blocked) {
    if (fprClass)
        return chooseFromScratchSet({kScratchFPR, kScratchFPR2}, canReuseDefScratch, scratch, blocked);
    return chooseFromScratchSet(
        {kScratchGPR, kScratchGPR2, kScratchGPR3}, canReuseDefScratch, scratch, blocked);
}

} // namespace

// =========================================================================
// Construction
// =========================================================================

LinearAllocator::LinearAllocator(MFunction &fn, const TargetInfo &ti) : fn_(fn), ti_(ti), fb_(fn) {
    pools_.build(ti);
    liveness_.run(fn);
}

// =========================================================================
// Entry point
// =========================================================================

AllocationResult LinearAllocator::run() {
    blockExitStates_.resize(fn_.blocks.size());

    for (std::size_t bi = 0; bi < fn_.blocks.size(); ++bi) {
        currentBlockIdx_ = bi;
        restoreFromPredecessor(bi);
        allocateBlock(fn_.blocks[bi]);
        releaseBlockState();
    }

    fb_.finalize();
    recordCalleeSavedUsage();

    return AllocationResult{};
}

// =========================================================================
// CFG / liveness — delegated to LivenessAnalysis (uses shared solver)
// =========================================================================

// =========================================================================
// Cross-block register persistence
// =========================================================================

void LinearAllocator::restoreFromPredecessor(std::size_t bi) {
    const auto &preds = liveness_.predecessors(bi);
    if (preds.size() != 1)
        return;

    const std::size_t pred = preds[0];
    if (pred >= bi)
        return;

    const auto &es = blockExitStates_[pred];

    // Restore in sorted vreg order so register-pool mutation order is
    // independent of hash-map iteration order (deterministic codegen).
    const auto sortedKeys = [](const std::unordered_map<uint16_t, PhysReg> &map) {
        std::vector<uint16_t> keys;
        keys.reserve(map.size());
        for (const auto &kv : map)
            keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        return keys;
    };

    for (const uint16_t vid : sortedKeys(es.gpr)) {
        const PhysReg phys = es.gpr.at(vid);

        auto it = std::find(pools_.gprFree.begin(), pools_.gprFree.end(), phys);
        if (it == pools_.gprFree.end()) {
            auto &st = gprStates_[vid];
            st.hasPhys = false;
            st.spilled = true;
            continue;
        }
        pools_.gprFree.erase(it);

        auto &st = gprStates_[vid];
        st.hasPhys = true;
        st.phys = phys;
        st.spilled = false;
        st.dirty = false;
    }

    for (const uint16_t vid : sortedKeys(es.fpr)) {
        const PhysReg phys = es.fpr.at(vid);

        auto it = std::find(pools_.fprFree.begin(), pools_.fprFree.end(), phys);
        if (it == pools_.fprFree.end()) {
            auto &st = fprStates_[vid];
            st.hasPhys = false;
            st.spilled = true;
            continue;
        }
        pools_.fprFree.erase(it);

        auto &st = fprStates_[vid];
        st.hasPhys = true;
        st.phys = phys;
        st.spilled = false;
        st.dirty = false;
    }
}

// =========================================================================
// Next-use analysis
// =========================================================================

void LinearAllocator::computeNextUses(const MBasicBlock &bb) {
    usePositionsGPR_.clear();
    usePositionsFPR_.clear();
    callPositions_.clear();

    unsigned idx = 0;
    for (const auto &mi : bb.instrs) {
        if (isCall(mi.opc))
            callPositions_.push_back(idx);

        for (std::size_t opIdx = 0; opIdx < mi.ops.size(); ++opIdx) {
            const auto &op = mi.ops[opIdx];
            if (op.kind != MOperand::Kind::Reg || op.reg.isPhys)
                continue;
            const auto [isUse, isDef] = operandRoles(mi, opIdx);
            (void)isDef;
            if (!isUse)
                continue;
            if (op.reg.cls == RegClass::GPR)
                usePositionsGPR_[op.reg.idOrPhys].push_back(idx);
            else
                usePositionsFPR_[op.reg.idOrPhys].push_back(idx);
        }
        ++idx;
    }
}

bool LinearAllocator::isProtectedOperand(uint16_t vreg, RegClass cls) const {
    if (cls == RegClass::GPR)
        return protectedOperandGPR_.find(vreg) != protectedOperandGPR_.end();
    return protectedOperandFPR_.find(vreg) != protectedOperandFPR_.end();
}

bool LinearAllocator::isProtectedPhys(PhysReg phys, RegClass cls) const {
    if (cls == RegClass::GPR)
        return protectedPhysGPR_.find(phys) != protectedPhysGPR_.end();
    return protectedPhysFPR_.find(phys) != protectedPhysFPR_.end();
}

bool LinearAllocator::isLiveOut(uint16_t vreg, RegClass cls) const {
    if (cls == RegClass::GPR)
        return liveness_.liveOutGPR(currentBlockIdx_).contains(vreg);
    return liveness_.liveOutFPR(currentBlockIdx_).contains(vreg);
}

bool LinearAllocator::nextUseAfterCall(uint16_t vreg, RegClass cls) const {
    const auto &map = (cls == RegClass::GPR) ? usePositionsGPR_ : usePositionsFPR_;
    auto it = map.find(vreg);
    if (it == map.end() || it->second.empty())
        return false;

    unsigned lastUse = 0;
    for (auto posIt = it->second.rbegin(); posIt != it->second.rend(); ++posIt) {
        if (*posIt > currentInstrIdx_) {
            lastUse = *posIt;
            break;
        }
    }
    if (lastUse == 0)
        return false;

    for (unsigned callIdx : callPositions_) {
        if (callIdx > currentInstrIdx_ && callIdx < lastUse)
            return true;
    }
    return false;
}

unsigned LinearAllocator::getNextUseDistance(uint16_t vreg, RegClass cls) const {
    const auto &map = (cls == RegClass::GPR) ? usePositionsGPR_ : usePositionsFPR_;
    auto it = map.find(vreg);
    if (it == map.end())
        return UINT_MAX;

    const auto &positions = it->second;
    auto pos = std::upper_bound(positions.begin(), positions.end(), currentInstrIdx_);
    if (pos == positions.end())
        return UINT_MAX;
    return *pos - currentInstrIdx_;
}

unsigned LinearAllocator::computeSpillLastUse(uint16_t vreg,
                                              RegClass cls,
                                              bool forceLiveOut) const {
    const auto &posMap = (cls == RegClass::GPR) ? usePositionsGPR_ : usePositionsFPR_;
    unsigned trueLastUse = currentInstrIdx_;
    auto posIt = posMap.find(vreg);
    if (posIt != posMap.end() && !posIt->second.empty())
        trueLastUse = std::max(trueLastUse, posIt->second.back());
    if (forceLiveOut || isLiveOut(vreg, cls))
        trueLastUse = std::max(trueLastUse, currentBlockInstrCount_);
    return trueLastUse;
}

int LinearAllocator::ensureCurrentSpillSlot(uint16_t vreg, RegClass cls, bool forceLiveOut) {
    return fb_.ensureSpillWithReuse(
        vreg, computeSpillLastUse(vreg, cls, forceLiveOut), currentInstrIdx_);
}

// =========================================================================
// Spilling
// =========================================================================

void LinearAllocator::spillVictim(RegClass cls, uint16_t id, std::vector<MInstr> &prefix) {
    auto &st = (cls == RegClass::GPR) ? gprStates_[id] : fprStates_[id];
    if (!st.hasPhys)
        return;

    const unsigned nextUseDist = getNextUseDistance(id, cls);
    const bool liveOut = isLiveOut(id, cls);
    if (nextUseDist == UINT_MAX && !liveOut) {
        if (cls == RegClass::GPR)
            pools_.releaseGPR(st.phys, ti_);
        else
            pools_.releaseFPR(st.phys, ti_);
        st.hasPhys = false;
        st.spilled = false;
        st.dirty = false;
        return;
    }

    if (st.dirty || st.fpOffset == 0) {
        const int off = ensureCurrentSpillSlot(id, cls);
        st.fpOffset = off;
        if (cls == RegClass::GPR)
            prefix.push_back(makeStrFp(st.phys, off));
        else
            prefix.push_back(
                MInstr{MOpcode::StrFprFpImm, {MOperand::regOp(st.phys), MOperand::immOp(off)}});
        st.dirty = false;
    }

    if (cls == RegClass::GPR)
        pools_.releaseGPR(st.phys, ti_);
    else
        pools_.releaseFPR(st.phys, ti_);
    st.hasPhys = false;
    st.spilled = true;
}

uint16_t LinearAllocator::selectLRUVictim(RegClass cls) {
    auto &states = (cls == RegClass::GPR) ? gprStates_ : fprStates_;
    uint16_t victim = UINT16_MAX;
    unsigned oldestUse = UINT_MAX;

    // Tie-break equal lastUse values on the smaller vreg id: hash-map
    // iteration order differs between STL implementations, and the victim
    // choice must not (deterministic codegen across platforms).
    for (auto &kv : states) {
        if (isProtectedOperand(kv.first, cls))
            continue;
        if (kv.second.hasPhys && isProtectedPhys(kv.second.phys, cls))
            continue;
        if (!kv.second.hasPhys)
            continue;
        if (kv.second.lastUse < oldestUse ||
            (kv.second.lastUse == oldestUse && kv.first < victim)) {
            oldestUse = kv.second.lastUse;
            victim = kv.first;
        }
    }
    return victim;
}

uint16_t LinearAllocator::selectFurthestVictim(RegClass cls) {
    auto &states = (cls == RegClass::GPR) ? gprStates_ : fprStates_;
    uint16_t victim = UINT16_MAX;
    unsigned furthestDist = 0;

    // Tie-break equal next-use distances on the smaller vreg id so the
    // selection is independent of hash-map iteration order.
    for (auto &kv : states) {
        if (!kv.second.hasPhys)
            continue;
        if (isProtectedOperand(kv.first, cls))
            continue;
        if (isProtectedPhys(kv.second.phys, cls))
            continue;

        unsigned dist = getNextUseDistance(kv.first, cls);
        if (dist > furthestDist ||
            (dist == furthestDist && victim != UINT16_MAX && kv.first < victim)) {
            furthestDist = dist;
            victim = kv.first;
        }
    }

    if (victim == UINT16_MAX)
        return selectLRUVictim(cls);

    return victim;
}

void LinearAllocator::maybeSpillForPressure(RegClass cls, std::vector<MInstr> &prefix) {
    if (cls == RegClass::GPR) {
        if (!pools_.gprFree.empty())
            return;
        uint16_t victim = selectFurthestVictim(RegClass::GPR);
        if (victim != UINT16_MAX)
            spillVictim(RegClass::GPR, victim, prefix);
    } else {
        if (!pools_.fprFree.empty())
            return;
        uint16_t victim = selectFurthestVictim(RegClass::FPR);
        if (victim != UINT16_MAX)
            spillVictim(RegClass::FPR, victim, prefix);
    }
}

// =========================================================================
// Register materialization
// =========================================================================

void LinearAllocator::materialize(MReg &r,
                                  bool isUse,
                                  bool isDef,
                                  std::vector<MInstr> &prefix,
                                  std::vector<MInstr> &suffix,
                                  std::vector<PhysReg> &scratch) {
    if (r.isPhys) {
        trackCalleeSavedPhys(static_cast<PhysReg>(r.idOrPhys));
        return;
    }

    const bool fprClass = (r.cls == RegClass::FPR);
    auto &st = fprClass ? fprStates_[r.idOrPhys] : gprStates_[r.idOrPhys];

    if (isUse || isDef)
        st.lastUse = currentInstrIdx_;

    if (st.spilled) {
        handleSpilledOperand(r, fprClass, isUse, isDef, prefix, suffix, scratch);
        return;
    }

    if (!st.hasPhys) {
        assignNewPhysReg(st, r.idOrPhys, fprClass);
    }

    if (isDef)
        st.dirty = true;

    r.isPhys = true;
    r.idOrPhys = static_cast<uint16_t>(st.phys);
}

void LinearAllocator::handleSpilledOperand(MReg &r,
                                           bool fprClass,
                                           bool isUse,
                                           bool isDef,
                                           std::vector<MInstr> &prefix,
                                           std::vector<MInstr> &suffix,
                                           std::vector<PhysReg> &scratch) {
    PhysReg tmp{};
    try {
        tmp = fprClass ? pools_.takeFPR() : pools_.takeGPR();
    } catch (const std::runtime_error &) {
        const auto &blocked = fprClass ? protectedPhysFPR_ : protectedPhysGPR_;
        tmp = chooseEmergencyScratch(fprClass, isDef && !isUse, scratch, blocked);
    }
    auto &st = fprClass ? fprStates_[r.idOrPhys] : gprStates_[r.idOrPhys];
    const int off = st.fpOffset != 0 ? st.fpOffset : fb_.ensureSpill(r.idOrPhys);
    st.fpOffset = off;

    if (isUse) {
        if (fprClass)
            prefix.push_back(
                MInstr{MOpcode::LdrFprFpImm, {MOperand::regOp(tmp), MOperand::immOp(off)}});
        else
            prefix.push_back(makeLdrFp(tmp, off));
    }

    if (isDef) {
        if (fprClass)
            suffix.push_back(
                MInstr{MOpcode::StrFprFpImm, {MOperand::regOp(tmp), MOperand::immOp(off)}});
        else
            suffix.push_back(makeStrFp(tmp, off));
    }

    r.isPhys = true;
    r.idOrPhys = static_cast<uint16_t>(tmp);
    scratch.push_back(tmp);
}

void LinearAllocator::assignNewPhysReg(VState &st, uint16_t vregId, bool fprClass) {
    PhysReg phys;
    if (fprClass) {
        if (!fn_.isLeaf && nextUseAfterCall(vregId, RegClass::FPR))
            phys = pools_.takeFPRPreferCalleeSaved(ti_);
        else
            phys = pools_.takeFPR();
    } else {
        if (!fn_.isLeaf && nextUseAfterCall(vregId, RegClass::GPR))
            phys = pools_.takeGPRPreferCalleeSaved(ti_);
        else
            phys = pools_.takeGPR();
    }

    st.hasPhys = true;
    st.phys = phys;

    if (!fprClass) {
        if (std::find(ti_.calleeSavedGPR.begin(), ti_.calleeSavedGPR.end(), phys) !=
            ti_.calleeSavedGPR.end()) {
            pools_.calleeUsed[static_cast<std::size_t>(phys)] = true;
        }
    } else {
        if (std::find(ti_.calleeSavedFPR.begin(), ti_.calleeSavedFPR.end(), phys) !=
            ti_.calleeSavedFPR.end()) {
            pools_.calleeUsedFPR[static_cast<std::size_t>(phys)] = true;
        }
    }
}

void LinearAllocator::trackCalleeSavedPhys(PhysReg pr) {
    if (isGPR(pr)) {
        if (std::find(ti_.calleeSavedGPR.begin(), ti_.calleeSavedGPR.end(), pr) !=
            ti_.calleeSavedGPR.end()) {
            pools_.calleeUsed[static_cast<std::size_t>(pr)] = true;
        }
    } else if (isFPR(pr)) {
        if (std::find(ti_.calleeSavedFPR.begin(), ti_.calleeSavedFPR.end(), pr) !=
            ti_.calleeSavedFPR.end()) {
            pools_.calleeUsedFPR[static_cast<std::size_t>(pr)] = true;
        }
    }
}

bool LinearAllocator::isCalleeSaved(PhysReg pr, RegClass cls) const noexcept {
    if (cls == RegClass::GPR) {
        return std::find(ti_.calleeSavedGPR.begin(), ti_.calleeSavedGPR.end(), pr) !=
               ti_.calleeSavedGPR.end();
    } else {
        return std::find(ti_.calleeSavedFPR.begin(), ti_.calleeSavedFPR.end(), pr) !=
               ti_.calleeSavedFPR.end();
    }
}

// =========================================================================
// Block allocation
// =========================================================================

void LinearAllocator::allocateBlock(MBasicBlock &bb) {
    fb_.beginNewBlock();

    computeNextUses(bb);
    currentInstrIdx_ = 0;
    currentBlockInstrCount_ = static_cast<unsigned>(bb.instrs.size());

    std::vector<MInstr> rewritten;
    rewritten.reserve(bb.instrs.size());

    for (auto &ins : bb.instrs) {
        if (isSpAdj(ins.opc) || isBranch(ins.opc)) {
            rewritten.push_back(ins);
            ++currentInstrIdx_;
            continue;
        }

        if (isCall(ins.opc)) {
            handleCall(ins, rewritten);
            ++currentInstrIdx_;
            continue;
        }

        allocateInstruction(ins, rewritten);
        ++currentInstrIdx_;
    }

    if (!rewritten.empty()) {
        std::vector<MInstr> endSpills;
        const std::size_t bi = currentBlockIdx_;

        // liveOut sets are unordered; process them in sorted vreg order so the
        // emitted spill sequence and the register-pool release order do not
        // depend on the STL implementation's hash iteration order.
        const auto sortedLiveOut = [](const std::unordered_set<uint16_t> &set) {
            std::vector<uint16_t> sorted(set.begin(), set.end());
            std::sort(sorted.begin(), sorted.end());
            return sorted;
        };
        const std::vector<uint16_t> loGPR = sortedLiveOut(liveness_.liveOutGPR(bi));
        const std::vector<uint16_t> loFPR = sortedLiveOut(liveness_.liveOutFPR(bi));

        if (bi < blockExitStates_.size()) {
            auto &es = blockExitStates_[bi];
            for (auto vid : loGPR) {
                auto it = gprStates_.find(vid);
                if (it != gprStates_.end() && it->second.hasPhys)
                    es.gpr[vid] = it->second.phys;
            }
            for (auto vid : loFPR) {
                auto it = fprStates_.find(vid);
                if (it != fprStates_.end() && it->second.hasPhys)
                    es.fpr[vid] = it->second.phys;
            }
        }

        for (auto vid : loGPR) {
            auto it = gprStates_.find(vid);
            if (it != gprStates_.end() && it->second.hasPhys) {
                if (it->second.dirty || it->second.fpOffset == 0) {
                    const int off =
                        ensureCurrentSpillSlot(vid, RegClass::GPR, /*forceLiveOut=*/true);
                    it->second.fpOffset = off;
                    endSpills.push_back(makeStrFp(it->second.phys, off));
                    it->second.dirty = false;
                }
                pools_.releaseGPR(it->second.phys, ti_);
                it->second.hasPhys = false;
                it->second.spilled = true;
            }
        }
        for (auto vid : loFPR) {
            auto it = fprStates_.find(vid);
            if (it != fprStates_.end() && it->second.hasPhys) {
                if (it->second.dirty || it->second.fpOffset == 0) {
                    const int off =
                        ensureCurrentSpillSlot(vid, RegClass::FPR, /*forceLiveOut=*/true);
                    it->second.fpOffset = off;
                    endSpills.push_back(
                        MInstr{MOpcode::StrFprFpImm,
                               {MOperand::regOp(it->second.phys), MOperand::immOp(off)}});
                    it->second.dirty = false;
                }
                pools_.releaseFPR(it->second.phys, ti_);
                it->second.hasPhys = false;
                it->second.spilled = true;
            }
        }
        if (!endSpills.empty()) {
            std::size_t insertPos = rewritten.size();
            while (insertPos > 0 && isTerminator(rewritten[insertPos - 1].opc))
                --insertPos;
            rewritten.insert(rewritten.begin() + static_cast<long>(insertPos),
                             endSpills.begin(),
                             endSpills.end());
        }
    }
    bb.instrs = std::move(rewritten);
}

void LinearAllocator::handleCall(MInstr &ins, std::vector<MInstr> &rewritten) {
    bool isArrayObjGet = false;
    if (!ins.ops.empty() && ins.ops[0].kind == MOperand::Kind::Label) {
        std::string label = ins.ops[0].label;
        if (auto mapped = il::runtime::mapCanonicalRuntimeName(label))
            label = std::string(*mapped);
        if (label == "rt_arr_obj_get")
            isArrayObjGet = true;
    }

    // Spill order is part of the emitted instruction stream, so iterate vregs
    // in sorted order rather than hash-map order: iteration order differs
    // between STL implementations and codegen must stay byte-identical
    // across platforms.
    std::vector<MInstr> preCall;
    const auto spillCallerSavedResidents = [&](RegClass cls) {
        auto &states = (cls == RegClass::GPR) ? gprStates_ : fprStates_;
        std::vector<uint16_t> resident;
        resident.reserve(states.size());
        for (auto &kv : states) {
            if (kv.second.hasPhys && !isCalleeSaved(kv.second.phys, cls))
                resident.push_back(kv.first);
        }
        std::sort(resident.begin(), resident.end());
        for (uint16_t vid : resident) {
            auto &st = states[vid];
            const bool liveOut = isLiveOut(vid, cls);
            const unsigned dist = getNextUseDistance(vid, cls);
            if (dist == UINT_MAX && !liveOut) {
                if (cls == RegClass::GPR)
                    pools_.releaseGPR(st.phys, ti_);
                else
                    pools_.releaseFPR(st.phys, ti_);
                st.hasPhys = false;
            } else {
                spillVictim(cls, vid, preCall);
            }
        }
    };
    spillCallerSavedResidents(RegClass::GPR);
    spillCallerSavedResidents(RegClass::FPR);
    for (auto &mi : preCall)
        rewritten.push_back(std::move(mi));

    std::vector<MInstr> callPrefix;
    std::vector<MInstr> callSuffix;
    std::vector<PhysReg> callScratch;
    std::vector<std::pair<uint16_t, RegClass>> virtualCallOperands;

    protectedOperandGPR_.clear();
    protectedOperandFPR_.clear();
    protectedPhysGPR_.clear();
    protectedPhysFPR_.clear();
    for (std::size_t i = 0; i < ins.ops.size(); ++i) {
        auto &op = ins.ops[i];
        const auto [isUse, isDef] = operandRoles(ins, i);
        if ((!isUse && !isDef) || op.kind != MOperand::Kind::Reg)
            continue;
        if (op.reg.isPhys) {
            const PhysReg phys = static_cast<PhysReg>(op.reg.idOrPhys);
            if (op.reg.cls == RegClass::GPR)
                protectedPhysGPR_.insert(phys);
            else
                protectedPhysFPR_.insert(phys);
            trackCalleeSavedPhys(phys);
            continue;
        }

        virtualCallOperands.push_back({op.reg.idOrPhys, op.reg.cls});
        if (op.reg.cls == RegClass::GPR)
            protectedOperandGPR_.insert(op.reg.idOrPhys);
        else
            protectedOperandFPR_.insert(op.reg.idOrPhys);
    }

    for (std::size_t i = 0; i < ins.ops.size(); ++i) {
        auto &op = ins.ops[i];
        const auto [isUse, isDef] = operandRoles(ins, i);
        if (op.kind == MOperand::Kind::Reg && !op.reg.isPhys) {
            maybeSpillForPressure(op.reg.cls, callPrefix);
            materialize(op.reg, isUse, isDef, callPrefix, callSuffix, callScratch);
        }
    }
    for (const auto &[vreg, cls] : virtualCallOperands) {
        auto &states = cls == RegClass::GPR ? gprStates_ : fprStates_;
        auto it = states.find(vreg);
        if (it == states.end() || !it->second.hasPhys || isCalleeSaved(it->second.phys, cls))
            continue;
        const bool neededLater = isLiveOut(vreg, cls) || getNextUseDistance(vreg, cls) != UINT_MAX;
        if (!neededLater)
            continue;
        if (it->second.fpOffset == 0 || it->second.dirty) {
            const int off = ensureCurrentSpillSlot(vreg, cls);
            it->second.fpOffset = off;
            if (cls == RegClass::GPR)
                callPrefix.push_back(makeStrFp(it->second.phys, off));
            else
                callPrefix.push_back(MInstr{
                    MOpcode::StrFprFpImm, {MOperand::regOp(it->second.phys), MOperand::immOp(off)}});
            it->second.dirty = false;
        }
    }
    for (auto &mi : callPrefix)
        rewritten.push_back(std::move(mi));
    rewritten.push_back(ins);
    for (auto &mi : callSuffix)
        rewritten.push_back(std::move(mi));
    for (const auto &[vreg, cls] : virtualCallOperands)
        retireCallOperandAfterCall(vreg, cls);
    releaseScratch(callScratch);
    protectedOperandGPR_.clear();
    protectedOperandFPR_.clear();
    protectedPhysGPR_.clear();
    protectedPhysFPR_.clear();
    if (isArrayObjGet)
        pendingGetBarrier_ = true;
}

void LinearAllocator::retireCallOperandAfterCall(uint16_t vreg, RegClass cls) {
    auto &states = cls == RegClass::GPR ? gprStates_ : fprStates_;
    auto it = states.find(vreg);
    if (it == states.end() || !it->second.hasPhys)
        return;

    auto &st = it->second;
    if (isCalleeSaved(st.phys, cls))
        return;

    const bool neededLater = isLiveOut(vreg, cls) || getNextUseDistance(vreg, cls) != UINT_MAX;
    if (cls == RegClass::GPR)
        pools_.releaseGPR(st.phys, ti_);
    else
        pools_.releaseFPR(st.phys, ti_);
    st.hasPhys = false;
    st.spilled = neededLater;
    st.dirty = false;
}

void LinearAllocator::allocateInstruction(MInstr &ins, std::vector<MInstr> &rewritten) {
    const bool isPhiStore = (ins.opc == MOpcode::PhiStoreGPR || ins.opc == MOpcode::PhiStoreFPR);
    const bool phiSrcIsFPR = (ins.opc == MOpcode::PhiStoreFPR);

    std::vector<MInstr> prefix;
    bool applyGetBarrier = false;
    uint16_t getBarrierDstVreg = 0;
    if (pendingGetBarrier_ && ins.opc == MOpcode::MovRR && ins.ops.size() >= 2) {
        const auto &dst = ins.ops[0];
        const auto &src = ins.ops[1];
        if (dst.kind == MOperand::Kind::Reg && !dst.reg.isPhys && src.kind == MOperand::Kind::Reg &&
            src.reg.isPhys && static_cast<PhysReg>(src.reg.idOrPhys) == PhysReg::X0) {
            applyGetBarrier = true;
            getBarrierDstVreg = dst.reg.idOrPhys;
            pendingGetBarrier_ = false;
        } else {
            pendingGetBarrier_ = false;
        }
    }
    std::vector<MInstr> suffix;
    std::vector<PhysReg> scratch;

    protectedOperandGPR_.clear();
    protectedOperandFPR_.clear();
    protectedPhysGPR_.clear();
    protectedPhysFPR_.clear();
    for (std::size_t i = 0; i < ins.ops.size(); ++i) {
        const auto &op = ins.ops[i];
        const auto [isUse, isDef] = operandRoles(ins, i);
        if ((!isUse && !isDef) || op.kind != MOperand::Kind::Reg)
            continue;
        if (op.reg.isPhys) {
            const PhysReg phys = static_cast<PhysReg>(op.reg.idOrPhys);
            if (op.reg.cls == RegClass::GPR)
                protectedPhysGPR_.insert(phys);
            else
                protectedPhysFPR_.insert(phys);
            continue;
        }
        if (op.reg.cls == RegClass::GPR)
            protectedOperandGPR_.insert(op.reg.idOrPhys);
        else
            protectedOperandFPR_.insert(op.reg.idOrPhys);
    }

    for (std::size_t i = 0; i < ins.ops.size(); ++i) {
        auto &op = ins.ops[i];
        auto [isUse, isDef] = operandRoles(ins, i);
        if (op.kind == MOperand::Kind::Reg) {
            if (op.reg.isPhys) {
                materialize(op.reg, isUse, isDef, prefix, suffix, scratch);
                continue;
            }
            maybeSpillForPressure(op.reg.cls, prefix);
            materialize(op.reg, isUse, isDef, prefix, suffix, scratch);
        }
    }

    if (applyGetBarrier) {
        auto it = gprStates_.find(getBarrierDstVreg);
        if (it != gprStates_.end() && it->second.hasPhys) {
            const int off = ensureCurrentSpillSlot(getBarrierDstVreg, RegClass::GPR);
            it->second.fpOffset = off;
            suffix.push_back(makeStrFp(it->second.phys, off));
            pools_.releaseGPR(it->second.phys, ti_);
            it->second.hasPhys = false;
            it->second.spilled = true;
        }
    }

    if (isPhiStore) {
        // PhiStore writes the source register into the phi DESTINATION's
        // slot. It says nothing about the source vreg's own spill slot, so
        // the source's dirty bit must be left untouched: clearing it here
        // would let a later spill skip the store and leave the source's slot
        // stale. (Today lowering mints single-def per-block vregs, which
        // masks the hazard, but the invariant should not depend on that.)
        ins.opc = phiSrcIsFPR ? MOpcode::StrFprFpImm : MOpcode::StrRegFpImm;
    }

    for (auto &pre : prefix)
        rewritten.push_back(std::move(pre));
    rewritten.push_back(std::move(ins));
    for (auto &suf : suffix)
        rewritten.push_back(std::move(suf));

    releaseScratch(scratch);
    protectedOperandGPR_.clear();
    protectedOperandFPR_.clear();
    protectedPhysGPR_.clear();
    protectedPhysFPR_.clear();
}

// =========================================================================
// Cleanup
// =========================================================================

void LinearAllocator::releaseScratch(std::vector<PhysReg> &scratch) {
    for (auto pr : scratch) {
        if (isReservedScratch(pr))
            continue;
        if (isGPR(pr)) {
            if (std::find(ti_.calleeSavedGPR.begin(), ti_.calleeSavedGPR.end(), pr) !=
                ti_.calleeSavedGPR.end())
                pools_.calleeUsed[static_cast<std::size_t>(pr)] = true;
            pools_.releaseGPR(pr, ti_);
        } else {
            if (std::find(ti_.calleeSavedFPR.begin(), ti_.calleeSavedFPR.end(), pr) !=
                ti_.calleeSavedFPR.end())
                pools_.calleeUsedFPR[static_cast<std::size_t>(pr)] = true;
            pools_.releaseFPR(pr, ti_);
        }
    }
}

void LinearAllocator::releaseBlockState() {
    // Release in sorted vreg order: the pool is a FIFO, so the order registers
    // return to it shapes every later allocation. Hash-map iteration order
    // would make the emitted code differ between STL implementations.
    const auto releaseAll = [&](RegClass cls) {
        auto &states = (cls == RegClass::GPR) ? gprStates_ : fprStates_;
        std::vector<uint16_t> resident;
        resident.reserve(states.size());
        for (auto &kv : states) {
            if (kv.second.hasPhys)
                resident.push_back(kv.first);
        }
        std::sort(resident.begin(), resident.end());
        for (uint16_t vid : resident) {
            auto &st = states[vid];
            if (cls == RegClass::GPR)
                pools_.releaseGPR(st.phys, ti_);
            else
                pools_.releaseFPR(st.phys, ti_);
            st.hasPhys = false;
            if (st.dirty) {
                st.spilled = false;
            } else if (st.fpOffset != 0) {
                st.spilled = true;
            }
        }
    };
    releaseAll(RegClass::GPR);
    releaseAll(RegClass::FPR);
}

void LinearAllocator::recordCalleeSavedUsage() {
    fn_.savedGPRs.clear();
    fn_.savedFPRs.clear();
    for (auto r : ti_.calleeSavedGPR) {
        if (pools_.calleeUsed[static_cast<std::size_t>(r)])
            fn_.savedGPRs.push_back(r);
    }
    for (auto r : ti_.calleeSavedFPR) {
        if (pools_.calleeUsedFPR[static_cast<std::size_t>(r)])
            fn_.savedFPRs.push_back(r);
    }
}

} // namespace viper::codegen::aarch64::ra
