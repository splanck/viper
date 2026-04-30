//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/SchedulerPass.cpp
// Purpose: Post-RA list scheduler for the AArch64 modular pipeline.
//
// Algorithm:
//   For each basic block in each MIR function:
//   1. Partition instructions into non-terminator body segments separated by
//      calls and mid-block guard branches, plus the final terminator group.
//   2. Build a data-dependency DAG from physical-register def/use chains.
//      Memory dependencies are tracked precisely only for explicit FP/SP-derived
//      stack addresses. Base-register heap/object accesses are conservatively
//      treated as may-alias because different registers or spill slots can hold
//      the same runtime object/list pointer.
//   3. Assign latencies to each dependency edge using a simplified Apple M1
//      latency model (loads: 4 cycles, multiplies: 3 cycles, FP: 3 cycles,
//      all other: 1 cycle).
//   4. Compute the critical-path length of each node (backward sum of latencies).
//   5. Greedy list scheduling: maintain a ready-list (all predecessors scheduled)
//      and repeatedly select the ready instruction with the highest critical-path
//      priority.  On tie, prefer the original program order.
//   6. Append terminators in their original relative order.
//
// Invariant: the reordered block contains the same multiset of instructions;
//            no instructions are added or removed.
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/SchedulerPass.hpp"

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/aarch64/ra/OperandRoles.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <queue>
#include <vector>

