//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/peephole/BranchOpt.cpp
// Purpose: Branch optimization peephole sub-passes for the x86-64 backend.
//          Implements greedy trace block layout, cold block reordering,
//          branch chain elimination, conditional branch inversion, and
//          fallthrough jump removal.
//
// Key invariants:
//   - Entry block always stays first in the layout.
//   - Branch chain resolution limits hops to 8 to avoid cycles.
//   - All control-flow rewrites preserve semantic equivalence.
//
// Ownership/Lifetime:
//   - Stateless; all state is owned by the caller.
//
// Links: src/codegen/x86_64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "BranchOpt.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::x64::peephole {

void traceBlockLayout(MFunction &fn, PeepholeStats &stats) {
    if (fn.blocks.size() <= 2)
        return;

    const auto n = fn.blocks.size();

    // Build label -> index map.
    std::unordered_map<std::string, std::size_t> nameToIdx;
    nameToIdx.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        nameToIdx[fn.blocks[i].label] = i;

    std::vector<bool> placed(n, false);
    std::vector<std::size_t> order;
    order.reserve(n);

    // Seed with entry block.
    std::size_t cur = 0;
    while (order.size() < n) {
        if (!placed[cur]) {
            placed[cur] = true;
            order.push_back(cur);

            // Follow unconditional JMP target to extend the trace.
            const auto &instrs = fn.blocks[cur].instructions;
            if (!instrs.empty() && instrs.back().opcode == MOpcode::JMP) {
                const auto *lbl = std::get_if<OpLabel>(&instrs.back().operands[0]);
                if (lbl) {
                    auto it = nameToIdx.find(lbl->name);
                    if (it != nameToIdx.end() && !placed[it->second]) {
                        cur = it->second;
                        continue;
                    }
                }
            }
        }

        // Find next unplaced block.
        bool found = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (!placed[i]) {
                cur = i;
                found = true;
                break;
            }
        }
        if (!found)
            break;
    }

    // Check if reordering changed anything.
    bool changed = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (order[i] != i) {
            changed = true;
            break;
        }
    }

    if (changed) {
        std::vector<MBasicBlock> reordered;
        reordered.reserve(n);
        for (std::size_t idx : order)
            reordered.push_back(std::move(fn.blocks[idx]));
        fn.blocks = std::move(reordered);
        stats.blocksReordered += n;
    }
}

void moveColdBlocks(MFunction &fn, PeepholeStats &stats) {
    if (fn.blocks.size() <= 2)
        return;

    std::vector<std::size_t> coldIndices;
    std::vector<std::size_t> hotIndices;

    // First block (entry) is always hot and stays first
    hotIndices.push_back(0);

    // Classify remaining blocks
    for (std::size_t bi = 1; bi < fn.blocks.size(); ++bi) {
        const auto &block = fn.blocks[bi];
        bool isCold = false;

        // Check for trap/error indicators in label
        const auto &label = block.label;
        if (label.find("trap") != std::string::npos || label.find("error") != std::string::npos ||
            label.find("panic") != std::string::npos ||
            label.find("unreachable") != std::string::npos) {
            isCold = true;
        }

        // Check for UD2 instruction (trap)
        if (!isCold) {
            for (const auto &instr : block.instructions) {
                if (instr.opcode == MOpcode::UD2) {
                    isCold = true;
                    break;
                }
            }
        }

        if (isCold)
            coldIndices.push_back(bi);
        else
            hotIndices.push_back(bi);
    }

    // Only reorder if we found cold blocks
    if (!coldIndices.empty() && hotIndices.size() > 1) {
        std::vector<MBasicBlock> newBlocks;
        newBlocks.reserve(fn.blocks.size());

        // Add hot blocks first
        for (std::size_t idx : hotIndices)
            newBlocks.push_back(std::move(fn.blocks[idx]));

        // Add cold blocks at the end
        for (std::size_t idx : coldIndices) {
            newBlocks.push_back(std::move(fn.blocks[idx]));
            ++stats.coldBlocksMoved;
        }

        fn.blocks = std::move(newBlocks);
    }
}

