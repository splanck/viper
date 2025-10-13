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

void verifyPostconditions(const il::core::Module *module)
{
    if (!module)
        return;

    auto verified = il::verify::Verifier::verify(*module);
    assert(verified && "SimplifyCFG postcondition verification failed");
    (void)verified;
}
#else
void verifyPreconditions(const il::core::Module *) {}
void verifyPostconditions(const il::core::Module *) {}
#endif

using il::core::BasicBlock;
using il::core::Function;
using il::core::Instr;
using il::core::Opcode;
using il::core::Value;

bool valuesEqual(const Value &lhs, const Value &rhs)
{
    if (lhs.kind != rhs.kind)
        return false;

    switch (lhs.kind)
    {
        case Value::Kind::Temp:
            return lhs.id == rhs.id;
        case Value::Kind::ConstInt:
            return lhs.i64 == rhs.i64 && lhs.isBool == rhs.isBool;
        case Value::Kind::ConstFloat:
            return lhs.f64 == rhs.f64;
        case Value::Kind::ConstStr:
        case Value::Kind::GlobalAddr:
            return lhs.str == rhs.str;
        case Value::Kind::NullPtr:
            return true;
    }
    return false;
}

bool valueVectorsEqual(const std::vector<Value> &lhs, const std::vector<Value> &rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (size_t index = 0; index < lhs.size(); ++index)
    {
        if (!valuesEqual(lhs[index], rhs[index]))
            return false;
    }

    return true;
}

void rewriteToUnconditionalBranch(Instr &instr, size_t successorIndex)
{
    assert(successorIndex < instr.labels.size());
    const std::string target = instr.labels[successorIndex];
    instr.op = Opcode::Br;
    instr.operands.clear();
    instr.labels.clear();
    instr.labels.push_back(target);

    if (instr.brArgs.empty())
    {
        return;
    }

    std::vector<std::vector<Value>> newArgs;
    if (successorIndex < instr.brArgs.size())
        newArgs.push_back(instr.brArgs[successorIndex]);
    instr.brArgs = std::move(newArgs);
}

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

    for (auto &block : F.blocks)
    {
        for (auto &instr : block.instructions)
        {
            if (instr.op != Opcode::CBr)
                continue;

            bool simplified = false;

            if (!instr.operands.empty())
            {
                const Value &cond = instr.operands.front();
                if (cond.kind == Value::Kind::ConstInt && cond.isBool)
                {
                    const bool takeTrue = cond.i64 != 0;
                    const size_t successorIndex = takeTrue ? 0 : 1;
                    if (successorIndex < instr.labels.size())
                    {
                        rewriteToUnconditionalBranch(instr, successorIndex);
                        simplified = true;
                    }
                }
            }

            if (!simplified && instr.labels.size() >= 2 && instr.labels[0] == instr.labels[1])
            {
                const std::vector<Value> *trueArgs = instr.brArgs.size() > 0 ? &instr.brArgs[0] : nullptr;
                const std::vector<Value> *falseArgs = instr.brArgs.size() > 1 ? &instr.brArgs[1] : nullptr;
                bool argsMatch = false;
                if (!trueArgs && !falseArgs)
                {
                    argsMatch = true;
                }
                else if (trueArgs && falseArgs)
                {
                    argsMatch = valueVectorsEqual(*trueArgs, *falseArgs);
                }

                if (argsMatch)
                {
                    rewriteToUnconditionalBranch(instr, 0);
                    simplified = true;
                }
            }

            if (simplified)
            {
                ++stats.cbrToBr;
                changed = true;
            }
        }
    }

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

    changed |= stats.unreachableRemoved > 0;

    if (changed)
        verifyPostconditions(module_);

    if (outStats)
        *outStats = stats;

    return changed;
}

} // namespace il::transform
