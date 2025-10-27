#include "il/verify/Rules.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/DiagSink.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using il::core::BasicBlock;
using il::core::Function;
using il::core::Instr;
using il::core::Opcode;
using il::verify::VerifyDiagCode;

namespace
{

struct EhBalanceResult
{
    bool ok = true;
    VerifyDiagCode code = VerifyDiagCode::Unknown;
    const Instr *failingInstr = nullptr;
    std::string message;
};

struct ResumeLabelResult
{
    bool ok = true;
    const Instr *failingInstr = nullptr;
    std::string message;
};

std::string encodeStateKey(const std::vector<const BasicBlock *> &stack, bool hasResumeToken)
{
    std::string key;
    key.reserve(stack.size() * 8 + 4);
    key.append(hasResumeToken ? "1|" : "0|");
    for (const BasicBlock *handler : stack)
    {
        if (handler)
            key.append(handler->label);
        key.push_back(';');
    }
    return key;
}

const Instr *findTerminator(const BasicBlock &bb)
{
    for (const auto &instr : bb.instructions)
    {
        if (il::verify::isTerminator(instr.op))
            return &instr;
    }
    return nullptr;
}

using CFG = std::unordered_map<std::string, const BasicBlock *>;

CFG buildCfg(const Function &fn)
{
    CFG blockMap;
    blockMap.reserve(fn.blocks.size());
    for (const auto &bb : fn.blocks)
        blockMap[bb.label] = &bb;
    return blockMap;
}

std::vector<const BasicBlock *> gatherSuccessors(const Instr &terminator, const CFG &blockMap)
{
    std::vector<const BasicBlock *> successors;
    switch (terminator.op)
    {
        case Opcode::Br:
            if (!terminator.labels.empty())
            {
                if (auto it = blockMap.find(terminator.labels[0]); it != blockMap.end())
                    successors.push_back(it->second);
            }
            break;
        case Opcode::CBr:
        case Opcode::SwitchI32:
            for (const auto &label : terminator.labels)
            {
                if (auto it = blockMap.find(label); it != blockMap.end())
                    successors.push_back(it->second);
            }
            break;
        case Opcode::ResumeLabel:
            if (!terminator.labels.empty())
            {
                if (auto it = blockMap.find(terminator.labels[0]); it != blockMap.end())
                    successors.push_back(it->second);
            }
            break;
        default:
            break;
    }
    return successors;
}

struct EhState
{
    int depth = 0;
    const BasicBlock *bb = nullptr;
};

std::vector<const BasicBlock *> buildPath(const std::vector<EhState> &states,
                                          const std::vector<int> &parents,
                                          int index)
{
    std::vector<const BasicBlock *> path;
    for (int cur = index; cur >= 0; cur = parents[cur])
    {
        if (states[cur].bb)
            path.push_back(states[cur].bb);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::string formatPathString(const std::vector<const BasicBlock *> &path)
{
    std::string buffer;
    for (const BasicBlock *node : path)
    {
        if (!buffer.empty())
            buffer.append(" -> ");
        buffer.append(node->label);
    }
    return buffer;
}

EhBalanceResult analyzeEhBalance(const Function &fn)
{
    EhBalanceResult result;
    if (fn.blocks.empty())
        return result;

    CFG blockMap = buildCfg(fn);

    std::deque<int> worklist;
    std::vector<EhState> states;
    std::vector<int> parents;
    std::vector<std::vector<const BasicBlock *>> handlerStacks;
    std::vector<bool> resumeTokens;
    std::unordered_map<const BasicBlock *, std::unordered_set<std::string>> visited;

    states.push_back({0, &fn.blocks.front()});
    parents.push_back(-1);
    handlerStacks.emplace_back();
    resumeTokens.push_back(false);
    worklist.push_back(0);
    visited[&fn.blocks.front()].insert(encodeStateKey(handlerStacks.front(), resumeTokens.front()));

    while (!worklist.empty())
    {
        const int stateIndex = worklist.front();
        worklist.pop_front();

        EhState &state = states[stateIndex];
        const BasicBlock &bb = *state.bb;

        std::vector<const BasicBlock *> handlerStack = handlerStacks[stateIndex];
        bool hasResumeToken = resumeTokens[stateIndex];
        int depth = static_cast<int>(handlerStack.size());

        const Instr *terminator = nullptr;
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == Opcode::EhPush)
            {
                const BasicBlock *handlerBlock = nullptr;
                if (!instr.labels.empty())
                {
                    if (auto it = blockMap.find(instr.labels[0]); it != blockMap.end())
                        handlerBlock = it->second;
                }
                handlerStack.push_back(handlerBlock);
                depth = static_cast<int>(handlerStack.size());
            }
            else if (instr.op == Opcode::EhPop)
            {
                if (handlerStack.empty())
                {
                    const std::vector<const BasicBlock *> path = buildPath(states, parents, stateIndex);
                    result.ok = false;
                    result.code = VerifyDiagCode::EhStackUnderflow;
                    result.failingInstr = &instr;
                    result.message = "eh.pop without matching eh.push; path: ";
                    result.message += formatPathString(path);
                    return result;
                }

                handlerStack.pop_back();
                depth = static_cast<int>(handlerStack.size());
            }
            else if (instr.op == Opcode::ResumeSame || instr.op == Opcode::ResumeNext ||
                     instr.op == Opcode::ResumeLabel)
            {
                if (!hasResumeToken)
                {
                    const std::vector<const BasicBlock *> path = buildPath(states, parents, stateIndex);
                    result.ok = false;
                    result.code = VerifyDiagCode::EhResumeTokenMissing;
                    result.failingInstr = &instr;
                    result.message = "resume.* requires active resume token; path: ";
                    result.message += formatPathString(path);
                    return result;
                }

                if (!handlerStack.empty())
                    handlerStack.pop_back();
                hasResumeToken = false;
                depth = static_cast<int>(handlerStack.size());
            }

            if (il::verify::isTerminator(instr.op))
            {
                terminator = &instr;
                break;
            }
        }

        if (!terminator)
            continue;

        state.depth = depth;
        handlerStacks[stateIndex] = handlerStack;
        resumeTokens[stateIndex] = hasResumeToken;

        if (terminator->op == Opcode::Ret && depth != 0)
        {
            const std::vector<const BasicBlock *> path = buildPath(states, parents, stateIndex);
            result.ok = false;
            result.code = VerifyDiagCode::EhStackLeak;
            result.failingInstr = terminator;
            result.message = "unmatched eh.push depth ";
            result.message += std::to_string(depth);
            result.message += "; path: ";
            result.message += formatPathString(path);
            return result;
        }

        if (terminator->op == Opcode::Trap || terminator->op == Opcode::TrapFromErr)
        {
            if (!handlerStack.empty())
            {
                const BasicBlock *handlerBlock = handlerStack.back();
                if (handlerBlock)
                {
                    EhState nextState;
                    nextState.bb = handlerBlock;
                    nextState.depth = static_cast<int>(handlerStack.size());

                    std::vector<const BasicBlock *> nextStack = handlerStack;
                    bool nextResumeToken = true;
                    const std::string key = encodeStateKey(nextStack, nextResumeToken);
                    if (visited[handlerBlock].insert(key).second)
                    {
                        const int nextIndex = static_cast<int>(states.size());
                        states.push_back(nextState);
                        parents.push_back(stateIndex);
                        handlerStacks.push_back(std::move(nextStack));
                        resumeTokens.push_back(nextResumeToken);
                        worklist.push_back(nextIndex);
                    }
                }
            }
            continue;
        }

        const std::vector<const BasicBlock *> successors = gatherSuccessors(*terminator, blockMap);
        for (const BasicBlock *succ : successors)
        {
            EhState nextState;
            nextState.bb = succ;
            nextState.depth = static_cast<int>(handlerStack.size());

            std::vector<const BasicBlock *> nextStack = handlerStack;
            bool nextResumeToken = hasResumeToken;
            if (terminator->op == Opcode::ResumeLabel)
                nextResumeToken = false;

            const std::string key = encodeStateKey(nextStack, nextResumeToken);
            if (!visited[succ].insert(key).second)
                continue;

            const int nextIndex = static_cast<int>(states.size());
            states.push_back(nextState);
            parents.push_back(stateIndex);
            handlerStacks.push_back(std::move(nextStack));
            resumeTokens.push_back(nextResumeToken);
            worklist.push_back(nextIndex);
        }
    }

    return result;
}

const EhBalanceResult &cachedEhBalance(const Function &fn)
{
    thread_local const Function *cachedFn = nullptr;
    thread_local EhBalanceResult cachedResult;
    if (cachedFn != &fn)
    {
        cachedResult = analyzeEhBalance(fn);
        cachedFn = &fn;
    }
    return cachedResult;
}

bool isPotentialFaultingOpcode(Opcode op)
{
    switch (op)
    {
        case Opcode::EhPush:
        case Opcode::EhPop:
        case Opcode::EhEntry:
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
        case Opcode::Br:
        case Opcode::CBr:
        case Opcode::SwitchI32:
        case Opcode::Ret:
            return false;
        default:
            return true;
    }
}

using HandlerCoverage =
    std::unordered_map<const BasicBlock *, std::unordered_set<const BasicBlock *>>;

class HandlerCoverageTraversal
{
  public:
    HandlerCoverageTraversal(const CFG &blockMap, HandlerCoverage &coverage)
        : blockMap(blockMap), coverage(coverage)
    {
    }

