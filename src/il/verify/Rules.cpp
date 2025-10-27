//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/Rules.cpp
//
// Summary:
//   Implements the rule registry consumed by the IL verifier.  Each rule wraps
//   a focussed predicate that examines an instruction (or the surrounding
//   function) and returns a structured diagnostic message on failure.  The
//   registry keeps the verifier drivers lightweight by centralising all
//   predicates and their associated error formatting.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Rule registry implementation for IL verification.
/// @details Provides predicate helpers for both EH verification and general
///          instruction signature checks.  Rule failures encode location
///          metadata so callers can surface uniform diagnostics.

#include "il/verify/Rules.hpp"

#include "il/verify/ControlFlowChecker.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
using il::core::BasicBlock;
using il::core::Function;
using il::core::Instr;
using il::core::Opcode;
using il::core::getOpcodeInfo;

struct Location
{
    int block = -1;
    int instr = -1;
};

struct EncodedMessage
{
    std::string message;
    Location location;
};

std::string encodeMessage(const EncodedMessage &payload)
{
    std::ostringstream oss;
    oss << payload.location.block << kRuleMessageSep << payload.location.instr << kRuleMessageSep
        << payload.message;
    return oss.str();
}

std::optional<Location> locate(const Function &fn, const BasicBlock &bb, const Instr &instr)
{
    for (size_t blockIndex = 0; blockIndex < fn.blocks.size(); ++blockIndex)
    {
        if (&fn.blocks[blockIndex] != &bb)
            continue;
        for (size_t instrIndex = 0; instrIndex < bb.instructions.size(); ++instrIndex)
        {
            if (&bb.instructions[instrIndex] == &instr)
                return Location{static_cast<int>(blockIndex), static_cast<int>(instrIndex)};
        }
    }
    return std::nullopt;
}

std::optional<Location> locate(const Function &fn, const Instr &needle)
{
    for (size_t blockIndex = 0; blockIndex < fn.blocks.size(); ++blockIndex)
    {
        const BasicBlock &bb = fn.blocks[blockIndex];
        for (size_t instrIndex = 0; instrIndex < bb.instructions.size(); ++instrIndex)
        {
            if (&bb.instructions[instrIndex] == &needle)
                return Location{static_cast<int>(blockIndex), static_cast<int>(instrIndex)};
        }
    }
    return std::nullopt;
}

//===----------------------------------------------------------------------===//
// EH verification helpers.
//===----------------------------------------------------------------------===//

struct EhState
{
    const BasicBlock *block = nullptr;
    std::vector<const BasicBlock *> handlerStack;
    bool hasResumeToken = false;
    int parent = -1;
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

std::vector<const BasicBlock *> gatherSuccessors(const Instr &terminator,
                                                 const std::unordered_map<std::string, const BasicBlock *> &blockMap)
{
    std::vector<const BasicBlock *> successors;
    switch (terminator.op)
    {
        case Opcode::Br:
        case Opcode::ResumeLabel:
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
        default:
            break;
    }
    return successors;
}

std::vector<const BasicBlock *> buildPath(const std::vector<EhState> &states, int index)
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

std::unordered_map<std::string, const BasicBlock *> buildCfg(const Function &fn)
{
    std::unordered_map<std::string, const BasicBlock *> cfg;
    cfg.reserve(fn.blocks.size());
    for (const auto &bb : fn.blocks)
        cfg[bb.label] = &bb;
    return cfg;
}

enum class EhFailureKind
{
    None,
    StackUnderflow,
    StackLeak,
    ResumeTokenMissing
};

struct EhFailure
{
    EhFailureKind kind = EhFailureKind::None;
    const BasicBlock *block = nullptr;
    const Instr *instr = nullptr;
    std::vector<const BasicBlock *> path;
    int depth = 0;
};

EhFailure runEhStackAnalysis(const Function &fn)
{
    EhFailure failure;
    if (fn.blocks.empty())
        return failure;

    std::unordered_map<std::string, const BasicBlock *> blockMap = buildCfg(fn);
    std::deque<int> worklist;
    std::vector<EhState> states;
    std::unordered_map<const BasicBlock *, std::unordered_set<std::string>> visited;

    EhState entry;
    entry.block = &fn.blocks.front();
    entry.parent = -1;
    states.push_back(entry);
    worklist.push_back(0);
    visited[entry.block].insert(encodeStateKey(entry.handlerStack, entry.hasResumeToken));

    while (!worklist.empty())
    {
        const int stateIndex = worklist.front();
        worklist.pop_front();

        EhState state = states[stateIndex];
        const BasicBlock &bb = *state.block;

        std::vector<const BasicBlock *> handlerStack = state.handlerStack;
        bool hasResumeToken = state.hasResumeToken;

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
            }
            else if (instr.op == Opcode::EhPop)
            {
                if (handlerStack.empty())
                {
                    failure.kind = EhFailureKind::StackUnderflow;
                    failure.block = &bb;
                    failure.instr = &instr;
                    failure.path = buildPath(states, stateIndex);
                    return failure;
                }
                handlerStack.pop_back();
            }

            if (instr.op == Opcode::ResumeSame || instr.op == Opcode::ResumeNext ||
                instr.op == Opcode::ResumeLabel)
            {
                if (!hasResumeToken)
                {
                    failure.kind = EhFailureKind::ResumeTokenMissing;
                    failure.block = &bb;
                    failure.instr = &instr;
                    failure.path = buildPath(states, stateIndex);
                    return failure;
                }
                if (!handlerStack.empty())
                    handlerStack.pop_back();
                hasResumeToken = false;
            }

            if (il::verify::isTerminator(instr.op))
            {
                terminator = &instr;
                break;
            }
        }

