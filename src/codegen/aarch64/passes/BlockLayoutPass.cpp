//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file codegen/aarch64/passes/BlockLayoutPass.cpp
/// @brief Greedy trace block layout pass for the AArch64 code-generation pipeline.
///
/// **Algorithm:**
///
/// For each MFunction, build a name→index map for all basic blocks, then
/// construct a placement order using a greedy trace:
///
/// 1. Seed the trace with the entry block (index 0) — it must stay first.
/// 2. For each block placed so far: inspect its last instruction.
///    - If the last instruction is an unconditional branch (`MOpcode::Br`)
///      with a label operand, try to place that label's block immediately next.
/// 3. Append any remaining unplaced blocks in their original relative order.
/// 4. Reorder `fn.blocks` in-place according to the computed trace.
///
/// After reordering, `PeepholePass` eliminates the now-redundant fall-through
/// branches (the peephole already handles `b <next_block>` removal).
///
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/BlockLayoutPass.hpp"

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/common/Diagnostics.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::aarch64::passes {

namespace {

static bool isConditionalBranch(MOpcode opc) {
    switch (opc) {
        case MOpcode::BCond:
        case MOpcode::Cbz:
        case MOpcode::Cbnz:
            return true;
        default:
            return false;
    }
}

static std::optional<std::size_t> lookupUnplacedTarget(
    const MInstr &instr,
    const std::unordered_map<std::string, std::size_t> &nameToIdx,
    const std::vector<bool> &placed) {
    if (instr.ops.empty() || instr.ops.back().kind != MOperand::Kind::Label)
        return std::nullopt;
    auto it = nameToIdx.find(instr.ops.back().label);
    if (it == nameToIdx.end() || placed[it->second])
        return std::nullopt;
    return it->second;
}

/// @brief Reorder the blocks of a single function using the greedy trace.
static void layoutFunction(MFunction &fn) {
    const std::size_t n = fn.blocks.size();
    if (n <= 1)
        return;

    // Build name → original-index map.
    std::unordered_map<std::string, std::size_t> nameToIdx;
    nameToIdx.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        nameToIdx[fn.blocks[i].name] = i;

    std::vector<bool> placed(n, false);
    std::vector<std::size_t> order;
    order.reserve(n);

    // Greedy trace: always start at the entry block (index 0).
    std::size_t cur = 0;
    while (order.size() < n) {
        if (!placed[cur]) {
            placed[cur] = true;
            order.push_back(cur);

            // Look at the last instruction of this block to find the preferred
            // fall-through successor.
            const MBasicBlock &bb = fn.blocks[cur];
            if (!bb.instrs.empty()) {
                const MInstr &last = bb.instrs.back();
                if (last.opc == MOpcode::Br) {
                    if (auto target = lookupUnplacedTarget(last, nameToIdx, placed)) {
                        cur = *target;
                        continue; // extend the trace
                    }
                }

                if (bb.instrs.size() >= 2 && isConditionalBranch(bb.instrs[bb.instrs.size() - 2].opc) &&
                    last.opc == MOpcode::Br) {
                    // Prefer the explicit false-edge target so the trailing
                    // unconditional branch can collapse into fallthrough.
                    if (auto fallthrough = lookupUnplacedTarget(last, nameToIdx, placed)) {
                        cur = *fallthrough;
                        continue;
                    }
                    if (auto taken = lookupUnplacedTarget(
                            bb.instrs[bb.instrs.size() - 2], nameToIdx, placed)) {
                        cur = *taken;
                        continue;
                    }
                }

                if (isConditionalBranch(last.opc)) {
                    if (auto target = lookupUnplacedTarget(last, nameToIdx, placed)) {
                        cur = *target;
                        continue;
                    }
                }
            }
        }

        // Advance to the next unplaced block in original order.
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

    // If the trace didn't differ from original order, skip the reorder.
    bool changed = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (order[i] != i) {
            changed = true;
            break;
        }
    }
    if (!changed)
        return;

    // Reorder fn.blocks according to the computed trace.
    std::vector<MBasicBlock> reordered;
    reordered.reserve(n);
    for (std::size_t idx : order)
        reordered.push_back(std::move(fn.blocks[idx]));
    fn.blocks = std::move(reordered);
}

} // namespace

bool BlockLayoutPass::run(AArch64Module &module, Diagnostics & /*diags*/) {
    for (MFunction &fn : module.mir)
        layoutFunction(fn);
    return true;
}

} // namespace viper::codegen::aarch64::passes
