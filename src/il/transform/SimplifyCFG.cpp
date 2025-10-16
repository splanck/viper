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

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

    void resize(size_t count, bool value = false)
    {
        bits_.assign(count, value);
    }

    bool test(size_t index) const
    {
        return bits_.at(index);
    }

    void set(size_t index)
    {
        bits_.at(index) = true;
    }

    size_t size() const
    {
        return bits_.size();
    }

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

void verifyIntermediateState(const il::core::Module *module)
{
    if (!module)
        return;

    auto verified = il::verify::Verifier::verify(*module);
    assert(verified && "SimplifyCFG verification failed after transformation batch");
    (void)verified;
}
#else
void verifyPreconditions(const il::core::Module *) {}

void verifyPostconditions(const il::core::Module *) {}

void verifyIntermediateState(const il::core::Module *) {}
#endif

using il::core::BasicBlock;
using il::core::Function;
using il::core::Instr;
using il::core::Opcode;
using il::core::Value;
using SimplifyContext = il::transform::SimplifyCFG::SimplifyCFGPassContext;

bool readDebugFlagFromEnv()
{
    if (const char *flag = std::getenv("VIPER_DEBUG_PASSES"))
        return flag[0] != '\0';
    return false;
}

static bool isEHSensitiveImpl(const BasicBlock &B);
Instr *findTerminator(BasicBlock &block);

static void realignBranchArgs(SimplifyContext &ctx, BasicBlock &B)
{
    for (auto &pred : ctx.function.blocks)
    {
        Instr *term = findTerminator(pred);
        if (!term)
            continue;

        for (size_t edgeIdx = 0; edgeIdx < term->labels.size(); ++edgeIdx)
        {
            if (term->labels[edgeIdx] != B.label)
                continue;

            if (term->brArgs.size() <= edgeIdx)
            {
                assert(B.params.empty() &&
                       "missing branch args for block parameters");
                continue;
            }

            auto &args = term->brArgs[edgeIdx];

            if (B.params.empty())
            {
                args.clear();
                continue;
            }

            if (args.size() > B.params.size())
                args.resize(B.params.size());

            assert(args.size() == B.params.size() &&
                   "mismatched branch argument count after parameter update");
        }
    }
}

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

Instr *findTerminator(BasicBlock &block)
{
    for (auto it = block.instructions.rbegin(); it != block.instructions.rend(); ++it)
    {
        if (il::verify::isTerminator(it->op))
            return &*it;
    }
    return nullptr;
}

const Instr *findTerminator(const BasicBlock &block)
{
    return findTerminator(const_cast<BasicBlock &>(block));
}

Value substituteValue(const Value &value, const std::unordered_map<unsigned, Value> &mapping)
{
    if (value.kind != Value::Kind::Temp)
        return value;

    if (auto it = mapping.find(value.id); it != mapping.end())
        return it->second;

    return value;
}

