// File: src/il/verify/EhVerifier.cpp
// License: MIT (see LICENSE for details).
// Purpose: Implements the verifier pass that validates EH stack balance per function.
// Key invariants: Control-flow is explored to ensure every execution path maintains
// balanced eh.push/eh.pop pairs, delegating to ExceptionHandlerAnalysis helpers.
// Ownership/Lifetime: Operates on caller-owned modules; no allocations outlive
// verification. Diagnostics are returned via Expected or forwarded through sinks.
// Links: docs/il-guide.md#reference

#include "il/verify/EhVerifier.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/ExceptionHandlerAnalysis.hpp"

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::verify
{
using il::support::Expected;

namespace
{

/// @brief Encode a handler stack and resume-token flag into a memoisation key.
/// @details Concatenates the resume-token bit and block labels separated by
/// semicolons to build a unique string for the current exploration state. The
/// key is used to avoid revisiting identical EH configurations during graph
/// traversal.
/// @param stack Current handler stack (top at the back).
/// @param hasResumeToken Whether the in-flight exception owns a resume token.
/// @return Deterministic key encoding the state.
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

/// @brief Locate the first terminator instruction within a block.
/// @details Scans the block's instruction list and returns a pointer to the
/// first opcode that transfers control. If no terminator exists the function
/// returns @c nullptr so callers can handle fall-through cases explicitly.
/// @param bb Block whose terminator should be located.
/// @return Pointer to the terminator or nullptr when absent.
const Instr *findTerminator(const BasicBlock &bb)
{
    for (const auto &instr : bb.instructions)
    {
        if (isTerminator(instr.op))
            return &instr;
    }
    return nullptr;
}

/// @brief Resolve successor blocks referenced by a terminator instruction.
/// @details Uses @p blockMap to translate successor labels into block pointers
/// for @c br, @c cbr, @c switch, and resume-label terminators. Unknown labels
/// are silently skipped so the verifier can surface a dedicated diagnostic
/// later.
/// @param terminator Instruction that ends a block.
/// @param blockMap Mapping from labels to block pointers for the owning function.
/// @return Vector containing each successor reachable from the terminator.
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

/// @brief Determine whether executing @p op could fault and trigger handlers.
/// @details Returns false for opcodes that either manipulate the EH stack or
/// serve as terminators; all other operations are conservatively treated as
/// potentially faulting so coverage includes their containing block.
/// @param op Opcode to classify.
/// @return True when @p op may fault.
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

using HandlerCoverage = std::unordered_map<const BasicBlock *, std::unordered_set<const BasicBlock *>>;

/// @brief Map each handler to the blocks it protects within a function.
/// @details Performs a worklist traversal over the function's CFG, tracking the
/// active handler stack. Whenever a potentially faulting instruction executes
/// under a handler, the enclosing block is recorded in the handler's coverage
/// set. Resume operations pop the stack to model stack unwinding precisely.
/// @param fn Function whose handlers are analysed.
/// @param blockMap Mapping from labels to block pointers.
/// @return Coverage table keyed by handler block pointer.
HandlerCoverage computeHandlerCoverage(
    const Function &fn, const std::unordered_map<std::string, const BasicBlock *> &blockMap)
{
    HandlerCoverage coverage;
    if (fn.blocks.empty())
        return coverage;

    struct State
    {
        const BasicBlock *block = nullptr;
        std::vector<const BasicBlock *> handlerStack;
        bool hasResumeToken = false;
    };

    std::deque<State> worklist;
    std::unordered_map<const BasicBlock *, std::unordered_set<std::string>> visited;

    State entryState;
    entryState.block = &fn.blocks.front();
    worklist.push_back(entryState);
    visited[entryState.block].insert(encodeStateKey(entryState.handlerStack, entryState.hasResumeToken));

    while (!worklist.empty())
    {
        State state = worklist.front();
        worklist.pop_front();

        const BasicBlock &bb = *state.block;
        std::vector<const BasicBlock *> handlerStack = state.handlerStack;
        bool hasResumeToken = state.hasResumeToken;

        const Instr *terminator = nullptr;
        for (const auto &instr : bb.instructions)
        {
            if (!hasResumeToken && !handlerStack.empty() && isPotentialFaultingOpcode(instr.op))
            {
                const BasicBlock *handlerBlock = handlerStack.back();
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
                handlerStack.push_back(handlerBlock);
            }
            else if (instr.op == Opcode::EhPop)
            {
                if (!handlerStack.empty())
                    handlerStack.pop_back();
            }
            else if (instr.op == Opcode::ResumeSame || instr.op == Opcode::ResumeNext ||
                     instr.op == Opcode::ResumeLabel)
            {
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

        if (terminator->op == Opcode::Trap || terminator->op == Opcode::TrapFromErr)
        {
            if (!handlerStack.empty())
            {
                const BasicBlock *handlerBlock = handlerStack.back();
                if (handlerBlock)
                {
                    coverage[handlerBlock].insert(&bb);

                    State nextState;
                    nextState.block = handlerBlock;
                    nextState.handlerStack = handlerStack;
                    nextState.hasResumeToken = true;
                    const std::string key =
                        encodeStateKey(nextState.handlerStack, nextState.hasResumeToken);
                    if (visited[handlerBlock].insert(key).second)
                        worklist.push_back(std::move(nextState));
                }
            }
            continue;
        }

        const std::vector<const BasicBlock *> successors = gatherSuccessors(*terminator, blockMap);
        for (const BasicBlock *succ : successors)
        {
            State nextState;
            nextState.block = succ;
            nextState.handlerStack = handlerStack;
            nextState.hasResumeToken =
                terminator->op == Opcode::ResumeLabel ? false : hasResumeToken;

            const std::string key =
                encodeStateKey(nextState.handlerStack, nextState.hasResumeToken);
            if (!visited[succ].insert(key).second)
                continue;
            worklist.push_back(std::move(nextState));
        }
    }

    return coverage;
}

struct PostDomInfo
{
    std::unordered_map<const BasicBlock *, size_t> indices;
    std::vector<const BasicBlock *> nodes;
    std::vector<std::vector<uint8_t>> matrix;
};

/// @brief Compute a simple post-dominator relation for reachable blocks.
/// @details Restricts the graph to reachable nodes, assigns each an index, and
/// iteratively computes a boolean post-dominator matrix via reverse CFG
/// traversal. Exit blocks form the base cases and each iteration intersects
/// successor sets until convergence.
/// @param fn Function being analysed.
/// @param blockMap Mapping from labels to block pointers.
/// @return Dense post-dominator summary including index tables.
PostDomInfo computePostDominators(
    const Function &fn, const std::unordered_map<std::string, const BasicBlock *> &blockMap)
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

    for (const auto &bb : fn.blocks)
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

/// @brief Query whether @p candidate post-dominates @p from in @p info.
/// @details Translates the block pointers into matrix indices and reads the
/// boolean relation computed by @ref computePostDominators.
/// @param info Post-dominator summary containing the relation matrix.
/// @param from Source block to test.
/// @param candidate Potential post-dominator block.
/// @return True when @p candidate post-dominates @p from.
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

/// @brief Ensure resume-label targets are valid handlers for the covered blocks.
/// @details Reuses handler coverage and post-dominator data to confirm that
/// every @c resume.label terminator jumps to a handler reachable from the fault
/// site and that the handler post-dominates the faulting block, mirroring the
/// runtime unwinding model. Emits diagnostics when a target is invalid or
/// missing.
/// @param fn Function being verified.
/// @param blockMap Mapping from labels to block pointers.
/// @return Success or a diagnostic describing the invalid target.
Expected<void> verifyResumeLabelTargets(
    const Function &fn, const std::unordered_map<std::string, const BasicBlock *> &blockMap)
{
    const HandlerCoverage coverage = computeHandlerCoverage(fn, blockMap);
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

                std::string suffix = "target ^";
                suffix += instr.labels[0];
                suffix += " must postdominate block ";
                suffix += faultingBlock->label;

                auto message = formatInstrDiag(fn, bb, instr, suffix);
                return Expected<void>{makeVerifierError(
                    VerifyDiagCode::EhResumeLabelInvalidTarget, instr.loc, std::move(message))};
            }
        }
    }

    return {};
}

} // namespace