    void compute(const Function &fn)
    {
        if (fn.blocks.empty())
            return;

        std::deque<State> worklist;
        State entryState;
        entryState.block = &fn.blocks.front();
        enqueueState(std::move(entryState), worklist);

        while (!worklist.empty())
        {
            State state = std::move(worklist.front());
            worklist.pop_front();

            const BasicBlock &bb = *state.block;
            State frame = state;

            const Instr *terminator = nullptr;
            for (const auto &instr : bb.instructions)
            {
                terminator = processEhInstruction(instr, bb, frame);
                if (terminator)
                    break;
            }

            if (!terminator)
                continue;

            if (terminator->op == Opcode::Trap || terminator->op == Opcode::TrapFromErr)
            {
                handleTrapTerminator(bb, frame, worklist);
                continue;
            }

            enqueueSuccessors(*terminator, frame, worklist);
        }
    }

  private:
    struct State
    {
        const BasicBlock *block = nullptr;
        std::vector<const BasicBlock *> handlerStack;
        bool hasResumeToken = false;
    };

    void enqueueState(State state, std::deque<State> &worklist)
    {
        if (!state.block)
            return;
        const std::string key = encodeStateKey(state.handlerStack, state.hasResumeToken);
        if (visited[state.block].insert(key).second)
            worklist.push_back(std::move(state));
    }