static bool mergeSinglePred(SimplifyContext &ctx, BasicBlock &B)
{
    Function &F = ctx.function;

    auto blockIt = std::find_if(
        F.blocks.begin(), F.blocks.end(),
        [&](BasicBlock &blk) { return &blk == &B; });
    if (blockIt == F.blocks.end())
        return false;

    if (ctx.isEHSensitive(B))
        return false;

    BasicBlock *predBlock = nullptr;
    Instr *predTerm = nullptr;
    size_t predecessorEdges = 0;

    for (auto &candidate : F.blocks)
    {
        if (&candidate == &B)
            continue;

        Instr *term = findTerminator(candidate);
        if (!term)
            continue;

        for (size_t idx = 0; idx < term->labels.size(); ++idx)
        {
            if (term->labels[idx] != B.label)
                continue;

            ++predecessorEdges;
            if (predecessorEdges == 1)
            {
                predBlock = &candidate;
                predTerm = term;
            }
        }
    }

    if (predecessorEdges != 1)
        return false;

    if (!predBlock || !predTerm)
        return false;

    if (predTerm->op != Opcode::Br)
        return false;

    if (predTerm->labels.size() != 1)
        return false;

    if (predTerm->labels.front() != B.label)
        return false;

    Instr *blockTerm = findTerminator(B);
    if (!blockTerm)
        return false;

    std::vector<Value> incomingArgs;
    if (!predTerm->brArgs.empty())
    {
        if (predTerm->brArgs.size() != 1)
            return false;
        incomingArgs = predTerm->brArgs.front();
    }

    if (B.params.size() != incomingArgs.size())
        return false;

    std::unordered_map<unsigned, Value> substitution;
    substitution.reserve(B.params.size());

    for (size_t idx = 0; idx < B.params.size(); ++idx)
        substitution.emplace(B.params[idx].id, incomingArgs[idx]);

    if (!substitution.empty())
    {
        for (auto &instr : B.instructions)
        {
            for (auto &operand : instr.operands)
                operand = substituteValue(operand, substitution);

            for (auto &argList : instr.brArgs)
            {
                for (auto &val : argList)
                    val = substituteValue(val, substitution);
            }
        }
    }

    auto &predInstrs = predBlock->instructions;
    auto predTermIt = std::find_if(
        predInstrs.begin(), predInstrs.end(),
        [&](Instr &instr) { return &instr == predTerm; });
    if (predTermIt == predInstrs.end())
        return false;

    auto &blockInstrs = B.instructions;
    auto blockTermIt = std::find_if(
        blockInstrs.begin(), blockInstrs.end(),
        [&](Instr &instr) { return &instr == blockTerm; });
    if (blockTermIt == blockInstrs.end())
        return false;

    std::vector<Instr> movedInstrs;
    movedInstrs.reserve(blockInstrs.size() > 0 ? blockInstrs.size() - 1 : 0);
    for (auto it = blockInstrs.begin(); it != blockInstrs.end(); ++it)
    {
        if (it == blockTermIt)
            continue;
        movedInstrs.push_back(std::move(*it));
    }

    Instr newTerm = std::move(*blockTermIt);
    for (auto &label : newTerm.labels)
    {
        if (label == B.label)
            label = predBlock->label;
    }

    predInstrs.erase(predTermIt);

    for (auto &instr : movedInstrs)
        predInstrs.push_back(std::move(instr));

    predInstrs.push_back(std::move(newTerm));
    predBlock->terminated = true;

    size_t blockIndex = static_cast<size_t>(std::distance(F.blocks.begin(), blockIt));
    F.blocks.erase(F.blocks.begin() + static_cast<std::ptrdiff_t>(blockIndex));

    return true;
}

static void redirectPredecessor(BasicBlock &Pred, BasicBlock &Dead, BasicBlock &Succ)
{
    Instr *predTerm = findTerminator(Pred);
    if (!predTerm)
        return;

    bool referencesDead = false;
    for (const auto &label : predTerm->labels)
    {
        if (label == Dead.label)
        {
            referencesDead = true;
            break;
        }
    }

    if (!referencesDead)
        return;

    Instr *deadTerm = findTerminator(Dead);
    assert(deadTerm && deadTerm->op == Opcode::Br);
    assert(deadTerm->labels.size() == 1);

    const std::vector<Value> *deadArgs = nullptr;
    if (!deadTerm->brArgs.empty())
    {
        assert(deadTerm->brArgs.size() == 1);
        deadArgs = &deadTerm->brArgs.front();
    }

    std::unordered_map<unsigned, Value> substitution;
    substitution.reserve(Dead.params.size());

    for (size_t idx = 0; idx < predTerm->labels.size(); ++idx)
    {
        if (predTerm->labels[idx] != Dead.label)
            continue;

        std::vector<Value> incomingArgs;
        if (idx < predTerm->brArgs.size())
            incomingArgs = predTerm->brArgs[idx];

        assert(incomingArgs.size() == Dead.params.size());

        substitution.clear();
        for (size_t paramIdx = 0; paramIdx < Dead.params.size(); ++paramIdx)
            substitution.emplace(Dead.params[paramIdx].id, incomingArgs[paramIdx]);

        std::vector<Value> newArgs;
        if (deadArgs)
        {
            newArgs.reserve(deadArgs->size());
            for (const auto &value : *deadArgs)
                newArgs.push_back(substituteValue(value, substitution));
        }

        predTerm->labels[idx] = Succ.label;
        if (predTerm->brArgs.size() <= idx)
            predTerm->brArgs.resize(idx + 1);
        predTerm->brArgs[idx] = std::move(newArgs);
    }
}

