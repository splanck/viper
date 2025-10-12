//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the analyses that validate exception-handling constructs within IL
// functions.  The helpers verify handler block signatures, ensure that eh.push
// and eh.pop instructions are balanced, and check that resume operations only
// occur when a resume token is active.  The analysis walks the control-flow
// graph without mutating it and reports precise diagnostics when invariants are
// broken.
//
//===----------------------------------------------------------------------===//

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

using HandlerInfo = std::pair<unsigned, unsigned>;

/// @brief Identify whether an opcode terminates the search for EH state.
///
/// The EH analysis needs to stop scanning a block once it reaches a terminator
/// so that successor handling can perform the appropriate stack propagation.
/// This helper centralises the list of terminators that are relevant for
/// exception handling.
///
/// @param op Opcode to classify.
/// @return `true` when @p op ends the block for EH purposes.
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

struct EhState
{
    const BasicBlock *block = nullptr;
    std::vector<const BasicBlock *> handlerStack;
    bool hasResumeToken = false;
    int parent = -1;
};

/// @brief Encode the EH analysis state for visited-set bookkeeping.
///
/// Serialises the handler stack and the resume-token flag into a compact string
/// so the BFS can avoid revisiting states that would not produce new
/// information.
///
/// @param stack Current stack of active handlers (top at the back).
/// @param hasResumeToken Whether the current state holds an active resume token.
/// @return Canonical key string for deduplicating states.
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

/// @brief Resolve the successor blocks referenced by a terminator.
///
/// Looks up the labels attached to a terminator in the provided block map and
/// returns the subset that exist.  Only control-flow opcodes relevant to
/// exception analysis are considered.
///
/// @param terminator Terminator instruction whose successors are required.
/// @param blockMap Mapping from block labels to block pointers.
/// @return Sequence of successor blocks reachable from @p terminator.
std::vector<const BasicBlock *> gatherSuccessors(
    const Instr &terminator, const std::unordered_map<std::string, const BasicBlock *> &blockMap)
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

/// @brief Reconstruct the CFG path that led to a recorded EH state.
///
/// Walks the parent links stored in the BFS state vector to produce a
/// human-readable list of blocks, which is then used to contextualise emitted
/// diagnostics.
///
/// @param states All states accumulated during BFS.
/// @param index Index of the state whose ancestry is required.
/// @return Sequence of blocks from function entry to the chosen state.
std::vector<const BasicBlock *> buildPath(const std::vector<EhState> &states, int index)
{
    std::vector<const BasicBlock *> path;
    for (int cur = index; cur >= 0; cur = states[cur].parent)
        path.push_back(states[cur].block);
    std::reverse(path.begin(), path.end());
    return path;
}

/// @brief Convert a CFG path into a printable arrow-separated string.
///
/// Used by diagnostics to show the path that triggered an invariant violation.
/// Each block label is appended in order with `->` separators.
///
/// @param path Sequence of blocks to serialise.
/// @return String representation of @p path suitable for diagnostics.
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

/// @brief Validate the signature and placement of an exception handler block.
///
/// Checks that the block begins with `eh.entry`, exposes the required
/// `%err:Error` and `%tok:ResumeTok` parameters, and uses the canonical
/// parameter names.  When the block is not a handler the function returns an
/// empty optional, otherwise it returns the identifier pair for the parameters.
///
/// @param fn Function that owns the block (used for diagnostics).
/// @param bb Block under inspection.
/// @return Handler signature when @p bb is a handler, empty optional when not,
///         or an error diagnostic if invariants are violated.
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

/// @brief Ensure eh.push/eh.pop usage forms a balanced stack across the CFG.
///
/// Performs a breadth-first exploration of the function while tracking the
/// active handler stack and whether a resume token is available.  The analysis
/// reports underflows, leaks, and resume instructions without an active token,
/// emitting diagnostic paths that show how the invalid state was reached.
///
/// @param fn Function to analyse.
/// @param blockMap Mapping from block labels to block pointers for successor lookup.
/// @return Success when the EH stack is well behaved, otherwise a diagnostic.
Expected<void> checkEhStackBalance(
    const Function &fn, const std::unordered_map<std::string, const BasicBlock *> &blockMap)
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