        if (!terminator)
            continue;

        if (terminator->op == Opcode::Ret && !handlerStack.empty())
        {
            failure.kind = EhFailureKind::StackLeak;
            failure.block = &bb;
            failure.instr = terminator;
            failure.path = buildPath(states, stateIndex);
            failure.depth = static_cast<int>(handlerStack.size());
            return failure;
        }

        if (terminator->op == Opcode::Trap || terminator->op == Opcode::TrapFromErr)
        {
            if (!handlerStack.empty())
            {
                const BasicBlock *handlerBlock = handlerStack.back();
                if (handlerBlock)
                {
                    EhState nextState;
                    nextState.block = handlerBlock;
                    nextState.handlerStack = handlerStack;
                    nextState.hasResumeToken = true;
                    nextState.parent = stateIndex;
                    const std::string key = encodeStateKey(nextState.handlerStack, nextState.hasResumeToken);
                    if (visited[handlerBlock].insert(key).second)
                    {
                        const int nextIndex = static_cast<int>(states.size());
                        states.push_back(nextState);
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
            nextState.block = succ;
            nextState.handlerStack = handlerStack;
            nextState.parent = stateIndex;
            if (terminator->op == Opcode::ResumeLabel)
                nextState.hasResumeToken = false;
            else
                nextState.hasResumeToken = hasResumeToken;
            const std::string key = encodeStateKey(nextState.handlerStack, nextState.hasResumeToken);
            if (visited[succ].insert(key).second)
            {
                const int nextIndex = static_cast<int>(states.size());
                states.push_back(nextState);
                worklist.push_back(nextIndex);
            }
        }
    }

    return failure;
}

bool encodeEhFailureMessage(const Function &fn, const EhFailure &failure, std::string &out_msg)
{
    Location loc{0, 0};
    if (failure.block && failure.instr)
    {
        if (auto found = locate(fn, *failure.block, *failure.instr))
            loc = *found;
    }

    std::string detail;
    switch (failure.kind)
    {
        case EhFailureKind::StackUnderflow:
            detail = "eh.pop without matching eh.push; path: " + formatPathString(failure.path);
            break;
        case EhFailureKind::StackLeak:
            detail = "unmatched eh.push depth " + std::to_string(failure.depth) +
                     "; path: " + formatPathString(failure.path);
            break;
        case EhFailureKind::ResumeTokenMissing:
            detail = "resume.* requires active resume token; path: " + formatPathString(failure.path);
            break;
        default:
            return false;
    }

    out_msg = encodeMessage({detail, loc});
    return true;
}

struct HandlerCoverageTraversal
{
    struct State
    {
        const BasicBlock *block = nullptr;
        std::vector<const BasicBlock *> handlerStack;
        bool hasResumeToken = false;
    };

    HandlerCoverageTraversal(const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                             std::unordered_map<const BasicBlock *, std::unordered_set<const BasicBlock *>> &coverage)
        : blockMap(blockMap), coverage(coverage)
    {
    }

    void compute(const Function &fn)
    {
        if (fn.blocks.empty())
            return;

        std::deque<State> worklist;
        State entry;
        entry.block = &fn.blocks.front();
        worklist.push_back(entry);

        while (!worklist.empty())
        {
            State state = std::move(worklist.front());
            worklist.pop_front();

            const BasicBlock &bb = *state.block;
            State frame = state;

            const Instr *terminator = nullptr;
            for (const auto &instr : bb.instructions)
            {
                terminator = processInstr(instr, bb, frame);
                if (terminator)
                    break;
            }

            if (!terminator)
                continue;

            if (terminator->op == Opcode::Trap || terminator->op == Opcode::TrapFromErr)
            {
                handleTrap(bb, frame, worklist);
                continue;
            }

            enqueueSuccessors(*terminator, frame, worklist);
        }
    }

  private:
    const Instr *processInstr(const Instr &instr, const BasicBlock &bb, State &state)
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
        else if (instr.op == Opcode::ResumeSame || instr.op == Opcode::ResumeNext ||
                 instr.op == Opcode::ResumeLabel)
        {
            if (!state.handlerStack.empty())
                state.handlerStack.pop_back();
            state.hasResumeToken = false;
        }

        if (il::verify::isTerminator(instr.op))
            return &instr;
        return nullptr;
    }

    void handleTrap(const BasicBlock &bb, const State &state, std::deque<State> &worklist)
    {
        if (state.handlerStack.empty())
            return;
        const BasicBlock *handlerBlock = state.handlerStack.back();
        if (!handlerBlock)
            return;

        State next = state;
        next.block = handlerBlock;
        next.hasResumeToken = true;
        worklist.push_back(next);
    }

    void enqueueSuccessors(const Instr &terminator, const State &state, std::deque<State> &worklist)
    {
        const std::vector<const BasicBlock *> successors = gatherSuccessors(terminator, blockMap);
        for (const BasicBlock *succ : successors)
        {
            State next = state;
            next.block = succ;
            if (terminator.op == Opcode::ResumeLabel)
            {
                if (!next.handlerStack.empty())
                    next.handlerStack.pop_back();
                next.hasResumeToken = true;
            }
            worklist.push_back(next);
        }
    }

    static bool isPotentialFaultingOpcode(Opcode op)
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

    const std::unordered_map<std::string, const BasicBlock *> &blockMap;
    std::unordered_map<const BasicBlock *, std::unordered_set<const BasicBlock *>> &coverage;
};

using HandlerCoverage = std::unordered_map<const BasicBlock *, std::unordered_set<const BasicBlock *>>;

struct PostDomInfo
{
    std::unordered_map<const BasicBlock *, std::unordered_set<const BasicBlock *>> postDomSets;
};

PostDomInfo computePostDominators(const Function &fn,
                                  const std::unordered_map<std::string, const BasicBlock *> &blockMap)
{
    (void)blockMap;
    PostDomInfo info;
    std::vector<const BasicBlock *> blocks;
    blocks.reserve(fn.blocks.size());
    for (const auto &bb : fn.blocks)
        blocks.push_back(&bb);

    if (blocks.empty())
        return info;

    std::unordered_map<const BasicBlock *, std::unordered_set<const BasicBlock *>> postDomSets;
    for (const BasicBlock *bb : blocks)
    {
        std::unordered_set<const BasicBlock *> set(blocks.begin(), blocks.end());
        postDomSets[bb] = std::move(set);
    }

    const BasicBlock *exitBlock = blocks.back();
    postDomSets[exitBlock] = {exitBlock};

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const BasicBlock *bb : blocks)
        {
            if (bb == exitBlock)
                continue;

            std::unordered_set<const BasicBlock *> intersection;
            bool first = true;
            if (!bb->instructions.empty())
            {
                const Instr &terminator = bb->instructions.back();
                const std::vector<const BasicBlock *> successors = gatherSuccessors(terminator, blockMap);
                for (const BasicBlock *succ : successors)
                {
                    const auto &succSet = postDomSets[succ];
                    if (first)
                    {
                        intersection = succSet;
                        first = false;
                    }
                    else
                    {
                        std::unordered_set<const BasicBlock *> tmp;
                        for (const BasicBlock *candidate : intersection)
                        {
                            if (succSet.count(candidate))
                                tmp.insert(candidate);
                        }
                        intersection.swap(tmp);
                    }
                }
            }
            if (first)
                intersection.clear();

            std::unordered_set<const BasicBlock *> newSet = intersection;
            newSet.insert(bb);
            if (newSet != postDomSets[bb])
            {
                postDomSets[bb] = std::move(newSet);
                changed = true;
            }
        }
    }

    info.postDomSets = std::move(postDomSets);
    return info;
}