void eliminateBranchChains(MFunction &fn, PeepholeStats &stats) {
    // Build forwarding map: label -> ultimate JMP target for single-JMP blocks.
    std::unordered_map<std::string, std::string> forwarding;
    for (const auto &block : fn.blocks) {
        if (block.instructions.size() == 1 && block.instructions[0].opcode == MOpcode::JMP) {
            const auto *lbl = std::get_if<OpLabel>(&block.instructions[0].operands[0]);
            if (lbl)
                forwarding[block.label] = lbl->name;
        }
    }

    // Resolve chains (limit hops to avoid cycles from self-loops).
    for (auto &[label, target] : forwarding) {
        for (int hops = 0; hops < 8; ++hops) {
            auto it = forwarding.find(target);
            if (it == forwarding.end() || it->second == target)
                break;
            target = it->second;
        }
    }

    // Retarget branches.
    if (!forwarding.empty()) {
        for (auto &block : fn.blocks) {
            if (block.instructions.empty())
                continue;
            auto &last = block.instructions.back();
            if (last.opcode == MOpcode::JMP) {
                auto *lbl = std::get_if<OpLabel>(&last.operands[0]);
                if (lbl) {
                    auto it = forwarding.find(lbl->name);
                    if (it != forwarding.end() && it->second != lbl->name) {
                        lbl->name = it->second;
                        ++stats.branchChainsEliminated;
                    }
                }
            } else if (last.opcode == MOpcode::JCC && last.operands.size() >= 2) {
                auto *lbl = std::get_if<OpLabel>(&last.operands[1]);
                if (lbl) {
                    auto it = forwarding.find(lbl->name);
                    if (it != forwarding.end() && it->second != lbl->name) {
                        lbl->name = it->second;
                        ++stats.branchChainsEliminated;
                    }
                }
            }
        }
    }
}

void invertConditionalBranches(MFunction &fn, PeepholeStats &stats) {
    // Pattern:  JCC(cc, label_skip) / JMP(label_exit) where label_skip is the next block
    // Rewrite:  JCC(invert(cc), label_exit) — saves 5 bytes (eliminates JMP)
    for (std::size_t bi = 0; bi + 1 < fn.blocks.size(); ++bi) {
        auto &block = fn.blocks[bi];
        const auto &nextBlock = fn.blocks[bi + 1];

        if (block.instructions.size() < 2)
            continue;

        auto &secondToLast = block.instructions[block.instructions.size() - 2];
        auto &last = block.instructions[block.instructions.size() - 1];

        // Check: second-to-last is JCC, last is JMP.
        if (secondToLast.opcode != MOpcode::JCC || last.opcode != MOpcode::JMP)
            continue;

        // JCC operands: {OpImm(cc), OpLabel(target)}
        if (secondToLast.operands.size() < 2 || last.operands.size() < 1)
            continue;

        // Check: JCC target is the next block's label (i.e., it skips over the JMP).
        const auto *jccLabel = std::get_if<OpLabel>(&secondToLast.operands[1]);
        if (!jccLabel || jccLabel->name != nextBlock.label)
            continue;

        // Get the JMP target — this becomes the inverted JCC target.
        const auto *jmpLabel = std::get_if<OpLabel>(&last.operands[0]);
        if (!jmpLabel)
            continue;

        // Invert the condition code.
        const auto *ccImm = std::get_if<OpImm>(&secondToLast.operands[0]);
        if (!ccImm)
            continue;

        // Viper CC inversion table:
        //  0 (eq) <-> 1 (ne), 2 (lt) <-> 5 (ge), 3 (le) <-> 4 (gt)
        //  6 (a)  <-> 9 (be), 7 (ae) <-> 8 (b),  10 (p) <-> 11 (np)
        //  12 (o) <-> 13 (no)
        constexpr int kInvertTable[] = {1, 0, 5, 4, 3, 2, 9, 8, 7, 6, 11, 10, 13, 12};
        int cc = static_cast<int>(ccImm->val);
        if (cc < 0 || cc > 13)
            continue;

        // Rewrite: JCC(invCC, jmpTarget)
        secondToLast.operands[0] = OpImm{kInvertTable[cc]};
        secondToLast.operands[1] = *jmpLabel;
        block.instructions.pop_back(); // Remove JMP.
        ++stats.branchesInverted;
    }
}

void removeFallthroughJumps(MFunction &fn, PeepholeStats &stats) {
    for (std::size_t bi = 0; bi + 1 < fn.blocks.size(); ++bi) {
        auto &block = fn.blocks[bi];
        const auto &nextBlock = fn.blocks[bi + 1];

        if (block.instructions.empty())
            continue;

        // Check if the last instruction is an unconditional jump to the next block
        auto &lastInstr = block.instructions.back();
        if (isJumpTo(lastInstr, nextBlock.label)) {
            block.instructions.pop_back();
            ++stats.branchesToNextRemoved;
        }
    }
}

} // namespace viper::codegen::x64::peephole