namespace viper::codegen::aarch64::passes {

namespace {

// ---------------------------------------------------------------------------
// Latency model
// ---------------------------------------------------------------------------

/// @brief Return the output latency (cycles from write to use) for an opcode.
///
/// Latency model tuned for Apple M1/M2/M3 Firestorm (performance) cores.
/// Values are approximate; actual latencies vary by microarchitecture revision
/// and operand width but are close enough for effective scheduling.
static unsigned instrLatency(MOpcode opc) noexcept {
    switch (opc) {
        // Loads: L1 hit ~4 cycles on Apple M-series.
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::LdpRegFpImm:
        case MOpcode::LdpFprFpImm:
            return 4;

        // Integer multiply + fused multiply-add/sub: 3 cycles.
        case MOpcode::MulRRR:
        case MOpcode::MSubRRRR:
        case MOpcode::MAddRRRR:
            return 3;

        // Integer divide: 7-12 cycles on M1; model as 7 (optimistic).
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
            return 7;

        // FP add/sub: 3 cycles.
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
            return 3;

        // FP multiply: 4 cycles on M1.
        case MOpcode::FMulRRR:
            return 4;

        // FP divide: 10-15 cycles on M1 (double precision); model as 10.
        case MOpcode::FDivRRR:
            return 10;

        // Int/FP conversions: 3-4 cycles.
        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
            return 4;

        // All other instructions: 1 cycle.
        default:
            return 1;
    }
}

// ---------------------------------------------------------------------------
// Memory classification helpers
// ---------------------------------------------------------------------------

static bool isLoad(MOpcode opc) noexcept {
    switch (opc) {
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::LdpRegFpImm:
        case MOpcode::LdpFprFpImm:
            return true;
        default:
            return false;
    }
}

static bool isStore(MOpcode opc) noexcept {
    switch (opc) {
        case MOpcode::StrRegFpImm:
        case MOpcode::StrRegBaseImm:
        case MOpcode::StrRegSpImm:
        case MOpcode::StrFprFpImm:
        case MOpcode::StrFprBaseImm:
        case MOpcode::StrFprSpImm:
        case MOpcode::StpRegFpImm:
        case MOpcode::StpFprFpImm:
            return true;
        default:
            return false;
    }
}

enum class MemBaseKind : uint8_t {
    FramePointer,
    StackPointer,
    BaseRegister,
};

struct AddressValue {
    MemBaseKind baseKind{MemBaseKind::BaseRegister};
    long long baseTag{0};
    long long offset{0};
};

struct MemoryAccessClass {
    MemBaseKind baseKind{MemBaseKind::FramePointer};
    long long baseTag{0};
    long long offset{0};
    unsigned size{0};
};

struct MemoryHistoryEntry {
    std::size_t instrIdx{0};
    bool isLoad{false};
    bool isStore{false};
    bool isBarrier{false};
    std::optional<MemoryAccessClass> accessClass;
};

/// Total number of AArch64 physical registers (X0..X30, SP, V0..V31 = 64).
static constexpr std::size_t kNumPhysRegs = 64;

/// Flat-array indices for implicit "virtual" registers beyond the physical file.
static constexpr std::size_t kIdxNZCV = kNumPhysRegs;   // 64
static constexpr std::size_t kIdxSP = kNumPhysRegs + 1; // 65

/// Total tracked register slots: 64 physical + NZCV + SP.
static constexpr std::size_t kNumTracked = kNumPhysRegs + 2;

/// Map a physical register ID (or sentinel) to a flat-array index.
static std::size_t regIdx(uint32_t reg) noexcept {
    // PhysReg enum values are 0..63, which map to themselves.
    return static_cast<std::size_t>(reg);
}

static std::optional<MemoryAccessClass>
makeResolvedAccessClass(const AddressValue &base, long long accessOffset, unsigned size) noexcept {
    return MemoryAccessClass{base.baseKind, base.baseTag, base.offset + accessOffset, size};
}

static std::optional<AddressValue>
getTrackedAddressValue(const std::array<std::optional<AddressValue>, kNumPhysRegs> &tracked,
                       uint32_t reg) noexcept {
    const std::size_t idx = regIdx(reg);
    if (idx >= kNumPhysRegs)
        return std::nullopt;
    return tracked[idx];
}

static std::optional<MemoryAccessClass>
classifyMemoryAccess(const MInstr &mi,
                     const std::array<std::optional<AddressValue>, kNumPhysRegs> &trackedAddrs)
    noexcept {
    switch (mi.opc) {
        case MOpcode::LdrRegFpImm:
        case MOpcode::StrRegFpImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::StrFprFpImm:
            if (mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Imm)
                return MemoryAccessClass{MemBaseKind::FramePointer, 0, mi.ops[1].imm, 8};
            return std::nullopt;
        case MOpcode::LdpRegFpImm:
        case MOpcode::StpRegFpImm:
        case MOpcode::LdpFprFpImm:
        case MOpcode::StpFprFpImm:
            if (mi.ops.size() >= 3 && mi.ops[2].kind == MOperand::Kind::Imm)
                return MemoryAccessClass{MemBaseKind::FramePointer, 0, mi.ops[2].imm, 16};
            return std::nullopt;
        case MOpcode::StrRegSpImm:
        case MOpcode::StrFprSpImm:
            if (mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Imm)
                return MemoryAccessClass{MemBaseKind::StackPointer, 0, mi.ops[1].imm, 8};
            return std::nullopt;
        case MOpcode::LdrRegBaseImm:
        case MOpcode::StrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::StrFprBaseImm:
            if (mi.ops.size() >= 3 && mi.ops[1].kind == MOperand::Kind::Reg &&
                mi.ops[1].reg.isPhys && mi.ops[2].kind == MOperand::Kind::Imm) {
                if (const auto tracked =
                        getTrackedAddressValue(trackedAddrs, mi.ops[1].reg.idOrPhys)) {
                    return makeResolvedAccessClass(*tracked, mi.ops[2].imm, 8);
                }
                // Unknown base-register accesses are heap/object accesses for
                // aliasing purposes. Physical register identity is not a
                // provenance guarantee: two registers may hold the same object.
                return std::nullopt;
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

static bool mayAliasMemory(const std::optional<MemoryAccessClass> &lhs,
                           const std::optional<MemoryAccessClass> &rhs) noexcept {
    if (!lhs || !rhs)
        return true;
    if (lhs->baseKind != rhs->baseKind)
        return false;
    if (lhs->baseKind == MemBaseKind::BaseRegister)
        return true;
    const long long lhsEnd = lhs->offset + static_cast<long long>(lhs->size);
    const long long rhsEnd = rhs->offset + static_cast<long long>(rhs->size);
    return !(lhsEnd <= rhs->offset || rhsEnd <= lhs->offset);
}

static std::optional<AddressValue>
deriveTrackedAddressValue(const MInstr &mi,
                          const std::array<std::optional<AddressValue>, kNumPhysRegs> &trackedAddrs)
    noexcept {
    auto regTracked = [&](std::size_t opIdx) -> std::optional<AddressValue> {
        if (opIdx >= mi.ops.size() || mi.ops[opIdx].kind != MOperand::Kind::Reg ||
            !mi.ops[opIdx].reg.isPhys) {
            return std::nullopt;
        }
        return getTrackedAddressValue(trackedAddrs, mi.ops[opIdx].reg.idOrPhys);
    };

    switch (mi.opc) {
        case MOpcode::MovRR: {
            return regTracked(1);
        }
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::AddsRI:
        case MOpcode::SubsRI: {
            if (mi.ops.size() < 3 || mi.ops[2].kind != MOperand::Kind::Imm)
                return std::nullopt;
            auto base = regTracked(1);
            if (!base)
                return std::nullopt;
            const long long delta = (mi.opc == MOpcode::SubRI || mi.opc == MOpcode::SubsRI)
                                        ? -mi.ops[2].imm
                                        : mi.ops[2].imm;
            base->offset += delta;
            return base;
        }
        case MOpcode::AddFpImm:
            if (mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Imm)
                return AddressValue{MemBaseKind::FramePointer, 0, mi.ops[1].imm};
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

static bool isTerminator(MOpcode opc) noexcept {
    switch (opc) {
        case MOpcode::Ret:
        case MOpcode::Br:
        case MOpcode::BCond:
        case MOpcode::Cbz:
        case MOpcode::Cbnz:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Flag (NZCV) classification helpers
// ---------------------------------------------------------------------------

/// @brief Returns true if the opcode sets the NZCV condition flags.
static bool setsFlags(MOpcode opc) noexcept {
    switch (opc) {
        case MOpcode::AddsRRR:
        case MOpcode::SubsRRR:
        case MOpcode::AddsRI:
        case MOpcode::SubsRI:
        case MOpcode::CmpRR:
        case MOpcode::CmpRI:
        case MOpcode::TstRR:
        case MOpcode::FCmpRR:
            return true;
        default:
            return false;
    }
}

/// @brief Returns true if the opcode reads the NZCV condition flags.
static bool usesFlags(MOpcode opc) noexcept {
    switch (opc) {
        case MOpcode::BCond:
        case MOpcode::Cset:
        case MOpcode::Csel:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Stack pointer helpers
// ---------------------------------------------------------------------------

/// @brief Returns true if the opcode implicitly modifies the stack pointer.
static bool modifiesSP(MOpcode opc) noexcept {
    return opc == MOpcode::SubSpImm || opc == MOpcode::AddSpImm;
}

/// @brief Returns true if the opcode implicitly reads the stack pointer
///        (SP-relative loads/stores for outgoing arguments).
static bool usesSP(MOpcode opc) noexcept {
    switch (opc) {
        case MOpcode::StrRegSpImm:
        case MOpcode::StrFprSpImm:
        case MOpcode::SubSpImm:
        case MOpcode::AddSpImm:
            return true;
        default:
            return false;
    }
}

/// @brief Returns true if the opcode is a call (Bl or Blr).
static bool isCall(MOpcode opc) noexcept {
    return opc == MOpcode::Bl || opc == MOpcode::Blr;
}

// ---------------------------------------------------------------------------
// Dependency node
// ---------------------------------------------------------------------------

struct DepNode {
    std::size_t instrIdx;           ///< Index into the body instruction array.
    std::vector<std::size_t> preds; ///< Predecessor indices (must complete first).
    std::vector<unsigned> predLat;  ///< Latency for each predecessor edge.
    unsigned critPath;              ///< Critical-path length from this node to end.
    unsigned predsDone;             ///< Count of predecessors already scheduled.
};

// ---------------------------------------------------------------------------
// Block scheduler
// ---------------------------------------------------------------------------

static std::vector<MInstr> scheduleBlock(std::vector<MInstr> body, const TargetInfo &target) {
    const std::size_t N = body.size();
    if (N <= 1)
        return body;

    // -----------------------------------------------------------------------
    // Build dependency graph.
    // -----------------------------------------------------------------------

    std::vector<DepNode> nodes(N);
    for (std::size_t i = 0; i < N; ++i)
        nodes[i].instrIdx = i;

    // Helper: add a dependency edge from instruction i to predecessor p.
    auto addDep = [&](std::size_t i, std::size_t p, unsigned lat) {
        if (p != i) {
            nodes[i].preds.push_back(p);
            nodes[i].predLat.push_back(lat);
        }
    };

    // last_def[regSlot] = index of the last instruction that defined the register.
    // usesSinceDef[regSlot] = indices of ALL instructions that read the register
    //   since its last definition.  When a new def occurs we add WAR deps to every
    //   entry, ensuring the def cannot be reordered before ANY earlier reader.
    // Flat arrays indexed by regIdx() for O(1) cache-friendly access.
    constexpr auto kNone = static_cast<std::size_t>(~0ULL);
    std::array<std::size_t, kNumTracked> lastDef;
    lastDef.fill(kNone);
    std::array<std::vector<std::size_t>, kNumTracked> usesSinceDef;
    std::array<std::optional<AddressValue>, kNumPhysRegs> trackedAddrs;
    trackedAddrs.fill(std::nullopt);

    std::vector<MemoryHistoryEntry> memoryHistory;
    memoryHistory.reserve(N);

    // Caller-saved registers that Bl/Blr implicitly clobber.  Pull these from
    // the selected target so scheduling follows Darwin/Linux/Windows ABI data.
    std::vector<uint32_t> callerSaved;
    callerSaved.reserve(target.callerSavedGPR.size() + target.callerSavedFPR.size());
    for (const PhysReg reg : target.callerSavedGPR)
        callerSaved.push_back(static_cast<uint32_t>(reg));
    for (const PhysReg reg : target.callerSavedFPR)
        callerSaved.push_back(static_cast<uint32_t>(reg));

    for (std::size_t i = 0; i < N; ++i) {
        const MInstr &mi = body[i];

        // --- Register USE dependencies (RAW) ---
        // Register allocator operand roles are the single source of truth for
        // explicit register uses and defs.
        for (std::size_t opIdx = 0; opIdx < mi.ops.size(); ++opIdx) {
            const auto roles = ra::operandRoles(mi, opIdx);
            if (!roles.first)
                continue;
            const auto &op = mi.ops[opIdx];
            if (op.kind != MOperand::Kind::Reg || !op.reg.isPhys)
                continue;
            const std::size_t ri = regIdx(op.reg.idOrPhys);
            if (lastDef[ri] != kNone)
                addDep(i, lastDef[ri], instrLatency(body[lastDef[ri]].opc));
            usesSinceDef[ri].push_back(i);
        }

        // --- NZCV flag dependencies (RAW on flags) ---
        // If this instruction uses flags, depend on the last flag-setter.
        if (usesFlags(mi.opc)) {
            if (lastDef[kIdxNZCV] != kNone)
                addDep(i, lastDef[kIdxNZCV], 1);
            usesSinceDef[kIdxNZCV].push_back(i);
        }

        // --- Stack pointer dependencies ---
        // SP-relative ops (StrRegSpImm, SubSpImm, AddSpImm) implicitly read SP.
        if (usesSP(mi.opc)) {
            if (lastDef[kIdxSP] != kNone)
                addDep(i, lastDef[kIdxSP], 1);
            usesSinceDef[kIdxSP].push_back(i);
        }
        // SubSpImm/AddSpImm implicitly define SP (WAW + WAR).
        if (modifiesSP(mi.opc)) {
            if (lastDef[kIdxSP] != kNone)
                addDep(i, lastDef[kIdxSP], 1);
            for (auto u : usesSinceDef[kIdxSP])
                addDep(i, u, 1);
            usesSinceDef[kIdxSP].clear();
            lastDef[kIdxSP] = i;
        }

        // --- Memory dependencies ---
        const bool memLoad = isLoad(mi.opc);
        const bool memStore = isStore(mi.opc);
        const auto memClass =
            (memLoad || memStore) ? classifyMemoryAccess(mi, trackedAddrs) : std::nullopt;
        if (memLoad || memStore) {
            for (const auto &prev : memoryHistory) {
                if (prev.isBarrier) {
                    addDep(i, prev.instrIdx, 1);
                    continue;
                }
                if (!mayAliasMemory(memClass, prev.accessClass))
                    continue;
                if (memLoad && prev.isStore)
                    addDep(i, prev.instrIdx, 1);
                if (memStore && (prev.isLoad || prev.isStore))
                    addDep(i, prev.instrIdx, 1);
            }
            memoryHistory.push_back(MemoryHistoryEntry{i, memLoad, memStore, false, memClass});
        }

        // --- Calls act as full memory barriers ---
        if (isCall(mi.opc)) {
            for (const auto &prev : memoryHistory)
                addDep(i, prev.instrIdx, 1);
            // Call also reads any live registers (conservatively: depend on all
            // recent defs of caller-saved regs so they aren't reordered past call).
            for (uint32_t r : callerSaved) {
                const std::size_t ri = regIdx(r);
                if (lastDef[ri] != kNone)
                    addDep(i, lastDef[ri], 1);
            }
            if (lastDef[kIdxNZCV] != kNone)
                addDep(i, lastDef[kIdxNZCV], 1);
            memoryHistory.push_back(MemoryHistoryEntry{i, false, false, true, std::nullopt});
        }

        // --- WAW + WAR dependencies, then update DEF map ---
        // WAW: if this instruction defines a register that was previously defined,
        //      it must come after the old definition.
        // WAR: if this instruction defines a register that was previously read,
        //      it must come after that reader (so the reader sees the old value).
        for (std::size_t opIdx = 0; opIdx < mi.ops.size(); ++opIdx) {
            const auto [isUse, isDef] = ra::operandRoles(mi, opIdx);
            if (!isDef)
                continue;
            if (mi.ops[opIdx].kind == MOperand::Kind::Reg && mi.ops[opIdx].reg.isPhys) {
                const std::size_t ri = regIdx(mi.ops[opIdx].reg.idOrPhys);
                if (lastDef[ri] != kNone)
                    addDep(i, lastDef[ri], 1); // WAW dependency
                for (auto u : usesSinceDef[ri])
                    addDep(i, u, 1); // WAR dependency
                usesSinceDef[ri].clear();
                lastDef[ri] = i;
                if (ri < kNumPhysRegs && mi.ops[opIdx].reg.cls == RegClass::GPR)
                    trackedAddrs[ri] = std::nullopt;
            }
        }

        if (const auto tracked = deriveTrackedAddressValue(mi, trackedAddrs);
            tracked && !mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Reg &&
            mi.ops[0].reg.isPhys && mi.ops[0].reg.cls == RegClass::GPR) {
            const std::size_t ri = regIdx(mi.ops[0].reg.idOrPhys);
            if (ri < kNumPhysRegs)
                trackedAddrs[ri] = tracked;
        }

        // Flag definitions — WAW + WAR on flags too.
        if (setsFlags(mi.opc)) {
            if (lastDef[kIdxNZCV] != kNone)
                addDep(i, lastDef[kIdxNZCV], 1);
            for (auto u : usesSinceDef[kIdxNZCV])
                addDep(i, u, 1);
            usesSinceDef[kIdxNZCV].clear();
            lastDef[kIdxNZCV] = i;
        }

        // Call clobbers: Bl/Blr implicitly define all caller-saved registers
        // and flags (the callee may clobber them).  Add WAW + WAR deps.
        if (isCall(mi.opc)) {
            for (uint32_t r : callerSaved) {
                const std::size_t ri = regIdx(r);
                if (lastDef[ri] != kNone)
                    addDep(i, lastDef[ri], 1); // WAW
                for (auto u : usesSinceDef[ri])
                    addDep(i, u, 1); // WAR
                usesSinceDef[ri].clear();
                lastDef[ri] = i;
                if (ri < kNumPhysRegs)
                    trackedAddrs[ri] = std::nullopt;
            }
            if (lastDef[kIdxNZCV] != kNone)
                addDep(i, lastDef[kIdxNZCV], 1);
            for (auto u : usesSinceDef[kIdxNZCV])
                addDep(i, u, 1);
            usesSinceDef[kIdxNZCV].clear();
            lastDef[kIdxNZCV] = i;
        }
    }

    // Deduplicate predecessor lists (a node may have been added multiple times).
    for (auto &n : nodes) {
        auto &p = n.preds;
        auto &l = n.predLat;
        // Sort by pred index, then remove duplicates keeping max latency.
        std::vector<std::pair<std::size_t, unsigned>> pl;
        pl.reserve(p.size());
        for (std::size_t k = 0; k < p.size(); ++k)
            pl.emplace_back(p[k], l[k]);
        std::sort(pl.begin(), pl.end());
        pl.erase(std::unique(pl.begin(),
                             pl.end(),
                             [](const auto &a, const auto &b) { return a.first == b.first; }),
                 pl.end());
        p.clear();
        l.clear();
        for (auto &[pidx, lat] : pl) {
            p.push_back(pidx);
            l.push_back(lat);
        }
        n.predsDone = 0;
    }

    // -----------------------------------------------------------------------
    // Compute successors (reverse of preds) for predsDone tracking.
    // -----------------------------------------------------------------------
    std::vector<std::vector<std::size_t>> succs(N);
    for (std::size_t i = 0; i < N; ++i)
        for (auto p : nodes[i].preds)
            succs[p].push_back(i);

    // -----------------------------------------------------------------------
    // Compute critical-path lengths (backward pass).
    // -----------------------------------------------------------------------
    // Process in reverse topological order (reverse program order is
    // approximately topological for sequential code).
    for (std::size_t i = N; i-- > 0;) {
        // critPath(i) = instrLatency(i) + max over successors s of critPath(s).
        // This gives the total latency from scheduling node i to the end of
        // the DAG, making loads (4 cycles) and multiplies (3 cycles) higher
        // priority than single-cycle ALU ops.
        unsigned maxSuccCrit = 0;
        for (auto s : succs[i]) {
            if (nodes[s].critPath > maxSuccCrit)
                maxSuccCrit = nodes[s].critPath;
        }
        nodes[i].critPath = instrLatency(body[i].opc) + maxSuccCrit;
    }

    // -----------------------------------------------------------------------
    // List scheduling (greedy, critical-path priority).
    // -----------------------------------------------------------------------

    // Count predecessors for each node to initialise the ready list.
    for (std::size_t i = 0; i < N; ++i)
        nodes[i].predsDone = 0;

    // Build initial ready list: nodes with no predecessors.
    // Priority: (critPath DESC, instrIdx ASC) — higher critPath first.
    using PriNode = std::pair<int, std::size_t>; // (-critPath, instrIdx)
    std::priority_queue<PriNode, std::vector<PriNode>, std::greater<PriNode>> ready;

    std::vector<std::size_t> predCount(N, 0);
    for (std::size_t i = 0; i < N; ++i)
        predCount[i] = nodes[i].preds.size();

    for (std::size_t i = 0; i < N; ++i)
        if (predCount[i] == 0)
            ready.emplace(-static_cast<int>(nodes[i].critPath), i);

    std::vector<MInstr> scheduled;
    scheduled.reserve(N);
    std::vector<bool> done(N, false);

    while (!ready.empty()) {
        auto [neg_crit, idx] = ready.top();
        ready.pop();

        if (done[idx])
            continue;
        done[idx] = true;
        scheduled.push_back(body[idx]);

        // Decrement pred counts for successors; enqueue newly-ready ones.
        for (auto s : succs[idx]) {
            if (done[s])
                continue;
            ++nodes[s].predsDone;
            if (nodes[s].predsDone >= predCount[s])
                ready.emplace(-static_cast<int>(nodes[s].critPath), s);
        }
    }

    // If the graph was cyclic (shouldn't happen in SSA-like post-RA MIR),
    // fall back to original order by appending any unscheduled instructions.
    for (std::size_t i = 0; i < N; ++i)
        if (!done[i])
            scheduled.push_back(body[i]);

    return scheduled;
}

// ---------------------------------------------------------------------------
// Per-function entry point
// ---------------------------------------------------------------------------

static void scheduleFunction(MFunction &fn, const TargetInfo &target) {
    for (auto &bb : fn.blocks) {
        if (bb.instrs.size() <= 1)
            continue;

        // Split the instruction stream into segments separated by calls and
        // mid-block terminators (BCond, Cbz, Cbnz that appear before the final
        // terminator group).  Each segment is scheduled independently, then
        // reassembled with the separators in their original positions.  Calls
        // remain hard scheduling boundaries because the dependency model tracks
        // register and memory effects, but does not model all runtime side
        // effects or helper-call conventions precisely enough to safely move
        // unrelated arithmetic across them.
        //
        // Example block layout:
        //   adds x15, x12, #3      ← segment 0
        //   b.vs L.Ltrap_ovf       ← separator 0 (mid-block guard)
        //   mov x14, #20           ← segment 1
        //   cbz x14, L.Ltrap_div0  ← separator 1 (mid-block guard)
        //   sdiv x13, x15, x14     ← segment 2
        //   ...
        //   b.ne Lbc_oob0          ← final terminator
        //   b Lbc_ok0              ← final terminator

        // First, find where the final terminator group starts (consecutive
        // terminators at the end of the block).
        std::size_t termStart = bb.instrs.size();
        while (termStart > 0 && isTerminator(bb.instrs[termStart - 1].opc))
            --termStart;

        // Now collect segments and separators from the body (indices 0..termStart-1).
        std::vector<MInstr> result;
        result.reserve(bb.instrs.size());
        std::vector<MInstr> segment;

        for (std::size_t i = 0; i < termStart; ++i) {
            auto &mi = bb.instrs[i];
            if (isTerminator(mi.opc) || isCall(mi.opc)) {
                // Mid-block terminator or call — schedule the preceding segment,
                // then append the boundary instruction in place.
                if (segment.size() > 1)
                    segment = scheduleBlock(std::move(segment), target);
                result.insert(result.end(), segment.begin(), segment.end());
                segment.clear();
                result.push_back(mi);
            } else {
                segment.push_back(mi);
            }
        }

        // Schedule the final body segment (between the last mid-block branch
        // and the final terminator group).
        if (segment.size() > 1)
            segment = scheduleBlock(std::move(segment), target);
        result.insert(result.end(), segment.begin(), segment.end());

        // Append final terminators in original order.
        for (std::size_t i = termStart; i < bb.instrs.size(); ++i)
            result.push_back(bb.instrs[i]);

        bb.instrs = std::move(result);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Pass implementation
// ---------------------------------------------------------------------------

bool SchedulerPass::run(AArch64Module &module, Diagnostics &diags) {
    if (module.ti == nullptr) {
        diags.error("aarch64 scheduler: target info is required");
        return false;
    }

    for (auto &fn : module.mir)
        scheduleFunction(fn, *module.ti);
    return true;
}

} // namespace viper::codegen::aarch64::passes