static bool shrinkParamsEqualAcrossPreds(SimplifyContext &ctx, BasicBlock &B)
{
    bool removedAny = false;

    while (true)
    {
        bool removedThisIteration = false;

        for (size_t paramIdx = 0; paramIdx < B.params.size();)
        {
            const unsigned paramId = B.params[paramIdx].id;
            Value commonValue{};
            bool hasCommonValue = false;
            bool mismatch = false;

            for (auto &pred : ctx.function.blocks)
            {
                Instr *term = findTerminator(pred);
                if (!term)
                    continue;

                for (size_t edgeIdx = 0; edgeIdx < term->labels.size(); ++edgeIdx)
                {
                    if (term->labels[edgeIdx] != B.label)
                        continue;

                    if (term->brArgs.size() <= edgeIdx)
                    {
                        mismatch = true;
                        break;
                    }

                    const auto &args = term->brArgs[edgeIdx];
                    if (args.size() != B.params.size())
                    {
                        mismatch = true;
                        break;
                    }

                    const Value &incoming = args[paramIdx];
                    if (!hasCommonValue)
                    {
                        commonValue = incoming;
                        hasCommonValue = true;
                    }
                    else if (!valuesEqual(incoming, commonValue))
                    {
                        mismatch = true;
                        break;
                    }
                }

                if (mismatch)
                    break;
            }

            if (!hasCommonValue || mismatch)
            {
                ++paramIdx;
                continue;
            }

            auto replaceUses = [&](Value &val) {
                if (val.kind == Value::Kind::Temp && val.id == paramId)
                    val = commonValue;
            };

            for (auto &instr : B.instructions)
            {
                for (auto &operand : instr.operands)
                    replaceUses(operand);

                for (auto &argList : instr.brArgs)
                {
                    for (auto &val : argList)
                        replaceUses(val);
                }
            }

            for (auto &pred : ctx.function.blocks)
            {
                Instr *term = findTerminator(pred);
                if (!term)
                    continue;

                for (size_t edgeIdx = 0; edgeIdx < term->labels.size(); ++edgeIdx)
                {
                    if (term->labels[edgeIdx] != B.label)
                        continue;

                    if (term->brArgs.size() <= edgeIdx)
                        continue;

                    auto &args = term->brArgs[edgeIdx];
                    if (paramIdx < args.size())
                    {
                        args.erase(args.begin() + static_cast<std::ptrdiff_t>(paramIdx));
                    }
                }
            }

            B.params.erase(B.params.begin() + static_cast<std::ptrdiff_t>(paramIdx));
            removedThisIteration = true;
            removedAny = true;
        }

        if (!removedThisIteration)
            break;
    }

    if (removedAny)
        realignBranchArgs(ctx, B);

    return removedAny;
}