/// @brief Analyse each function and ensure its EH regions are structurally sound.
/// @details Scans for functions containing EH opcodes, builds a label map, and
/// delegates to helper analyses that check stack balance and resume target
/// validity. Functions without EH constructs are skipped entirely. Diagnostics
/// are surfaced through the returned Expected; the sink is currently unused but
/// retained for future integration.
/// @param module Module whose functions are verified.
/// @param sink Diagnostic sink reserved for streaming messages.
/// @return Empty Expected on success or the first failure diagnostic.
il::support::Expected<void> EhVerifier::run(const Module &module, DiagSink &sink) const
{
    (void)sink;
    for (const auto &fn : module.functions)
    {
        bool hasEhOps = false;
        for (const auto &bb : fn.blocks)
        {
            for (const auto &instr : bb.instructions)
            {
                switch (instr.op)
                {
                    case Opcode::EhPush:
                    case Opcode::EhPop:
                    case Opcode::Trap:
                    case Opcode::TrapFromErr:
                    case Opcode::ResumeSame:
                    case Opcode::ResumeNext:
                    case Opcode::ResumeLabel:
                        hasEhOps = true;
                        break;
                    default:
                        break;
                }
                if (hasEhOps)
                    break;
            }
            if (hasEhOps)
                break;
        }

        if (!hasEhOps)
            continue;

        std::unordered_map<std::string, const BasicBlock *> blockMap;
        blockMap.reserve(fn.blocks.size());
        for (const auto &bb : fn.blocks)
            blockMap[bb.label] = &bb;

        if (auto result = checkEhStackBalance(fn, blockMap); !result)
            return result;

        if (auto result = verifyResumeLabelTargets(fn, blockMap); !result)
            return result;
    }

    return {};
}

} // namespace il::verify