    const Instr *processEhInstruction(const Instr &instr, const BasicBlock &bb, State &state)
    {
        if (!state.hasResumeToken && !state.handlerStack.empty() && isPotentialFaultingOpcode(instr.op))
        {
            const BasicBlock *handlerBlock = state.handlerStack.back();
            if (handlerBlock)
                coverage[handlerBlock].insert(&bb);
        }

        if (instr.op == Opcode::EhPush)
        {
            const BasicBlock *handlerBlock = nullptr;
            if (!instr.labels.empty())
            {
                if (auto it = blockMap.find(instr.labels[0]); it != blockMap.end())
                    handlerBlock = it->second;
            }
            state.handlerStack.push_back(handlerBlock);
        }
        else if (instr.op == Opcode::EhPop)
        {
            if (!state.handlerStack.empty())
                state.handlerStack.pop_back();
        }
        else if (instr.op == Opcode::ResumeSame || instr.op == Opcode::ResumeNext)
        {
            if (!state.handlerStack.empty())
                state.handlerStack.pop_back();
            state.hasResumeToken = false;
        }
        else if (instr.op == Opcode::ResumeLabel)
        {
            if (!state.handlerStack.empty())
                state.handlerStack.pop_back();
            state.hasResumeToken = false;
        }

        if (il::verify::isTerminator(instr.op))
            return &instr;
        return nullptr;
    }

