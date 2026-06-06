//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Scheduler.cpp
// Purpose: Conservative post-RA list scheduler for x86-64 Machine IR.
// Key invariants:
//   - Data dependences are computed per basic block before reordering.
//   - Block boundaries and control flow instructions are never reordered.
// Ownership/Lifetime:
//   - Stateless; mutates caller-owned MFunction in place.
// Links: codegen/x86_64/Scheduler.hpp,
//        codegen/x86_64/OperandRoles.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Scheduler.hpp"

#include "codegen/x86_64/OperandRoles.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

namespace viper::codegen::x64 {
namespace {

struct InstrDeps {
    std::unordered_set<uint16_t> uses{};
    std::unordered_set<uint16_t> defs{};
    bool usesFlags{false};
    bool defsFlags{false};
    bool memory{false};
    unsigned latency{1};
};

/// @brief Predicate: does @p op contain any virtual register reference?
/// @details Post-register-allocation scheduling refuses to operate on blocks
///          that still contain virtual operands because their interference
///          isn't yet resolved. Used as a guard by @ref isSchedulingBoundary.
[[nodiscard]] bool isVirtualOperand(const Operand &op) noexcept {
    if (const auto *reg = std::get_if<OpReg>(&op))
        return !reg->isPhys;
    if (const auto *mem = std::get_if<OpMem>(&op)) {
        if (!mem->base.isPhys)
            return true;
        return mem->hasIndex && !mem->index.isPhys;
    }
    return false;
}

/// @brief Add the physical registers used by @p mem to @p deps.uses.
/// @details Memory operands implicitly read their base and index registers.
///          The scheduler records those reads so dependency edges include
///          address computation.
void addMemRegs(const OpMem &mem, InstrDeps &deps) {
    if (mem.base.isPhys)
        deps.uses.insert(mem.base.idOrPhys);
    if (mem.hasIndex && mem.index.isPhys)
        deps.uses.insert(mem.index.idOrPhys);
}

/// @brief Approximate instruction latency in cycles.
/// @details Hand-tuned numbers loosely based on modern x86 microarchitectures.
///          Memory loads (4 cycles), IMUL (3), FDIV (10), and FP arithmetic
///          (3) form the bulk of the table; everything else defaults to 1.
///          The scheduler uses this to prefer dispatching long-latency
///          instructions earlier in the segment.
[[nodiscard]] unsigned opcodeLatency(MOpcode opcode) noexcept {
    switch (opcode) {
        case MOpcode::MOVmr:
        case MOpcode::MOVSDmr:
        case MOpcode::MOVUPSmr:
            return 4;
        case MOpcode::IMULrr:
            return 3;
        case MOpcode::FDIV:
            return 10;
        case MOpcode::CVTSI2SD:
        case MOpcode::CVTTSD2SI:
            return 4;
        case MOpcode::FADD:
        case MOpcode::FSUB:
        case MOpcode::FMUL:
            return 3;
        default:
            return 1;
    }
}

/// @brief Predicate: does @p instr terminate a schedulable segment?
/// @details Control transfers, calls, parallel-copy pseudos, and
///          implicit-register opcodes (CQO, IDIV) cannot be reordered around.
///          Instructions that define RSP or RBP, or that still reference
///          virtual operands, also serve as boundaries.
[[nodiscard]] bool isSchedulingBoundary(const MInstr &instr) noexcept {
    switch (instr.opcode) {
        case MOpcode::LABEL:
        case MOpcode::CALL:
        case MOpcode::RET:
        case MOpcode::JMP:
        case MOpcode::JCC:
        case MOpcode::UD2:
        case MOpcode::PUSH:
        case MOpcode::POP:
        case MOpcode::CQO:
        case MOpcode::IDIVrm:
        case MOpcode::DIVrm:
        case MOpcode::PX_COPY:
        case MOpcode::SELECT_GPR:
        case MOpcode::SELECT_XMM:
        case MOpcode::ADDOvfrr:
        case MOpcode::SUBOvfrr:
        case MOpcode::IMULOvfrr:
            return true;
        default:
            break;
    }

    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        (void)isUse;
        if (!isDef)
            continue;
        const auto *reg = std::get_if<OpReg>(&instr.operands[idx]);
        if (!reg || !reg->isPhys)
            continue;
        const auto phys = static_cast<PhysReg>(reg->idOrPhys);
        if (phys == PhysReg::RSP || phys == PhysReg::RBP)
            return true;
    }