bool isPostDominator(const PostDomInfo &info, const BasicBlock *bb, const BasicBlock *candidate)
{
    const auto it = info.postDomSets.find(bb);
    if (it == info.postDomSets.end())
        return false;
    return it->second.count(candidate) != 0;
}

std::optional<EncodedMessage> checkResumeTargets(const Function &fn)
{
    std::unordered_map<std::string, const BasicBlock *> blockMap = buildCfg(fn);
    HandlerCoverage coverage;
    HandlerCoverageTraversal traversal(blockMap, coverage);
    traversal.compute(fn);
    const PostDomInfo postDomInfo = computePostDominators(fn, blockMap);

    for (const auto &bb : fn.blocks)
    {
        const auto coverageIt = coverage.find(&bb);
        if (coverageIt == coverage.end())
            continue;

        for (const auto &instr : bb.instructions)
        {
            if (instr.op != Opcode::ResumeLabel || instr.labels.empty())
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
                const std::vector<const BasicBlock *> successors = gatherSuccessors(*faultTerminator, blockMap);
                if (successors.empty())
                    continue;
                if (isPostDominator(postDomInfo, faultingBlock, targetBlock))
                    continue;

                auto loc = locate(fn, bb, instr);
                if (!loc)
                    continue;
                EncodedMessage payload;
                payload.location = *loc;
                payload.message = "target ^" + instr.labels[0] + " must postdominate block " +
                                  faultingBlock->label;
                return payload;
            }
        }
    }

    return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Instruction signature rules.
