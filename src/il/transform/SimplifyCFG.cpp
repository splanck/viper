//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the scaffold for the control-flow graph simplification pass. The
// actual simplification logic will be filled in by future work; for now, the
// pass exposes a run method that records default statistics and advertises that
// no mutations take place.
//
//===----------------------------------------------------------------------===//

#include "il/transform/SimplifyCFG.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>
#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__has_include)
#if __has_include("llvm_like/ADT/BitVector.hpp")
#include "llvm_like/ADT/BitVector.hpp"
#define VIPER_HAVE_LLVM_LIKE_BITVECTOR 1
#endif
#endif

#ifndef VIPER_HAVE_LLVM_LIKE_BITVECTOR
#include <vector>

namespace llvm_like
{

class BitVector
{
  public:
    BitVector() = default;
    explicit BitVector(size_t count, bool value = false) : bits_(count, value) {}

    void resize(size_t count, bool value = false) { bits_.assign(count, value); }

    bool test(size_t index) const { return bits_.at(index); }

    void set(size_t index) { bits_.at(index) = true; }

    size_t size() const { return bits_.size(); }

  private:
    std::vector<bool> bits_;
};

} // namespace llvm_like

#endif // VIPER_HAVE_LLVM_LIKE_BITVECTOR

namespace
{

#ifndef NDEBUG
void verifyPreconditions(const il::core::Module *module)
{
    if (!module)
        return;

    auto verified = il::verify::Verifier::verify(*module);
    assert(verified && "SimplifyCFG precondition verification failed");
    (void)verified;
}
#else
void verifyPreconditions(const il::core::Module *) {}
#endif

using il::core::BasicBlock;
using il::core::Function;
using il::core::Instr;
using il::core::Opcode;

const Instr *findTerminator(const BasicBlock &block)
{
    for (auto it = block.instructions.rbegin(); it != block.instructions.rend(); ++it)
    {
        if (il::verify::isTerminator(it->op))
            return &*it;
    }
    return nullptr;
}

size_t lookupBlockIndex(const std::unordered_map<std::string, size_t> &labelToIndex,
                        const std::string &label)
{
    if (auto it = labelToIndex.find(label); it != labelToIndex.end())
        return it->second;
    return static_cast<size_t>(-1);
}

void enqueueSuccessor(llvm_like::BitVector &reachable,
                      std::deque<size_t> &worklist,
                      size_t successor)
{
    if (successor == static_cast<size_t>(-1))
        return;
    if (successor < reachable.size() && !reachable.test(successor))
    {
        reachable.set(successor);
        worklist.push_back(successor);
    }
}

static llvm_like::BitVector markReachable(Function &F)
{
    llvm_like::BitVector reachable(F.blocks.size(), false);
    if (F.blocks.empty())
        return reachable;

    std::unordered_map<std::string, size_t> labelToIndex;
    labelToIndex.reserve(F.blocks.size());
    for (size_t idx = 0; idx < F.blocks.size(); ++idx)
        labelToIndex.emplace(F.blocks[idx].label, idx);

    std::deque<size_t> worklist;
    reachable.set(0);
    worklist.push_back(0);

    while (!worklist.empty())
    {
        size_t index = worklist.front();
        worklist.pop_front();

        const BasicBlock &block = F.blocks[index];
        const Instr *terminator = findTerminator(block);
        if (!terminator)
            continue;

        auto addLabel = [&](const std::string &label) {
            enqueueSuccessor(reachable, worklist, lookupBlockIndex(labelToIndex, label));
        };

        switch (terminator->op)
        {
            case Opcode::Br:
                if (!terminator->labels.empty())
                    addLabel(terminator->labels.front());
                break;
            case Opcode::CBr:
            case Opcode::SwitchI32:
                for (const auto &label : terminator->labels)
                    addLabel(label);
                break;
            case Opcode::ResumeLabel:
                if (!terminator->labels.empty())
                    addLabel(terminator->labels.front());
                break;
            default:
                break;
        }
    }

    return reachable;
}

bool isResumeOpcode(Opcode op)
{
    return op == Opcode::ResumeSame || op == Opcode::ResumeNext || op == Opcode::ResumeLabel;
}

bool isEhStructuralOpcode(Opcode op)
{
    switch (op)
    {
        case Opcode::EhPush:
        case Opcode::EhPop:
        case Opcode::EhEntry:
            return true;
        default:
            return false;
    }
}

static bool isEHSensitive(const BasicBlock &B)
{
    if (B.instructions.empty())
        return false;

    if (B.instructions.front().op == Opcode::EhEntry)
        return true;

    for (const auto &instr : B.instructions)
    {
        if (isEhStructuralOpcode(instr.op))
            return true;
    }

    const Instr *terminator = findTerminator(B);
    return terminator && isResumeOpcode(terminator->op);
}

static bool hasSideEffects(const Instr &I)
{
    return il::core::getOpcodeInfo(I.op).hasSideEffects;
}

} // namespace

namespace il::transform
{

bool SimplifyCFG::run(il::core::Function &F, Stats *outStats)
{
    verifyPreconditions(module_);

    Stats stats{};
    bool changed = false;

    llvm_like::BitVector reachable = markReachable(F);

    std::vector<size_t> unreachableBlocks;
    unreachableBlocks.reserve(F.blocks.size());
    for (size_t index = 1; index < F.blocks.size(); ++index)
    {
        if (!reachable.test(index))
            unreachableBlocks.push_back(index);
    }

    for (auto it = unreachableBlocks.rbegin(); it != unreachableBlocks.rend(); ++it)
    {
        const size_t blockIndex = *it;
        if (blockIndex >= F.blocks.size())
            continue;

        const std::string label = F.blocks[blockIndex].label;

        for (auto &block : F.blocks)
        {
            for (auto &instr : block.instructions)
            {
                if (instr.labels.empty())
                    continue;

                for (size_t idx = 0; idx < instr.labels.size();)
                {
                    if (instr.labels[idx] == label)
                    {
                        instr.labels.erase(instr.labels.begin() + idx);
                        if (idx < instr.brArgs.size())
                            instr.brArgs.erase(instr.brArgs.begin() + idx);
                    }
                    else
                    {
                        ++idx;
                    }
                }
            }
        }

        F.blocks.erase(F.blocks.begin() + static_cast<std::ptrdiff_t>(blockIndex));
        ++stats.unreachableRemoved;
    }

    changed = stats.unreachableRemoved > 0;

    if (outStats)
        *outStats = stats;

    return changed;
}

} // namespace il::transform