    return std::any_of(instr.operands.begin(), instr.operands.end(), isVirtualOperand);
}

/// @brief Build the dependency descriptor for @p instr.
/// @details Walks each operand and records physical register uses/defs,
///          whether the instruction reads/writes EFLAGS, whether it
///          accesses memory, and the assumed latency. The result feeds
///          the dependency graph constructed by @ref scheduleSegment.
[[nodiscard]] InstrDeps analyseInstr(const MInstr &instr) {
    InstrDeps deps{};
    deps.usesFlags = usesEFlags(instr.opcode);
    deps.defsFlags = definesEFlags(instr.opcode);
    deps.latency = opcodeLatency(instr.opcode);

    for (std::size_t idx = 0; idx < instr.operands.size(); ++idx) {
        const auto [isUse, isDef] = operandRoles(instr, idx);
        const Operand &op = instr.operands[idx];

        if (const auto *reg = std::get_if<OpReg>(&op)) {
            if (reg->isPhys) {
                if (isUse)
                    deps.uses.insert(reg->idOrPhys);
                if (isDef)
                    deps.defs.insert(reg->idOrPhys);
            }
            continue;
        }

        if (const auto *mem = std::get_if<OpMem>(&op)) {
            deps.memory = true;
            addMemRegs(*mem, deps);
            continue;
        }

        if (std::holds_alternative<OpRipLabel>(op))
            deps.memory = true;
    }

    return deps;
}

/// @brief Does @p a share any element with @p b?
/// @details Recursion onto the smaller set keeps the hash-set lookup count
///          minimal. Used by @ref dependsOn to detect def-use, use-def, and
///          def-def conflicts.
[[nodiscard]] bool intersects(const std::unordered_set<uint16_t> &a,
                              const std::unordered_set<uint16_t> &b) {
    if (a.size() > b.size())
        return intersects(b, a);
    return std::any_of(a.begin(), a.end(), [&](uint16_t reg) { return b.count(reg) != 0; });
}

/// @brief Predicate: must @p second execute after @p first?
/// @details A dependency exists when:
///          - one instruction's def aliases another's use (data dep),
///          - they both define the same register (output dep),
///          - one defines EFLAGS while the other reads or redefines it,
///          - or both perform memory accesses (conservative aliasing).
[[nodiscard]] bool dependsOn(const InstrDeps &first, const InstrDeps &second) {
    if (intersects(first.defs, second.uses))
        return true;
    if (intersects(first.uses, second.defs))
        return true;
    if (intersects(first.defs, second.defs))
        return true;

    if (first.defsFlags && (second.usesFlags || second.defsFlags))
        return true;
    if (first.usesFlags && second.defsFlags)
        return true;

    return first.memory && second.memory;
}

/// @brief Compute the critical-path height of each instruction.
/// @details Height = instruction's own latency plus the maximum height of
///          its successors. A higher height means more downstream work, so
///          the list scheduler prioritises dispatching that instruction
///          earlier to expose parallelism.
/// @param deps Per-instruction dependency descriptors.
/// @param succs Dependency graph successor lists.
/// @return Per-instruction critical-path height in cycles.
[[nodiscard]] std::vector<unsigned> criticalHeights(
    const std::vector<InstrDeps> &deps, const std::vector<std::vector<std::size_t>> &succs) {
    std::vector<unsigned> heights(deps.size(), 0);
    for (std::size_t i = deps.size(); i-- > 0;) {
        unsigned succHeight = 0;
        for (std::size_t succ : succs[i])
            succHeight = std::max(succHeight, heights[succ]);
        heights[i] = deps[i].latency + succHeight;
    }
    return heights;
}

/// @brief Reorder a straight-line instruction segment using list scheduling.
/// @details Builds an upper-triangular dependency graph (for each i < j,
///          adds i→j when they conflict), tracks ready instructions
///          (predecessor count zero), and at each step picks the ready
///          instruction with the highest critical-path height. Ties broken
///          by latency then by program-order index (preferring the later
///          instruction to delay it less).
/// @param segment Instructions to reorder (consumed by move).
/// @param changed Output: set to true if the order actually changed.
/// @return Reordered instruction stream.
[[nodiscard]] std::vector<MInstr> scheduleSegment(std::vector<MInstr> segment, bool &changed) {
    changed = false;
    const std::size_t n = segment.size();
    if (n < 2)
        return segment;

    std::vector<InstrDeps> deps;
    deps.reserve(n);
    for (const auto &instr : segment)
        deps.push_back(analyseInstr(instr));

    std::vector<std::vector<std::size_t>> succs(n);
    std::vector<unsigned> predCount(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!dependsOn(deps[i], deps[j]))
                continue;
            succs[i].push_back(j);
            ++predCount[j];
        }
    }

    const std::vector<unsigned> heights = criticalHeights(deps, succs);

    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < n; ++i) {
        if (predCount[i] == 0)
            ready.push_back(i);
    }

    std::vector<bool> scheduled(n, false);
    std::vector<std::size_t> order;
    order.reserve(n);

    while (!ready.empty()) {
        auto best =
            std::max_element(ready.begin(), ready.end(), [&](std::size_t lhs, std::size_t rhs) {
                if (heights[lhs] != heights[rhs])
                    return heights[lhs] < heights[rhs];
                if (deps[lhs].latency != deps[rhs].latency)
                    return deps[lhs].latency < deps[rhs].latency;
                return lhs > rhs;
            });

        const std::size_t idx = *best;
        ready.erase(best);
        if (scheduled[idx])
            continue;

        scheduled[idx] = true;
        order.push_back(idx);

        for (std::size_t succ : succs[idx]) {
            if (predCount[succ] > 0)
                --predCount[succ];
            if (predCount[succ] == 0 && !scheduled[succ])
                ready.push_back(succ);
        }
    }

    if (order.size() != n)
        throw std::runtime_error("x86-64 scheduler: dependency graph did not schedule every node");

    for (std::size_t i = 0; i < n; ++i) {
        if (order[i] != i) {
            changed = true;
            break;
        }
    }
    if (!changed)
        return segment;

    std::vector<MInstr> scheduledSegment;
    scheduledSegment.reserve(n);
    for (std::size_t idx : order)
        scheduledSegment.push_back(std::move(segment[idx]));
    return scheduledSegment;
}