//===----------------------------------------------------------------------===//

std::optional<EncodedMessage> checkUnexpectedResult(const Function &fn, const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);
    if (info.resultArity == il::core::ResultArity::None && instr.result.has_value())
    {
        auto loc = locate(fn, instr);
        if (!loc)
            return std::nullopt;
        EncodedMessage payload;
        payload.location = *loc;
        payload.message = "unexpected result";
        return payload;
    }
    return std::nullopt;
}

std::optional<EncodedMessage> checkMissingResult(const Function &fn, const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);
    if (info.resultArity == il::core::ResultArity::One && !instr.result.has_value())
    {
        auto loc = locate(fn, instr);
        if (!loc)
            return std::nullopt;
        EncodedMessage payload;
        payload.location = *loc;
        payload.message = "missing result";
        return payload;
    }
    return std::nullopt;
}

std::optional<EncodedMessage> checkOperandCount(const Function &fn, const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);
    const size_t operandCount = instr.operands.size();
    const bool variadic = il::core::isVariadicOperandCount(info.numOperandsMax);
    if (operandCount >= info.numOperandsMin && (variadic || operandCount <= info.numOperandsMax))
        return std::nullopt;

    auto loc = locate(fn, instr);
    if (!loc)
        return std::nullopt;

    std::string message;
    if (info.numOperandsMin == info.numOperandsMax)
    {
        message = "expected " +
                  std::to_string(static_cast<unsigned>(info.numOperandsMin)) + " operand";
        if (info.numOperandsMin != 1)
            message += 's';
    }
    else if (variadic)
    {
        message = "expected at least " +
                  std::to_string(static_cast<unsigned>(info.numOperandsMin)) + " operand";
        if (info.numOperandsMin != 1)
            message += 's';
    }
    else
    {
        message = "expected between " +
                  std::to_string(static_cast<unsigned>(info.numOperandsMin)) + " and " +
                  std::to_string(static_cast<unsigned>(info.numOperandsMax)) + " operands";
    }

    EncodedMessage payload;
    payload.location = *loc;
    payload.message = std::move(message);
    return payload;
}