    void handleTrapTerminator(const BasicBlock &bb, const State &state, std::deque<State> &worklist)
    {
        if (state.handlerStack.empty())
            return;

        const BasicBlock *handlerBlock = state.handlerStack.back();
        if (!handlerBlock)
            return;

        coverage[handlerBlock].insert(&bb);

        State nextState;
        nextState.block = handlerBlock;
        nextState.handlerStack = state.handlerStack;
        nextState.hasResumeToken = true;
        enqueueState(std::move(nextState), worklist);
    }

    void enqueueSuccessors(const Instr &terminator, const State &state, std::deque<State> &worklist)
    {
        const std::vector<const BasicBlock *> successors = gatherSuccessors(terminator, blockMap);
        for (const BasicBlock *succ : successors)
        {
            State nextState = state;
            nextState.block = succ;
            if (terminator.op == Opcode::ResumeLabel)
                nextState.hasResumeToken = false;
            enqueueState(std::move(nextState), worklist);
        }
    }

    const CFG &blockMap;
    HandlerCoverage &coverage;
    std::unordered_map<const BasicBlock *, std::unordered_set<std::string>> visited;
};

struct PostDomInfo
{
    std::vector<const BasicBlock *> nodes;
    std::unordered_map<const BasicBlock *, size_t> indices;
    std::vector<std::vector<uint8_t>> matrix;
};

PostDomInfo computePostDominators(const Function &fn, const CFG &blockMap)
{
    PostDomInfo info;
    if (fn.blocks.empty())
        return info;

    const BasicBlock *entry = &fn.blocks.front();
    std::unordered_set<const BasicBlock *> reachable;
    std::deque<const BasicBlock *> queue;

    queue.push_back(entry);
    reachable.insert(entry);

    while (!queue.empty())
    {
        const BasicBlock *bb = queue.front();
        queue.pop_front();

        const Instr *terminator = findTerminator(*bb);
        if (!terminator)
            continue;

        const std::vector<const BasicBlock *> successors = gatherSuccessors(*terminator, blockMap);
        for (const BasicBlock *succ : successors)
        {
            if (reachable.insert(succ).second)
                queue.push_back(succ);
        }
    }

    info.nodes.reserve(reachable.size());
    for (const auto &bb : fn.blocks)
    {
        if (!reachable.count(&bb))
            continue;
        info.indices[&bb] = info.nodes.size();
        info.nodes.push_back(&bb);
    }

    const size_t n = info.nodes.size();
    info.matrix.assign(n, std::vector<uint8_t>(n, 1));

    std::vector<std::vector<size_t>> successors(n);
    std::vector<uint8_t> isExit(n, 0);

    for (size_t idx = 0; idx < n; ++idx)
    {
        const BasicBlock *bb = info.nodes[idx];
        const Instr *terminator = findTerminator(*bb);
        if (!terminator)
        {
            std::fill(info.matrix[idx].begin(), info.matrix[idx].end(), 0);
            info.matrix[idx][idx] = 1;
            isExit[idx] = 1;
            continue;
        }

        const std::vector<const BasicBlock *> succBlocks = gatherSuccessors(*terminator, blockMap);
        for (const BasicBlock *succ : succBlocks)
        {
            auto it = info.indices.find(succ);
            if (it != info.indices.end())
                successors[idx].push_back(it->second);
        }

        if (successors[idx].empty())
        {
            std::fill(info.matrix[idx].begin(), info.matrix[idx].end(), 0);
            info.matrix[idx][idx] = 1;
            isExit[idx] = 1;
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t idx = 0; idx < n; ++idx)
        {
            if (isExit[idx])
                continue;

            std::vector<uint8_t> newSet(n, 1);
            if (!successors[idx].empty())
            {
                newSet = info.matrix[successors[idx].front()];
                for (size_t succPos = 1; succPos < successors[idx].size(); ++succPos)
                {
                    const size_t succIdx = successors[idx][succPos];
                    for (size_t bit = 0; bit < n; ++bit)
                        newSet[bit] = static_cast<uint8_t>(newSet[bit] & info.matrix[succIdx][bit]);
                }
            }
            else
            {
                std::fill(newSet.begin(), newSet.end(), 0);
            }

            newSet[idx] = 1;
            if (newSet != info.matrix[idx])
            {
                info.matrix[idx] = std::move(newSet);
                changed = true;
            }
        }
    }

    return info;
}

bool isPostDominator(const PostDomInfo &info, const BasicBlock *from, const BasicBlock *candidate)
{
    if (info.nodes.empty())
        return false;

    auto fromIt = info.indices.find(from);
    auto candIt = info.indices.find(candidate);
    if (fromIt == info.indices.end() || candIt == info.indices.end())
        return false;

    return info.matrix[fromIt->second][candIt->second] != 0;
}

ResumeLabelResult analyzeResumeLabels(const Function &fn)
{
    ResumeLabelResult result;
    CFG blockMap = buildCfg(fn);
    HandlerCoverage coverage;
    HandlerCoverageTraversal traversal(blockMap, coverage);
    traversal.compute(fn);
    const PostDomInfo postDomInfo = computePostDominators(fn, blockMap);

    for (const auto &bb : fn.blocks)
    {
        auto coverageIt = coverage.find(&bb);
        if (coverageIt == coverage.end())
            continue;

        for (const auto &instr : bb.instructions)
        {
            if (instr.op != Opcode::ResumeLabel)
                continue;
            if (instr.labels.empty())
                continue;

            const auto targetIt = blockMap.find(instr.labels[0]);
            if (targetIt == blockMap.end())
                continue;

            const BasicBlock *targetBlock = targetIt->second;
            for (const BasicBlock *faultingBlock : coverageIt->second)
            {
                const Instr *faultTerminator = findTerminator(*faultingBlock);
                if (!faultTerminator)
                    continue;

                const std::vector<const BasicBlock *> faultSuccs =
                    gatherSuccessors(*faultTerminator, blockMap);
                if (faultSuccs.empty())
                    continue;

                if (isPostDominator(postDomInfo, faultingBlock, targetBlock))
                    continue;

                result.ok = false;
                result.failingInstr = &instr;
                result.message = "target ^";
                result.message += instr.labels[0];
                result.message += " must postdominate block ";
                result.message += faultingBlock->label;
                return result;
            }
        }
    }

    return result;
}

const ResumeLabelResult &cachedResumeLabels(const Function &fn)
{
    thread_local const Function *cachedFn = nullptr;
    thread_local ResumeLabelResult cachedResult;
    if (cachedFn != &fn)
    {
        cachedResult = analyzeResumeLabels(fn);
        cachedFn = &fn;
    }
    return cachedResult;
}

bool ruleEhStackUnderflow(const Function &fn, const Instr &instr, std::string &out)
{
    const EhBalanceResult &result = cachedEhBalance(fn);
    if (!result.ok && result.code == VerifyDiagCode::EhStackUnderflow && result.failingInstr == &instr)
    {
        out = result.message;
        return false;
    }
    return true;
}

bool ruleEhStackLeak(const Function &fn, const Instr &instr, std::string &out)
{
    const EhBalanceResult &result = cachedEhBalance(fn);
    if (!result.ok && result.code == VerifyDiagCode::EhStackLeak && result.failingInstr == &instr)
    {
        out = result.message;
        return false;
    }
    return true;
}

bool ruleEhResumeToken(const Function &fn, const Instr &instr, std::string &out)
{
    const EhBalanceResult &result = cachedEhBalance(fn);
    if (!result.ok && result.code == VerifyDiagCode::EhResumeTokenMissing && result.failingInstr == &instr)
    {
        out = result.message;
        return false;
    }
    return true;
}

bool ruleEhResumeLabelTarget(const Function &fn, const Instr &instr, std::string &out)
{
    const ResumeLabelResult &result = cachedResumeLabels(fn);
    if (!result.ok && result.failingInstr == &instr)
    {
        out = result.message;
        return false;
    }
    return true;
}

bool ruleDisallowAdd(const Function &, const Instr &instr, std::string &out)
{
    if (instr.op != Opcode::Add)
        return true;
    out = "signed integer add must use iadd.ovf (traps on overflow)";
    return false;
}

bool ruleDisallowSub(const Function &, const Instr &instr, std::string &out)
{
    if (instr.op != Opcode::Sub)
        return true;
    out = "signed integer sub must use isub.ovf (traps on overflow)";
    return false;
}

bool ruleDisallowMul(const Function &, const Instr &instr, std::string &out)
{
    if (instr.op != Opcode::Mul)
        return true;
    out = "signed integer mul must use imul.ovf (traps on overflow)";
    return false;
}

bool ruleDisallowSDiv(const Function &, const Instr &instr, std::string &out)
{
    if (instr.op != Opcode::SDiv)
        return true;
    out = "signed division must use sdiv.chk0 (traps on divide-by-zero and overflow)";
    return false;
}

bool ruleDisallowUDiv(const Function &, const Instr &instr, std::string &out)
{
    if (instr.op != Opcode::UDiv)
        return true;
    out = "unsigned division must use udiv.chk0 (traps on divide-by-zero)";
    return false;
}

bool ruleDisallowSRem(const Function &, const Instr &instr, std::string &out)
{
    if (instr.op != Opcode::SRem)
        return true;
    out = "signed remainder must use srem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)";
    return false;
}

bool ruleDisallowURem(const Function &, const Instr &instr, std::string &out)
{
    if (instr.op != Opcode::URem)
        return true;
    out = "unsigned remainder must use urem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)";
    return false;
}

bool ruleDisallowFptosi(const Function &, const Instr &instr, std::string &out)
{
    if (instr.op != Opcode::Fptosi)
        return true;
    out = "fp to integer narrowing must use cast.fp_to_si.rte.chk (rounds to nearest-even and traps on overflow)";
    return false;
}

} // namespace

const std::vector<Rule> &viper_verifier_rules()
{
    static const std::vector<Rule> kRules = {
        {"eh.stack-underflow", &ruleEhStackUnderflow},
        {"eh.stack-leak", &ruleEhStackLeak},
        {"eh.resume-token", &ruleEhResumeToken},
        {"eh.resume-label-target", &ruleEhResumeLabelTarget},
        {"instr.disallow-add", &ruleDisallowAdd},
        {"instr.disallow-sub", &ruleDisallowSub},
        {"instr.disallow-mul", &ruleDisallowMul},
        {"instr.disallow-sdiv", &ruleDisallowSDiv},
        {"instr.disallow-udiv", &ruleDisallowUDiv},
        {"instr.disallow-srem", &ruleDisallowSRem},
        {"instr.disallow-urem", &ruleDisallowURem},
        {"instr.disallow-fptosi", &ruleDisallowFptosi},
    };
    return kRules;
}