static bool dropUnusedParams(SimplifyContext &ctx, BasicBlock &B)
{
    bool removedAny = false;

    for (size_t paramIdx = 0; paramIdx < B.params.size();)
    {
        const unsigned paramId = B.params[paramIdx].id;
        bool used = false;

        for (const auto &instr : B.instructions)
        {
            auto checkValue = [&](const Value &value) {
                return value.kind == Value::Kind::Temp && value.id == paramId;
            };

            for (const auto &operand : instr.operands)
            {
                if (checkValue(operand))
                {
                    used = true;
                    break;
                }
            }

            if (used)
                break;

            for (const auto &argList : instr.brArgs)
            {
                for (const auto &value : argList)
                {
                    if (checkValue(value))
                    {
                        used = true;
                        break;
                    }
                }

                if (used)
                    break;
            }

            if (used)
                break;
        }

        if (used)
        {
            ++paramIdx;
            continue;
        }

        for (auto &pred : ctx.function.blocks)
        {
            Instr *term = findTerminator(pred);
            if (!term)
                continue;

            for (size_t edgeIdx = 0; edgeIdx < term->labels.size(); ++edgeIdx)
            {
                if (term->labels[edgeIdx] != B.label)
                    continue;

                if (term->brArgs.size() <= edgeIdx)
                    continue;

                auto &args = term->brArgs[edgeIdx];
                if (paramIdx < args.size())
                {
                    args.erase(args.begin() +
                               static_cast<std::ptrdiff_t>(paramIdx));
                }
            }
        }

        B.params.erase(B.params.begin() + static_cast<std::ptrdiff_t>(paramIdx));
        removedAny = true;
    }

    if (removedAny)
        realignBranchArgs(ctx, B);

    return removedAny;
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

        auto addLabel = [&](const std::string &label)
        { enqueueSuccessor(reachable, worklist, lookupBlockIndex(labelToIndex, label)); };

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

static bool isEHSensitiveImpl(const BasicBlock &B)
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

static bool isEntryLabel(const std::string &label)
{
    return label == "entry" || label.rfind("entry_", 0) == 0;
}

[[maybe_unused]] static bool isEmptyForwardingBlock(SimplifyContext &ctx, const BasicBlock &B)
{
    if (isEntryLabel(B.label))
        return false;

    if (ctx.isEHSensitive(B))
        return false;

    if (B.instructions.empty())
        return false;

    const Instr *terminator = findTerminator(B);
    if (!terminator)
        return false;

    if (terminator->op != Opcode::Br)
        return false;

    if (terminator->labels.size() != 1)
        return false;

    if (&B.instructions.back() != terminator)
        return false;

    std::unordered_set<unsigned> definedTemps;
    for (const auto &instr : B.instructions)
    {
        if (instr.result)
            definedTemps.insert(*instr.result);
    }

    for (const auto &instr : B.instructions)
    {
        if (&instr == terminator)
            break;

        if (hasSideEffects(instr))
            return false;
    }

    if (!terminator->brArgs.empty())
    {
        if (terminator->brArgs.size() != 1)
            return false;

        for (const auto &value : terminator->brArgs.front())
        {
            if (value.kind == Value::Kind::Temp && definedTemps.count(value.id))
                return false;
        }
    }

    return true;
}

struct [[maybe_unused]] SoleSuccessor
{
    const std::string *label = nullptr;
    const std::vector<Value> *args = nullptr;
};

[[maybe_unused]] static SoleSuccessor getSoleSuccessor(const BasicBlock &B)
{
    SoleSuccessor info;
    const Instr *terminator = findTerminator(B);
    if (!terminator)
        return info;

    if (terminator->op != Opcode::Br)
        return info;

    if (terminator->labels.size() != 1)
        return info;

    info.label = &terminator->labels.front();
    if (!terminator->brArgs.empty())
    {
        if (terminator->brArgs.size() != 1)
        {
            info.label = nullptr;
            return info;
        }
        info.args = &terminator->brArgs.front();
    }
    return info;
}

static bool foldTrivialSwitchToBr(SimplifyContext &ctx)
{
    Function &F = ctx.function;
    bool changed = false;

    for (auto &block : F.blocks)
    {
        if (ctx.isEHSensitive(block))
            continue;

        for (auto &instr : block.instructions)
        {
            if (instr.op != Opcode::SwitchI32)
                continue;

            bool simplified = false;
            const size_t caseCount = il::core::switchCaseCount(instr);

            if (caseCount == 0)
            {
                rewriteToUnconditionalBranch(instr, 0);
                simplified = true;
            }
            else if (caseCount == 1)
            {
                const std::string &defaultLabel = il::core::switchDefaultLabel(instr);
                const std::string &caseLabel = il::core::switchCaseLabel(instr, 0);
                if (defaultLabel == caseLabel)
                {
                    const auto &defaultArgs = il::core::switchDefaultArgs(instr);
                    const auto &caseArgs = il::core::switchCaseArgs(instr, 0);
                    if (valueVectorsEqual(defaultArgs, caseArgs))
                    {
                        rewriteToUnconditionalBranch(instr, 0);
                        simplified = true;
                    }
                }
            }

            if (simplified)
            {
                changed = true;
                ++ctx.stats.switchToBr;
                if (ctx.isDebugLoggingEnabled())
                {
                    std::string message = "folded trivial switch in block '" + block.label + "'";
                    ctx.logDebug(message);
                }
            }
        }
    }

    return changed;
}

static bool foldTrivialCbrToBr(SimplifyContext &ctx)
{
    Function &F = ctx.function;
    bool changed = false;

    for (auto &block : F.blocks)
    {
        if (ctx.isEHSensitive(block))
            continue;

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

            if (!simplified && instr.labels.size() >= 2 &&
                instr.labels[0] == instr.labels[1])
            {
                const std::vector<Value> *trueArgs =
                    instr.brArgs.size() > 0 ? &instr.brArgs[0] : nullptr;
                const std::vector<Value> *falseArgs =
                    instr.brArgs.size() > 1 ? &instr.brArgs[1] : nullptr;
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
                changed = true;
                ++ctx.stats.cbrToBr;
                if (ctx.isDebugLoggingEnabled())
                {
                    std::string message = "simplified conditional branch in block '" + block.label + "'";
                    ctx.logDebug(message);
                }
            }
        }
    }

    return changed;
}

