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
//   1. Partition instructions into non-terminator body + terminators.
//   2. Build a data-dependency DAG from physical-register def/use chains.
//      Memory dependencies are handled conservatively: every store depends on
//      all prior loads/stores; every load depends on all prior stores.
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

#include <algorithm>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

namespace viper::codegen::aarch64::passes
{

namespace
{

// ---------------------------------------------------------------------------
// Latency model
// ---------------------------------------------------------------------------

/// @brief Return the output latency (cycles from write to use) for an opcode.
static unsigned instrLatency(MOpcode opc) noexcept
{
    switch (opc)
    {
        // Loads: L1 hit ~4 cycles on Apple M-series.
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::LdpRegFpImm:
        case MOpcode::LdpFprFpImm:
            return 4;

        // Integer multiply: 3 cycles.
        case MOpcode::MulRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::MSubRRRR:
        case MOpcode::MAddRRRR:
            return 3;

        // FP arithmetic: 3 cycles.
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
            return 3;

        // Conversions: 3 cycles.
        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
            return 3;

        // All other instructions: 1 cycle.
        default:
            return 1;
    }
}

// ---------------------------------------------------------------------------
// Memory classification helpers
// ---------------------------------------------------------------------------

static bool isLoad(MOpcode opc) noexcept
{
    switch (opc)
    {
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

static bool isStore(MOpcode opc) noexcept
{
    switch (opc)
    {
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

static bool isTerminator(MOpcode opc) noexcept
{
    switch (opc)
    {
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
// Dependency node
// ---------------------------------------------------------------------------

struct DepNode
{
    std::size_t          instrIdx;  ///< Index into the body instruction array.
    std::vector<std::size_t> preds; ///< Predecessor indices (must complete first).
    std::vector<unsigned>    predLat; ///< Latency for each predecessor edge.
    unsigned             critPath;  ///< Critical-path length from this node to end.
    unsigned             predsDone; ///< Count of predecessors already scheduled.
};

// ---------------------------------------------------------------------------
// Block scheduler
// ---------------------------------------------------------------------------

static std::vector<MInstr> scheduleBlock(std::vector<MInstr> body)
{
    const std::size_t N = body.size();
    if (N <= 1)
        return body;

    // -----------------------------------------------------------------------
    // Build dependency graph.
    // -----------------------------------------------------------------------

    std::vector<DepNode> nodes(N);
    for (std::size_t i = 0; i < N; ++i)
        nodes[i].instrIdx = i;

    // last_def[physReg] = index of the last instruction that defined physReg.
    // We use uint32_t to keep it small; UINT32_MAX = "none".
    constexpr auto kNone = static_cast<std::size_t>(~0ULL);
    std::unordered_map<uint32_t, std::size_t> lastDef;

    // Last instruction indices with memory side-effects.
    std::size_t lastStore = kNone;
    std::size_t lastLoad  = kNone;

    for (std::size_t i = 0; i < N; ++i)
    {
        const MInstr &mi = body[i];

        // Register data dependencies: scan all USE operands.
        for (const auto &op : mi.ops)
        {
            if (op.kind != MOperand::Kind::Reg || !op.reg.isPhys)
                continue;
            const uint32_t reg = op.reg.idOrPhys;
            auto it = lastDef.find(reg);
            if (it != lastDef.end() && it->second != i)
            {
                const std::size_t def = it->second;
                // Add i → def backward edge (i depends on def).
                nodes[i].preds.push_back(def);
                nodes[i].predLat.push_back(instrLatency(body[def].opc));
            }
        }

        // Conservative memory dependencies.
        if (isLoad(mi.opc))
        {
            // Load depends on last store (RAW through memory).
            if (lastStore != kNone && lastStore != i)
            {
                nodes[i].preds.push_back(lastStore);
                nodes[i].predLat.push_back(1);
            }
            lastLoad = i;
        }
        else if (isStore(mi.opc))
        {
            // Store depends on last store (WAW) and last load (WAR).
            if (lastStore != kNone && lastStore != i)
            {
                nodes[i].preds.push_back(lastStore);
                nodes[i].predLat.push_back(1);
            }
            if (lastLoad != kNone && lastLoad != i)
            {
                nodes[i].preds.push_back(lastLoad);
                nodes[i].predLat.push_back(1);
            }
            lastStore = i;
        }

        // Update def map for every DEF operand.
        // (Conservative: assume first operand is def for most opcodes.)
        if (!mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Reg && mi.ops[0].reg.isPhys)
            lastDef[mi.ops[0].reg.idOrPhys] = i;
    }

    // Deduplicate predecessor lists (a node may have been added multiple times).
    for (auto &n : nodes)
    {
        auto &p = n.preds;
        auto &l = n.predLat;
        // Sort by pred index, then remove duplicates keeping max latency.
        std::vector<std::pair<std::size_t, unsigned>> pl;
        pl.reserve(p.size());
        for (std::size_t k = 0; k < p.size(); ++k)
            pl.emplace_back(p[k], l[k]);
        std::sort(pl.begin(), pl.end());
        pl.erase(std::unique(pl.begin(), pl.end(),
                             [](const auto &a, const auto &b)
                             { return a.first == b.first; }),
                 pl.end());
        p.clear(); l.clear();
        for (auto &[pidx, lat] : pl) { p.push_back(pidx); l.push_back(lat); }
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
    for (std::size_t i = N; i-- > 0;)
    {
        unsigned maxSuccCrit = 0;
        for (auto s : succs[i])
        {
            const unsigned edge = instrLatency(body[i].opc);
            const unsigned c    = edge + nodes[s].critPath;
            if (c > maxSuccCrit)
                maxSuccCrit = c;
        }
        nodes[i].critPath = maxSuccCrit + 1;
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

    while (!ready.empty())
    {
        auto [neg_crit, idx] = ready.top();
        ready.pop();

        if (done[idx])
            continue;
        done[idx] = true;
        scheduled.push_back(body[idx]);

        // Decrement pred counts for successors; enqueue newly-ready ones.
        for (auto s : succs[idx])
        {
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

static void scheduleFunction(MFunction &fn)
{
    for (auto &bb : fn.blocks)
    {
        if (bb.instrs.size() <= 1)
            continue;

        // Partition into body and terminators.
        std::vector<MInstr> body, terms;
        for (auto &mi : bb.instrs)
        {
            if (isTerminator(mi.opc))
                terms.push_back(mi);
            else
                body.push_back(mi);
        }

        if (body.size() <= 1)
            continue; // Nothing to reorder.

        body = scheduleBlock(std::move(body));

        // Reassemble: scheduled body + terminators in original order.
        bb.instrs.clear();
        bb.instrs.reserve(body.size() + terms.size());
        bb.instrs.insert(bb.instrs.end(), body.begin(), body.end());
        bb.instrs.insert(bb.instrs.end(), terms.begin(), terms.end());
    }
}

} // namespace (anonymous)

// ---------------------------------------------------------------------------
// Pass implementation
// ---------------------------------------------------------------------------

bool SchedulerPass::run(AArch64Module &module, Diagnostics & /*diags*/)
{
    for (auto &fn : module.mir)
        scheduleFunction(fn);
    return true;
}

} // namespace viper::codegen::aarch64::passes