std::optional<EncodedMessage> checkSuccessorMinimum(const Function &fn, const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);
    if (!il::core::isVariadicSuccessorCount(info.numSuccessors))
        return std::nullopt;
    if (!instr.labels.empty())
        return std::nullopt;

    auto loc = locate(fn, instr);
    if (!loc)
        return std::nullopt;

    EncodedMessage payload;
    payload.location = *loc;
    payload.message = "expected at least 1 successor";
    return payload;
}

std::optional<EncodedMessage> checkSuccessorExact(const Function &fn, const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);
    if (il::core::isVariadicSuccessorCount(info.numSuccessors))
        return std::nullopt;
    if (instr.labels.size() == info.numSuccessors)
        return std::nullopt;

    auto loc = locate(fn, instr);
    if (!loc)
        return std::nullopt;

    std::string message = "expected " +
                          std::to_string(static_cast<unsigned>(info.numSuccessors)) + " successor";
    if (info.numSuccessors != 1)
        message += 's';

    EncodedMessage payload;
    payload.location = *loc;
    payload.message = std::move(message);
    return payload;
}

std::optional<EncodedMessage> checkBranchArgsVariadic(const Function &fn, const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);
    if (!il::core::isVariadicSuccessorCount(info.numSuccessors))
        return std::nullopt;
    if (instr.brArgs.empty() || instr.brArgs.size() == instr.labels.size())
        return std::nullopt;

    auto loc = locate(fn, instr);
    if (!loc)
        return std::nullopt;

    EncodedMessage payload;
    payload.location = *loc;
    payload.message = "expected branch argument bundle per successor or none";
    return payload;
}

std::optional<EncodedMessage> checkBranchArgsMax(const Function &fn, const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);
    if (il::core::isVariadicSuccessorCount(info.numSuccessors))
        return std::nullopt;
    if (instr.brArgs.size() <= info.numSuccessors)
        return std::nullopt;

    auto loc = locate(fn, instr);
    if (!loc)
        return std::nullopt;

    std::string message = "expected at most " +
                          std::to_string(static_cast<unsigned>(info.numSuccessors)) +
                          " branch argument bundle";
    if (info.numSuccessors != 1)
        message += 's';

    EncodedMessage payload;
    payload.location = *loc;
    payload.message = std::move(message);
    return payload;
}

std::optional<EncodedMessage> checkBranchArgsExactOrNone(const Function &fn, const Instr &instr)
{
    const auto &info = getOpcodeInfo(instr.op);
    if (il::core::isVariadicSuccessorCount(info.numSuccessors))
        return std::nullopt;
    if (instr.brArgs.empty() || instr.brArgs.size() == info.numSuccessors)
        return std::nullopt;

    auto loc = locate(fn, instr);
    if (!loc)
        return std::nullopt;

    std::string message = "expected " +
                          std::to_string(static_cast<unsigned>(info.numSuccessors)) +
                          " branch argument bundle";
    if (info.numSuccessors != 1)
        message += 's';
    message += ", or none";

    EncodedMessage payload;
    payload.location = *loc;
    payload.message = std::move(message);
    return payload;
}

//===----------------------------------------------------------------------===//
// Rule entry points.
//===----------------------------------------------------------------------===//

bool rule_eh_stack_underflow(const Function &fn, const Instr &instr, std::string &out_msg)
{
    (void)instr;
    const EhFailure failure = runEhStackAnalysis(fn);
    if (failure.kind != EhFailureKind::StackUnderflow)
        return true;
    if (!encodeEhFailureMessage(fn, failure, out_msg))
        out_msg.clear();
    return false;
}