/// @brief Schedule @p segment and append its result to @p out.
/// @details Helper used by the block-walker to drain accumulated
///          schedulable instructions whenever a boundary is hit. The
///          @p changedSegments counter is bumped only when the
///          reordering actually mutated the stream.
void flushSegment(std::vector<MInstr> &out,
                  std::vector<MInstr> &segment,
                  std::size_t &changedSegments) {
    if (segment.empty())
        return;

    bool changed = false;
    std::vector<MInstr> scheduled = scheduleSegment(std::move(segment), changed);
    if (changed)
        ++changedSegments;
    out.insert(out.end(),
               std::make_move_iterator(scheduled.begin()),
               std::make_move_iterator(scheduled.end()));
    segment.clear();
}

} // namespace

/// @brief Schedule every block of @p fn, splitting at boundary instructions.
/// @details For each block, walks instructions linearly and accumulates a
///          schedulable segment until a boundary instruction is reached.
///          Each segment is reordered independently. Boundary instructions
///          themselves are appended verbatim.
/// @param fn Machine function rewritten in place.
/// @return Number of segments whose order changed (useful for stats).
std::size_t scheduleFunction(MFunction &fn) {
    std::size_t changedSegments = 0;

    for (auto &block : fn.blocks) {
        std::vector<MInstr> rewritten;
        rewritten.reserve(block.instructions.size());

        std::vector<MInstr> segment;
        for (auto &instr : block.instructions) {
            if (isSchedulingBoundary(instr)) {
                flushSegment(rewritten, segment, changedSegments);
                rewritten.push_back(std::move(instr));
                continue;
            }
            segment.push_back(std::move(instr));
        }
        flushSegment(rewritten, segment, changedSegments);
        block.instructions.swap(rewritten);
    }

    return changedSegments;
}

/// @brief Schedule every function in @p mir.
/// @details Wraps @ref scheduleFunction over the module's function list.
std::size_t scheduleModule(std::vector<MFunction> &mir) {
    std::size_t changed = 0;
    for (auto &fn : mir)
        changed += scheduleFunction(fn);
    return changed;
}

} // namespace viper::codegen::x64
