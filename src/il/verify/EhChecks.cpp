//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// File: src/il/verify/EhChecks.cpp
//
// Purpose:
//   Implement reusable exception-handling verification predicates operating on
//   the EhModel abstraction. The checks mirror historical verifier behaviour so
//   diagnostics remain stable while allowing multiple passes to share logic.
//
// Key invariants:
//   * Diagnostics preserve existing wording and codes.
//   * Traversals avoid mutating the underlying IL.
//
// Ownership/Lifetime:
//   All routines borrow IR nodes through EhModel and never assume ownership.
//
// Links:
//   docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/verify/EhChecks.hpp"

#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/DiagSink.hpp"

#include <algorithm>
#include <deque>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::verify
{
namespace
{

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

struct StackState
{
    const BasicBlock *block = nullptr;
    std::vector<const BasicBlock *> handlerStack;
    bool hasResumeToken = false;
    int parent = -1;
    int depth = 0;
};

std::vector<const BasicBlock *> buildPath(const std::vector<StackState> &states, int index)
{
    std::vector<const BasicBlock *> path;
    for (int cur = index; cur >= 0; cur = states[cur].parent)
    {
        if (states[cur].block)
            path.push_back(states[cur].block);
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

il::support::Expected<void> reportEhMismatch(const EhModel &model,
                                             const BasicBlock &bb,
                                             const Instr &instr,
                                             VerifyDiagCode code,
                                             const std::vector<StackState> &states,
                                             int stateIndex,
                                             int depth)
{
    const std::vector<const BasicBlock *> path = buildPath(states, stateIndex);
    std::string suffix;
    switch (code)
    {
        case VerifyDiagCode::EhStackUnderflow:
            suffix = "eh.pop without matching eh.push; path: ";
            suffix += formatPathString(path);
            break;
        case VerifyDiagCode::EhStackLeak:
            suffix = "unmatched eh.push depth ";
            suffix += std::to_string(depth);
            suffix += "; path: ";
            suffix += formatPathString(path);
            break;
        case VerifyDiagCode::EhResumeTokenMissing:
            suffix = "resume.* requires active resume token; path: ";
            suffix += formatPathString(path);
            break;
        default:
            suffix = formatPathString(path);
            break;
    }

    auto message = formatInstrDiag(model.function(), bb, instr, suffix);
    return il::support::Expected<void>{makeVerifierError(code, instr.loc, std::move(message))};
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
    HandlerCoverageTraversal(const EhModel &model, HandlerCoverage &coverage)
        : model(model), coverage(coverage)
    {
    }

    void compute()
    {
        if (!model.entry())
            return;

        State initial;
        initial.block = model.entry();
        enqueueState(std::move(initial));

        std::deque<State> worklist;
        if (!pending.empty())
        {
            worklist.push_back(std::move(pending.front()));
            pending.pop_front();
        }

        while (!worklist.empty())
        {
            State frame = std::move(worklist.front());
            worklist.pop_front();

            const BasicBlock &bb = *frame.block;
            for (const auto &instr : bb.instructions)
            {
                if (const Instr *terminator = processEhInstruction(instr, bb, frame))
                {
                    if (terminator->op == Opcode::Trap || terminator->op == Opcode::TrapFromErr)
                    {
                        handleTrapTerminator(bb, frame, worklist);
                        break;
                    }

                    enqueueSuccessors(*terminator, frame, worklist);
                    break;
                }
            }
        }
    }

  private:
    struct State
    {
        const BasicBlock *block = nullptr;
        std::vector<const BasicBlock *> handlerStack;
        bool hasResumeToken = false;
    };

    const Instr *processEhInstruction(const Instr &instr, const BasicBlock &bb, State &state)
    {
        if (!state.hasResumeToken && !state.handlerStack.empty() &&
            isPotentialFaultingOpcode(instr.op))
        {
            if (const BasicBlock *handlerBlock = state.handlerStack.back())
                coverage[handlerBlock].insert(&bb);
        }

        if (instr.op == Opcode::EhPush)
        {
            const BasicBlock *handlerBlock = nullptr;
            if (!instr.labels.empty())
                handlerBlock = model.findBlock(instr.labels[0]);
            state.handlerStack.push_back(handlerBlock);
        }
        else if (instr.op == Opcode::EhPop)
        {
            if (!state.handlerStack.empty())
                state.handlerStack.pop_back();
        }
        else if (instr.op == Opcode::ResumeSame || instr.op == Opcode::ResumeNext ||
                 instr.op == Opcode::ResumeLabel)
        {
            if (!state.handlerStack.empty())
                state.handlerStack.pop_back();
            state.hasResumeToken = false;
        }

        if (isTerminator(instr.op))
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
        const std::vector<const BasicBlock *> successors = model.gatherSuccessors(terminator);
        for (const BasicBlock *succ : successors)
        {
            State nextState = state;
            nextState.block = succ;
            if (terminator.op == Opcode::ResumeLabel)
                nextState.hasResumeToken = false;
            enqueueState(std::move(nextState), worklist);
        }
    }

    void enqueueState(State state, std::deque<State> &worklist)
    {
        if (!state.block)
            return;

        const std::string key = encodeStateKey(state.handlerStack, state.hasResumeToken);
        if (!visited[state.block].insert(key).second)
            return;

        worklist.push_back(state);
    }

    void enqueueState(State state)
    {
        if (!state.block)
            return;

        const std::string key = encodeStateKey(state.handlerStack, state.hasResumeToken);
        if (!visited[state.block].insert(key).second)
            return;

        pending.push_back(std::move(state));
    }

    const EhModel &model;
    HandlerCoverage &coverage;
    std::unordered_map<const BasicBlock *, std::unordered_set<std::string>> visited;
    std::deque<State> pending;
};

HandlerCoverage computeHandlerCoverage(const EhModel &model)
{
    HandlerCoverage coverage;
    HandlerCoverageTraversal traversal(model, coverage);
    traversal.compute();
    return coverage;
}

struct PostDomInfo
{
    std::unordered_map<const BasicBlock *, size_t> indices;
    std::vector<const BasicBlock *> nodes;
    std::vector<std::vector<uint8_t>> matrix;
};

PostDomInfo computePostDominators(const EhModel &model)
{
    PostDomInfo info;
    if (!model.entry())
        return info;

    const BasicBlock *entry = model.entry();
    std::unordered_set<const BasicBlock *> reachable;
    std::deque<const BasicBlock *> queue;
    queue.push_back(entry);
    reachable.insert(entry);

    while (!queue.empty())
    {
        const BasicBlock *bb = queue.front();
        queue.pop_front();

        if (const Instr *terminator = model.findTerminator(*bb))
        {
            for (const BasicBlock *succ : model.gatherSuccessors(*terminator))
            {
                if (reachable.insert(succ).second)
                    queue.push_back(succ);
            }
        }
    }

    for (const auto &bb : model.function().blocks)
    {
        if (reachable.find(&bb) == reachable.end())
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
        const Instr *terminator = model.findTerminator(*bb);
        if (!terminator)
        {
            std::fill(info.matrix[idx].begin(), info.matrix[idx].end(), 0);
            info.matrix[idx][idx] = 1;
            isExit[idx] = 1;
            continue;
        }

        const std::vector<const BasicBlock *> succBlocks = model.gatherSuccessors(*terminator);
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

} // namespace

il::support::Expected<void> checkEhStackBalance(const EhModel &model)
{
    if (!model.entry())
        return {};

    std::deque<int> worklist;
    std::vector<StackState> states;
    std::unordered_map<const BasicBlock *, std::unordered_set<std::string>> visited;

    StackState initial;
    initial.block = model.entry();
    states.push_back(initial);
    worklist.push_back(0);
    visited[initial.block].insert(encodeStateKey(initial.handlerStack, initial.hasResumeToken));

    while (!worklist.empty())
    {
        const int stateIndex = worklist.front();
        worklist.pop_front();

        const StackState &snapshot = states[stateIndex];
        const BasicBlock &bb = *snapshot.block;
        std::vector<const BasicBlock *> handlerStack = snapshot.handlerStack;
        bool hasResumeToken = snapshot.hasResumeToken;

        const Instr *terminator = nullptr;
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == Opcode::EhPush)
            {
                const BasicBlock *handlerBlock = nullptr;
                if (!instr.labels.empty())
                    handlerBlock = model.findBlock(instr.labels[0]);
                handlerStack.push_back(handlerBlock);
            }
            else if (instr.op == Opcode::EhPop)
            {
                if (handlerStack.empty())
                    return reportEhMismatch(model,
                                             bb,
                                             instr,
                                             VerifyDiagCode::EhStackUnderflow,
                                             states,
                                             stateIndex,
                                             static_cast<int>(handlerStack.size()));

                handlerStack.pop_back();
            }
            else if (instr.op == Opcode::ResumeSame || instr.op == Opcode::ResumeNext ||
                     instr.op == Opcode::ResumeLabel)
            {
                if (!hasResumeToken)
                    return reportEhMismatch(model,
                                             bb,
                                             instr,
                                             VerifyDiagCode::EhResumeTokenMissing,
                                             states,
                                             stateIndex,
                                             static_cast<int>(handlerStack.size()));

                if (!handlerStack.empty())
                    handlerStack.pop_back();
                hasResumeToken = false;
            }

            if (isTerminator(instr.op))
            {
                terminator = &instr;
                break;
            }
        }

        if (!terminator)
            continue;

        states[stateIndex].depth = static_cast<int>(handlerStack.size());
        states[stateIndex].handlerStack = handlerStack;
        states[stateIndex].hasResumeToken = hasResumeToken;

        if (terminator->op == Opcode::Ret && !handlerStack.empty())
        {
            return reportEhMismatch(model,
                                    bb,
                                    *terminator,
                                    VerifyDiagCode::EhStackLeak,
                                    states,
                                    stateIndex,
                                    static_cast<int>(handlerStack.size()));
        }

        if (terminator->op == Opcode::Trap || terminator->op == Opcode::TrapFromErr)
        {
            if (!handlerStack.empty())
            {
                const BasicBlock *handlerBlock = handlerStack.back();
                if (handlerBlock)
                {
                    StackState nextState;
                    nextState.block = handlerBlock;
                    nextState.handlerStack = handlerStack;
                    nextState.hasResumeToken = true;
                    nextState.parent = stateIndex;
                    nextState.depth = static_cast<int>(handlerStack.size());

                    const std::string key =
                        encodeStateKey(nextState.handlerStack, nextState.hasResumeToken);
                    if (visited[handlerBlock].insert(key).second)
                    {
                        const int nextIndex = static_cast<int>(states.size());
                        states.push_back(std::move(nextState));
                        worklist.push_back(nextIndex);
                    }
                }
            }
            continue;
        }

        const std::vector<const BasicBlock *> successors = model.gatherSuccessors(*terminator);
        for (const BasicBlock *succ : successors)
        {
            StackState nextState;
            nextState.block = succ;
            nextState.handlerStack = handlerStack;
            nextState.parent = stateIndex;
            nextState.depth = static_cast<int>(handlerStack.size());
            if (terminator->op == Opcode::ResumeLabel)
                nextState.hasResumeToken = false;
            else
                nextState.hasResumeToken = hasResumeToken;

            const std::string key =
                encodeStateKey(nextState.handlerStack, nextState.hasResumeToken);
            if (!visited[succ].insert(key).second)
                continue;

            const int nextIndex = static_cast<int>(states.size());
            states.push_back(std::move(nextState));
            worklist.push_back(nextIndex);
        }
    }

    return {};
}

il::support::Expected<void> checkDominanceOfHandlers(const EhModel &model)
{
    (void)model;
    return {};
}

il::support::Expected<void> checkUnreachableHandlers(const EhModel &model)
{
    (void)model;
    return {};
}

il::support::Expected<void> checkResumeEdges(const EhModel &model)
{
    const HandlerCoverage coverage = computeHandlerCoverage(model);
    const PostDomInfo postDomInfo = computePostDominators(model);

    for (const auto &bb : model.function().blocks)
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

            const BasicBlock *targetBlock = model.findBlock(instr.labels[0]);
            if (!targetBlock)
                continue;

            for (const BasicBlock *faultingBlock : coverageIt->second)
            {
                const Instr *faultTerminator = model.findTerminator(*faultingBlock);
                if (!faultTerminator)
                    continue;

                const std::vector<const BasicBlock *> faultSuccs =
                    model.gatherSuccessors(*faultTerminator);
                if (faultSuccs.empty())
                    continue;

                if (isPostDominator(postDomInfo, faultingBlock, targetBlock))
                    continue;

                std::string suffix = "target ^";
                suffix += instr.labels[0];
                suffix += " must postdominate block ";
                suffix += faultingBlock->label;

                auto message = formatInstrDiag(model.function(), bb, instr, suffix);
                return il::support::Expected<void>{makeVerifierError(
                    VerifyDiagCode::EhResumeLabelInvalidTarget, instr.loc, std::move(message))};
            }
        }
    }

    return {};
}

} // namespace il::verify

