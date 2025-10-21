//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
// File: src/il/verify/ExceptionHandlerAnalysis.cpp
//
// Summary:
//   Implements verifier helpers that reason about exception-handling blocks,
//   ensuring handlers expose the correct entry signature, EH stack operations
//   balance, and resume instructions observe required invariants.  The
//   utilities traverse functions to validate structured exception flow prior to
//   code generation.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Exception-handling analysis utilities for the IL verifier.
/// @details Provides functions that inspect handler blocks, track EH stack
///          state across control-flow edges, and emit diagnostics when
///          invariants are violated.  These helpers are used during verifier
///          passes to guarantee consistent exception semantics.

#include "il/verify/ExceptionHandlerAnalysis.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/DiagSink.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::verify
{
using il::support::Expected;
using il::support::makeError;

namespace
{

/// @brief Identify terminators relevant for exception-handling analysis.
/// @details Returns @c true when the opcode can transfer control in a way that
///          affects EH state transitions, including branches, traps, resumes,
///          and returns.
/// @param op Opcode being inspected.
/// @return @c true if the opcode should trigger successor exploration.
bool isTerminatorForEh(Opcode op)
{
    switch (op)
    {
        case Opcode::Br:
        case Opcode::CBr:
        case Opcode::SwitchI32:
        case Opcode::Ret:
        case Opcode::Trap:
        case Opcode::TrapFromErr:
        case Opcode::ResumeSame:
        case Opcode::ResumeNext:
        case Opcode::ResumeLabel:
            return true;
        default:
            return false;
    }
}

/// @brief Breadth-first search state describing EH stack context.
/// @details Tracks the current block, the stack of active handlers, whether a
///          resume token is live, and the predecessor state index so paths can
///          be reconstructed for diagnostics.
struct EhState
{
    const BasicBlock *block = nullptr;
    std::vector<const BasicBlock *> handlerStack;
    bool hasResumeToken = false;
    int parent = -1;
};

/// @brief Encode the EH stack state into a string used for visited tracking.
/// @details Generates a compact representation combining the resume-token flag
///          and the ordered list of handler labels.  The key allows the search
///          to detect revisiting equivalent states for the same basic block.
/// @param stack Current stack of handler blocks (top at back).
/// @param hasResumeToken Whether a resume token is active.
/// @return Unique string identifying the EH state for visited-set lookup.
std::string encodeStateKey(const std::vector<const BasicBlock *> &stack, bool hasResumeToken)
{
    std::string key;
    key.reserve(stack.size() * 8 + 4);
    key.append(hasResumeToken ? "1|" : "0|");
    for (const BasicBlock *handler : stack)
    {
        if (handler)
        {
            key.append(handler->label);
        }
        key.push_back(';');
    }
    return key;
}

/// @brief Collect successor blocks from an EH-relevant terminator.
/// @details Looks up branch targets in the provided block map, filtering out
///          missing labels.  Only terminators that explicitly list successors
///          contribute to the returned set.
/// @param terminator Instruction whose successors are required.
/// @param blockMap Lookup table resolving block labels to definitions.
/// @return Vector of successor block pointers.
std::vector<const BasicBlock *> gatherSuccessors(const Instr &terminator,
                                                const std::unordered_map<std::string, const BasicBlock *> &blockMap)
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
            for (size_t idx = 0; idx < terminator.labels.size(); ++idx)
            {
                if (auto it = blockMap.find(terminator.labels[idx]); it != blockMap.end())
                    successors.push_back(it->second);
            }
            break;
        case Opcode::SwitchI32:
            for (size_t idx = 0; idx < terminator.labels.size(); ++idx)
            {
                if (auto it = blockMap.find(terminator.labels[idx]); it != blockMap.end())
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

/// @brief Reconstruct the control-flow path that led to a state index.
/// @details Walks the parent chain from the specified state back to the root
///          and reverses the collected sequence so callers receive the path in
///          forward order.
/// @param states Vector containing all discovered EH states.
/// @param index Index of the state whose ancestry should be materialised.
/// @return Ordered list of basic blocks representing the traversal path.
std::vector<const BasicBlock *> buildPath(const std::vector<EhState> &states, int index)
{
    std::vector<const BasicBlock *> path;
    for (int cur = index; cur >= 0; cur = states[cur].parent)
        path.push_back(states[cur].block);
    std::reverse(path.begin(), path.end());
    return path;
}

/// @brief Format a path of basic blocks for diagnostics.
/// @details Produces a human-readable string of block labels separated by
///          arrows, used when reporting EH stack violations.
/// @param path Sequence of blocks describing a traversal.
/// @return Formatted path string.
std::string formatPathString(const std::vector<const BasicBlock *> &path)
{
    std::ostringstream oss;
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (i != 0)
            oss << " -> ";
        oss << path[i]->label;
    }
    return oss.str();
}

} // namespace

/// @brief Inspect a basic block to determine whether it defines a handler entry.
/// @details Validates that @c eh.entry appears first when present, enforces the
///          required parameter signature, and returns the handler parameter ids
///          used for later verification.  When the block is not a handler the
///          function yields an empty optional.
/// @param fn Function containing the candidate handler block.
/// @param bb Basic block being analysed.
/// @return Optional handler signature wrapped in @ref Expected for diagnostics.
Expected<std::optional<HandlerSignature>> analyzeHandlerBlock(const Function &fn,
                                                              const BasicBlock &bb)
{
    if (bb.instructions.empty())
        return std::optional<HandlerSignature>{};

    const Instr &first = bb.instructions.front();
    if (first.op != Opcode::EhEntry)
    {
        for (size_t idx = 1; idx < bb.instructions.size(); ++idx)
        {
            if (bb.instructions[idx].op == Opcode::EhEntry)
            {
                return Expected<std::optional<HandlerSignature>>{
                    makeError(bb.instructions[idx].loc,
                              formatInstrDiag(
                                  fn,
                                  bb,
                                  bb.instructions[idx],
                                  "eh.entry only allowed as first instruction of handler block"))};
            }
        }
        return std::optional<HandlerSignature>{};
    }

    if (bb.params.size() != 2)
        return Expected<std::optional<HandlerSignature>>{makeError(
            {},
            formatBlockDiag(fn, bb, "handler blocks must declare (%err:Error, %tok:ResumeTok)"))};

    if (bb.params[0].type.kind != Type::Kind::Error ||
        bb.params[1].type.kind != Type::Kind::ResumeTok)
        return Expected<std::optional<HandlerSignature>>{makeError(
            {}, formatBlockDiag(fn, bb, "handler params must be (%err:Error, %tok:ResumeTok)"))};

    if (bb.params[0].name != "err" || bb.params[1].name != "tok")
        return Expected<std::optional<HandlerSignature>>{
            makeError({}, formatBlockDiag(fn, bb, "handler params must be named %err and %tok"))};

    HandlerSignature sig = {bb.params[0].id, bb.params[1].id};
    return std::optional<HandlerSignature>{sig};
}

/// @brief Ensure EH push/pop operations and resume instructions remain balanced.
/// @details Performs a breadth-first traversal over the function's control flow,
///          tracking active handler stacks and resume tokens.  Reports
///          diagnostics when the stack underflows, leaks, or when resume
///          instructions appear without an active token.
/// @param fn Function whose EH structure is being validated.
/// @param blockMap Map used to resolve block labels during traversal.
/// @return Empty success or a structured diagnostic on failure.
Expected<void> checkEhStackBalance(const Function &fn,
                                   const std::unordered_map<std::string, const BasicBlock *> &blockMap)
{
    if (fn.blocks.empty())
        return {};

    std::deque<int> worklist;
    std::vector<EhState> states;
    std::unordered_map<const BasicBlock *, std::unordered_set<std::string>> visited;

    states.push_back({&fn.blocks.front(), {}, false, -1});
    worklist.push_back(0);
    visited[&fn.blocks.front()].insert(
        encodeStateKey(states.front().handlerStack, states.front().hasResumeToken));

    while (!worklist.empty())
    {
        const int stateIndex = worklist.front();
        worklist.pop_front();

        const EhState &state = states[stateIndex];
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
                    std::vector<const BasicBlock *> path = buildPath(states, stateIndex);
                    std::string message =
                        formatInstrDiag(fn,
                                        bb,
                                        instr,
                                        std::string("eh.pop without matching eh.push; path: ") +
                                            formatPathString(path));
                    return Expected<void>{makeVerifierError(
                        VerifyDiagCode::EhStackUnderflow, instr.loc, message)};
                }
                handlerStack.pop_back();
            }

            if (instr.op == Opcode::ResumeSame || instr.op == Opcode::ResumeNext ||
                instr.op == Opcode::ResumeLabel)
            {
                if (!hasResumeToken)
                {
                    std::vector<const BasicBlock *> path = buildPath(states, stateIndex);
                    std::string message = formatInstrDiag(
                        fn,
                        bb,
                        instr,
                        std::string("resume.* requires active resume token; path: ") +
                            formatPathString(path));
                    return Expected<void>{makeVerifierError(
                        VerifyDiagCode::EhResumeTokenMissing, instr.loc, message)};
                }
                if (!handlerStack.empty())
                    handlerStack.pop_back();
                hasResumeToken = false;
            }

            if (isTerminatorForEh(instr.op))
            {
                terminator = &instr;
                break;
            }
        }

        if (!terminator)
            continue;

        const int depth = static_cast<int>(handlerStack.size());

        if (terminator->op == Opcode::Ret && depth != 0)
        {
            std::vector<const BasicBlock *> path = buildPath(states, stateIndex);
            std::string message =
                formatInstrDiag(fn,
                                bb,
                                *terminator,
                                std::string("unmatched eh.push depth ") + std::to_string(depth) +
                                    "; path: " + formatPathString(path));
            return Expected<void>{makeVerifierError(
                VerifyDiagCode::EhStackLeak, terminator->loc, message)};
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

        const std::vector<const BasicBlock *> successors = gatherSuccessors(*terminator, blockMap);
        for (const BasicBlock *succ : successors)
        {
            EhState nextState;
            nextState.block = succ;
            nextState.handlerStack = handlerStack;
            nextState.parent = stateIndex;
            if (terminator->op == Opcode::ResumeLabel)
            {
                nextState.hasResumeToken = false;
            }
            else
            {
                nextState.hasResumeToken = hasResumeToken;
            }

            const std::string key = encodeStateKey(nextState.handlerStack, nextState.hasResumeToken);
            if (!visited[succ].insert(key).second)
                continue;
            const int nextIndex = static_cast<int>(states.size());
            states.push_back(std::move(nextState));
            worklist.push_back(nextIndex);
        }
    }

    return {};
}

} // namespace il::verify