bool rule_eh_stack_leak(const Function &fn, const Instr &instr, std::string &out_msg)
{
    (void)instr;
    const EhFailure failure = runEhStackAnalysis(fn);
    if (failure.kind != EhFailureKind::StackLeak)
        return true;
    if (!encodeEhFailureMessage(fn, failure, out_msg))
        out_msg.clear();
    return false;
}

bool rule_eh_resume_token(const Function &fn, const Instr &instr, std::string &out_msg)
{
    (void)instr;
    const EhFailure failure = runEhStackAnalysis(fn);
    if (failure.kind != EhFailureKind::ResumeTokenMissing)
        return true;
    if (!encodeEhFailureMessage(fn, failure, out_msg))
        out_msg.clear();
    return false;
}

bool rule_eh_resume_label(const Function &fn, const Instr &instr, std::string &out_msg)
{
    (void)instr;
    const auto payload = checkResumeTargets(fn);
    if (!payload)
        return true;
    out_msg = encodeMessage(*payload);
    return false;
}

bool rule_sig_unexpected_result(const Function &fn, const Instr &instr, std::string &out_msg)
{
    const auto payload = checkUnexpectedResult(fn, instr);
    if (!payload)
        return true;
    out_msg = encodeMessage(*payload);
    return false;
}

bool rule_sig_missing_result(const Function &fn, const Instr &instr, std::string &out_msg)
{
    const auto payload = checkMissingResult(fn, instr);
    if (!payload)
        return true;
    out_msg = encodeMessage(*payload);
    return false;
}

bool rule_sig_operand_count(const Function &fn, const Instr &instr, std::string &out_msg)
{
    const auto payload = checkOperandCount(fn, instr);
    if (!payload)
        return true;
    out_msg = encodeMessage(*payload);
    return false;
}

bool rule_sig_successor_minimum(const Function &fn, const Instr &instr, std::string &out_msg)
{
    const auto payload = checkSuccessorMinimum(fn, instr);
    if (!payload)
        return true;
    out_msg = encodeMessage(*payload);
    return false;
}

bool rule_sig_successor_exact(const Function &fn, const Instr &instr, std::string &out_msg)
{
    const auto payload = checkSuccessorExact(fn, instr);
    if (!payload)
        return true;
    out_msg = encodeMessage(*payload);
    return false;
}

bool rule_sig_branch_args_variadic(const Function &fn, const Instr &instr, std::string &out_msg)
{
    const auto payload = checkBranchArgsVariadic(fn, instr);
    if (!payload)
        return true;
    out_msg = encodeMessage(*payload);
    return false;
}

bool rule_sig_branch_args_max(const Function &fn, const Instr &instr, std::string &out_msg)
{
    const auto payload = checkBranchArgsMax(fn, instr);
    if (!payload)
        return true;
    out_msg = encodeMessage(*payload);
    return false;
}

bool rule_sig_branch_args_exact(const Function &fn, const Instr &instr, std::string &out_msg)
{
    const auto payload = checkBranchArgsExactOrNone(fn, instr);
    if (!payload)
        return true;
    out_msg = encodeMessage(*payload);
    return false;
}

const std::vector<Rule> &buildRegistry()
{
    static const std::vector<Rule> rules = {
        {"eh.stack.underflow", &rule_eh_stack_underflow},
        {"eh.stack.leak", &rule_eh_stack_leak},
        {"eh.resume.token", &rule_eh_resume_token},
        {"eh.resume.label.dominates", &rule_eh_resume_label},
        {"sig.unexpected-result", &rule_sig_unexpected_result},
        {"sig.missing-result", &rule_sig_missing_result},
        {"sig.operand-count", &rule_sig_operand_count},
        {"sig.successor-min", &rule_sig_successor_minimum},
        {"sig.successor-exact", &rule_sig_successor_exact},
        {"sig.branch-args-variadic", &rule_sig_branch_args_variadic},
        {"sig.branch-args-max", &rule_sig_branch_args_max},
        {"sig.branch-args-exact", &rule_sig_branch_args_exact},
    };
    return rules;
}

} // namespace

const std::vector<Rule> &viper_verifier_rules()
{
    return buildRegistry();
}