static bool mergeSinglePredBlocks(SimplifyContext &ctx)
{
    Function &F = ctx.function;

    bool changed = false;
    size_t blockIndex = 0;
    while (blockIndex < F.blocks.size())
    {
        const bool debugEnabled = ctx.isDebugLoggingEnabled();
        std::string mergedLabel;
        if (debugEnabled)
            mergedLabel = F.blocks[blockIndex].label;

        if (mergeSinglePred(ctx, F.blocks[blockIndex]))
        {
            changed = true;
            ++ctx.stats.blocksMerged;
            if (debugEnabled)
            {
                std::string message = "merged block '" + mergedLabel + "' into its predecessor";
                ctx.logDebug(message);
            }
            continue;
        }
        ++blockIndex;
    }

    return changed;
}

static bool canonicalizeParamsAndArgs(SimplifyContext &ctx)
{
    Function &F = ctx.function;

    bool changed = false;

    for (auto &block : F.blocks)
    {
        if (ctx.isEHSensitive(block))
            continue;

        if (block.params.empty())
            continue;

        const size_t beforeShrink = block.params.size();
        if (shrinkParamsEqualAcrossPreds(ctx, block))
        {
            const size_t removed = beforeShrink - block.params.size();
            if (removed > 0)
            {
                changed = true;
                ctx.stats.paramsShrunk += removed;
                if (ctx.isDebugLoggingEnabled())
                {
                    std::string message = "replaced duplicated params in block '" + block.label + "', removed " +
                                          std::to_string(removed);
                    ctx.logDebug(message);
                }
            }
        }

        if (block.params.empty())
            continue;

        const size_t beforeDrop = block.params.size();
        if (dropUnusedParams(ctx, block))
        {
            const size_t removed = beforeDrop - block.params.size();
            if (removed > 0)
            {
                changed = true;
                ctx.stats.paramsShrunk += removed;
                if (ctx.isDebugLoggingEnabled())
                {
                    std::string message = "dropped unused params in block '" + block.label + "', removed " +
                                          std::to_string(removed);
                    ctx.logDebug(message);
                }
            }
        }
    }

    return changed;
}

static bool removeEmptyForwarders(SimplifyContext &ctx)
{
    Function &F = ctx.function;
    bool changed = false;

    std::vector<std::string> forwardingBlocks;
    forwardingBlocks.reserve(F.blocks.size());
    for (const auto &block : F.blocks)
    {
        if (isEmptyForwardingBlock(ctx, block))
            forwardingBlocks.push_back(block.label);
    }

    size_t removedBlocks = 0;

    for (const auto &deadLabel : forwardingBlocks)
    {
        auto deadIt = std::find_if(
            F.blocks.begin(),
            F.blocks.end(),
            [&](const BasicBlock &B) { return B.label == deadLabel; });
        if (deadIt == F.blocks.end())
            continue;

        BasicBlock &dead = *deadIt;
        Instr *deadTerm = findTerminator(dead);
        if (!deadTerm || deadTerm->labels.size() != 1)
            continue;

        const std::string &succLabel = deadTerm->labels.front();
        if (succLabel == dead.label)
            continue;

        auto succIt = std::find_if(
            F.blocks.begin(), F.blocks.end(),
            [&](const BasicBlock &B) { return B.label == succLabel; });
        if (succIt == F.blocks.end())
            continue;

        BasicBlock &succ = *succIt;

        size_t redirected = 0;
        for (auto &pred : F.blocks)
        {
            Instr *predTerm = findTerminator(pred);
            if (!predTerm)
                continue;

            bool touchesDead = false;
            for (const auto &label : predTerm->labels)
            {
                if (label == dead.label)
                {
                    touchesDead = true;
                    break;
                }
            }

            if (!touchesDead)
                continue;

            redirectPredecessor(pred, dead, succ);
            ++redirected;
        }

        if (redirected > 0)
        {
            changed = true;
            ctx.stats.predsMerged += redirected;
            if (ctx.isDebugLoggingEnabled())
            {
                std::string message = "redirected " + std::to_string(redirected) +
                                      " predecessor edges around block '" + dead.label + "'";
                ctx.logDebug(message);
            }
        }

        bool hasPreds = false;
        for (const auto &pred : F.blocks)
        {
            const Instr *predTerm = findTerminator(pred);
            if (!predTerm)
                continue;

            for (const auto &label : predTerm->labels)
            {
                if (label == dead.label)
                {
                    hasPreds = true;
                    break;
                }
            }

            if (hasPreds)
                break;
        }

        if (hasPreds)
            continue;

        F.blocks.erase(F.blocks.begin() +
                        std::distance(F.blocks.begin(), deadIt));
        ++removedBlocks;
    }

    if (removedBlocks > 0)
    {
        changed = true;
        ctx.stats.emptyBlocksRemoved += removedBlocks;
        if (ctx.isDebugLoggingEnabled())
        {
            std::string message =
                "removed " + std::to_string(removedBlocks) + " empty forwarding block" +
                (removedBlocks == 1 ? "" : "s");
            ctx.logDebug(message);
        }
    }

    return changed;
}

