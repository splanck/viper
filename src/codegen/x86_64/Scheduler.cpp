//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/Scheduler.cpp
// Purpose: Conservative post-RA list scheduler for x86-64 Machine IR.
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/Scheduler.hpp"

#include "codegen/x86_64/OperandRoles.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
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

void addMemRegs(const OpMem &mem, InstrDeps &deps) {
    if (mem.base.isPhys)
        deps.uses.insert(mem.base.idOrPhys);
    if (mem.hasIndex && mem.index.isPhys)
        deps.uses.insert(mem.index.idOrPhys);
}

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

    return std::any_of(instr.operands.begin(), instr.operands.end(), isVirtualOperand);
}

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

[[nodiscard]] bool intersects(const std::unordered_set<uint16_t> &a,
                              const std::unordered_set<uint16_t> &b) {
    if (a.size() > b.size())
        return intersects(b, a);
    return std::any_of(a.begin(), a.end(), [&](uint16_t reg) { return b.count(reg) != 0; });
}

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

[[nodiscard]] std::vector<unsigned> criticalHeights(const std::vector<InstrDeps> &deps,
                                                    const std::vector<std::vector<std::size_t>> &succs) {
    std::vector<unsigned> heights(deps.size(), 0);
    for (std::size_t i = deps.size(); i-- > 0;) {
        unsigned succHeight = 0;
        for (std::size_t succ : succs[i])
            succHeight = std::max(succHeight, heights[succ]);
        heights[i] = deps[i].latency + succHeight;
    }
    return heights;
}

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
        auto best = std::max_element(
            ready.begin(),
            ready.end(),
            [&](std::size_t lhs, std::size_t rhs) {
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
        return segment;

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

void flushSegment(std::vector<MInstr> &out, std::vector<MInstr> &segment, std::size_t &changedSegments) {
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

std::size_t scheduleModule(std::vector<MFunction> &mir) {
    std::size_t changed = 0;
    for (auto &fn : mir)
        changed += scheduleFunction(fn);
    return changed;
}

} // namespace viper::codegen::x64