static bool removeUnreachable(SimplifyContext &ctx)
{
    Function &F = ctx.function;
    llvm_like::BitVector reachable = markReachable(F);

    std::vector<size_t> unreachableBlocks;
    unreachableBlocks.reserve(F.blocks.size());
    for (size_t index = 1; index < F.blocks.size(); ++index)
    {
        if (!reachable.test(index))
            unreachableBlocks.push_back(index);
    }

    size_t removedBlocks = 0;

    for (auto it = unreachableBlocks.rbegin(); it != unreachableBlocks.rend(); ++it)
    {
        const size_t blockIndex = *it;
        if (blockIndex >= F.blocks.size())
            continue;

        BasicBlock &candidate = F.blocks[blockIndex];
        if (ctx.isEHSensitive(candidate))
            continue;

        const std::string label = candidate.label;

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

        F.blocks.erase(F.blocks.begin() +
                        static_cast<std::ptrdiff_t>(blockIndex));
        ++removedBlocks;
    }

    if (removedBlocks > 0)
    {
        ctx.stats.unreachableRemoved += removedBlocks;
        if (ctx.isDebugLoggingEnabled())
        {
            std::string message =
                "erased " + std::to_string(removedBlocks) + " unreachable block" +
                (removedBlocks == 1 ? "" : "s");
            ctx.logDebug(message);
        }
        return true;
    }

    return false;
}

static void invalidateCFGAndDominators(Function &F)
{
    static_cast<void>(F);
    // TODO: Hook into analysis invalidation once caches are connected.
}

} // namespace

namespace il::transform
{

SimplifyCFG::SimplifyCFGPassContext::SimplifyCFGPassContext(il::core::Function &function,
                                                            const il::core::Module *module,
                                                            Stats &stats)
    : function(function), module(module), stats(stats),
      debugLoggingEnabled_(readDebugFlagFromEnv())
{
}

bool SimplifyCFG::SimplifyCFGPassContext::isDebugLoggingEnabled() const
{
    return debugLoggingEnabled_;
}

void SimplifyCFG::SimplifyCFGPassContext::logDebug(std::string_view message) const
{
    if (!isDebugLoggingEnabled())
        return;

    const char *name = function.name.c_str();
    std::fprintf(stderr, "[DEBUG][SimplifyCFG] %s: %.*s\n", name,
                 static_cast<int>(message.size()), message.data());
}

bool SimplifyCFG::SimplifyCFGPassContext::isEHSensitive(const il::core::BasicBlock &block) const
{
    return isEHSensitiveImpl(block);
}

bool SimplifyCFG::run(il::core::Function &F, Stats *outStats)
{
    verifyPreconditions(module_);

    Stats stats{};
    SimplifyCFGPassContext ctx(F, module_, stats);

    bool changedAny = false;

    for (int iter = 0; iter < 8; ++iter)
    {
        bool changed = false;
        if (aggressive)
            changed |= foldTrivialSwitchToBr(ctx);
        changed |= foldTrivialCbrToBr(ctx);
        changed |= removeEmptyForwarders(ctx);
        changed |= mergeSinglePredBlocks(ctx);
        changed |= removeUnreachable(ctx);
        changed |= canonicalizeParamsAndArgs(ctx);
        if (!changed)
            break;
        changedAny = true;
        verifyIntermediateState(module_);
    }

    if (changedAny)
    {
        verifyPostconditions(module_);
        invalidateCFGAndDominators(F);
    }

    if (outStats)
        *outStats = stats;

    return changedAny;
}

} // namespace il::transform
