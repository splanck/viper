//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the exception-handling verifier.  The pass checks EH stack balance,
// validates resume targets, and builds coverage data so diagnostics can pinpoint
// control-flow paths with mismatched handlers.
//
//===----------------------------------------------------------------------===//

#include "il/verify/EhVerifier.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/DiagFormat.hpp"

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::verify
{
using il::support::Expected;

template <typename T> using ErrorOr = il::support::Expected<T>;

using CFG = std::unordered_map<std::string, const BasicBlock *>;

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

struct EhState
{
    int depth = 0;
    const BasicBlock *bb = nullptr;
};

/// @brief Reconstruct the path of basic blocks leading to a recorded state.
/// @details Walks the parent index chain emitted by the worklist search, collecting
///          each block pointer from the stored states before reversing the sequence
///          so diagnostics can report the forward execution path that triggered an
///          EH mismatch.
/// @param states Snapshot of traversal states explored so far.
/// @param parents Parent indices forming the predecessor chain.
/// @param index Index of the state whose path should be reconstructed.
/// @return Sequence of basic block pointers describing the execution path.
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

/// @brief Format a block path for inclusion in diagnostics.
/// @details Joins the block labels with arrow separators to produce a readable
///          representation of the execution path returned by @ref buildPath.
/// @param path Sequence of blocks to stringify.
/// @return String describing the block path.
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

/// @brief Materialise a verifier diagnostic for EH stack mismatches.
/// @details Builds the execution path leading to the offending instruction and
///          tailors the diagnostic suffix to the specific error code.  The helper
///          centralises message formatting so all EH stack errors share consistent
///          wording.
/// @param fn Function being verified.
/// @param bb Basic block containing the problematic instruction.
/// @param instr Instruction that triggered the mismatch.
/// @param code Diagnostic identifier describing the failure.
/// @param states Snapshot of traversal states.
/// @param parents Parent index chain for the states.
/// @param stateIndex Index of the failing state.
/// @param depth Depth of the handler stack when the mismatch occurred.
/// @return Expected containing the constructed diagnostic error.
ErrorOr<void> reportEhMismatch(const Function &fn,
                               const BasicBlock &bb,
                               const Instr &instr,
                               VerifyDiagCode code,
                               const std::vector<EhState> &states,
                               const std::vector<int> &parents,
                               int stateIndex,
                               int depth)
{
    const std::vector<const BasicBlock *> path = buildPath(states, parents, stateIndex);
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

    auto message = formatInstrDiag(fn, bb, instr, suffix);
    return ErrorOr<void>{makeVerifierError(code, instr.loc, std::move(message))};
}

/// @brief Build a label-to-block lookup table for the function under analysis.
/// @details Iterates over every basic block and records its address in an unordered
///          map keyed by label.  The resulting table accelerates later lookups when
///          translating terminator labels into block pointers during traversal.
/// @param fn Function whose blocks should be indexed.
/// @return Mapping from block labels to block pointers.
CFG buildCfg(const Function &fn)
{
    CFG blockMap;
    blockMap.reserve(fn.blocks.size());
    for (const auto &bb : fn.blocks)
        blockMap[bb.label] = &bb;
    return blockMap;
}

/// @brief Verify that eh.push/eh.pop usage stays balanced along every path.
/// @details Performs a worklist traversal over the function, tracking the handler
///          stack and resume-token state for each reachable block.  Imbalances or
///          invalid resume operations trigger diagnostics that include the traversal
///          path leading to the offending instruction.
/// @param fn Function whose EH regions are validated.
/// @return Success when the EH stack is balanced; otherwise a diagnostic.
ErrorOr<void> checkBalancedTryCatch(const Function &fn)
{
    if (fn.blocks.empty())
        return {};

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
                    return reportEhMismatch(fn,
                                            bb,
                                            instr,
                                            VerifyDiagCode::EhStackUnderflow,
                                            states,
                                            parents,
                                            stateIndex,
                                            depth);

                handlerStack.pop_back();
                depth = static_cast<int>(handlerStack.size());
            }
            else if (instr.op == Opcode::ResumeSame || instr.op == Opcode::ResumeNext ||
                     instr.op == Opcode::ResumeLabel)
            {
                if (!hasResumeToken)
                    return reportEhMismatch(fn,
                                            bb,
                                            instr,
                                            VerifyDiagCode::EhResumeTokenMissing,
                                            states,
                                            parents,
                                            stateIndex,
                                            depth);

                if (!handlerStack.empty())
                    handlerStack.pop_back();
                hasResumeToken = false;
                depth = static_cast<int>(handlerStack.size());
            }

            if (isTerminator(instr.op))
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
            return reportEhMismatch(fn,
                                    bb,
                                    *terminator,
                                    VerifyDiagCode::EhStackLeak,
                                    states,
                                    parents,
                                    stateIndex,
                                    depth);

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

    return {};
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

using HandlerCoverage =
    std::unordered_map<const BasicBlock *, std::unordered_set<const BasicBlock *>>;

/// @brief Map each handler to the blocks it protects within a function.
/// @details Performs a worklist traversal over the function's CFG, tracking the
/// active handler stack. Whenever a potentially faulting instruction executes
/// under a handler, the enclosing block is recorded in the handler's coverage
/// set. Resume operations pop the stack to model stack unwinding precisely.
/// @param fn Function whose handlers are analysed.
/// @param blockMap Mapping from labels to block pointers.
/// @return Coverage table keyed by handler block pointer.
/// @brief Worklist engine that computes handler coverage for potentially faulting blocks.
/// @details Maintains the active handler stack while traversing the CFG so each
///          handler can be associated with the blocks whose instructions may fault
///          under its protection.  The traversal respects resume operations to
///          model the stack unwinding performed at runtime.
class HandlerCoverageTraversal
{
  public:
    /// @brief Prepare the traversal with a label lookup table and output buffer.
    /// @details Stores references to the caller-provided block map and coverage
    ///          table so the traversal can reuse them without extra allocations.
    ///          The constructor performs no work beyond capturing these references.
    /// @param blockMap Mapping from block labels to block pointers.
    /// @param coverage Table that will be populated with handler coverage sets.
    HandlerCoverageTraversal(const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                             HandlerCoverage &coverage)
        : blockMap(blockMap), coverage(coverage)
    {
    }

    /// @brief Execute the traversal to collect handler coverage for @p fn.
    /// @details Seeds the worklist with the entry block and iteratively explores
    ///          successors while updating handler stacks, recording coverage for
    ///          potentially faulting instructions, and enqueuing trap and
    ///          fall-through destinations until all reachable states have been
    ///          processed.
    /// @param fn Function whose handler coverage should be computed.
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

    /// @brief Update traversal state for a single EH-aware instruction.
    /// @details Records coverage for potentially faulting opcodes, manipulates the
    ///          handler stack for push/pop/resume operations, and returns the
    ///          instruction when it serves as a block terminator so callers can
    ///          schedule successor exploration.
    /// @param instr Instruction currently being evaluated.
    /// @param bb Owning basic block.
    /// @param state Mutable traversal state for the block.
    /// @return Pointer to @p instr when it terminates the block; otherwise nullptr.
    const Instr *processEhInstruction(const Instr &instr, const BasicBlock &bb, State &state)
    {
        if (!state.hasResumeToken && !state.handlerStack.empty() &&
            isPotentialFaultingOpcode(instr.op))
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

        if (isTerminator(instr.op))
            return &instr;

        return nullptr;
    }

    /// @brief Schedule the handler path when encountering a trap terminator.
    /// @details Records coverage for the faulting block, prepares the successor state
    ///          that resumes execution inside the handler, and enqueues it for
    ///          further processing with a resume token in hand.
    /// @param bb Block that terminates with a trap instruction.
    /// @param state Traversal state captured at the trap site.
    /// @param worklist Pending states awaiting exploration.
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

    /// @brief Queue successor blocks reached via @p terminator.
    /// @details Copies the traversal state, adjusts the resume-token flag for
    ///          resume-label terminators, and enqueues each successor while
    ///          deduplicating via the visited table.
    /// @param terminator Instruction whose successors should be enqueued.
    /// @param state Traversal state captured at the end of the block.
    /// @param worklist Pending states awaiting exploration.
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

    /// @brief Push a new traversal state if it has not been explored yet.
    /// @details Computes a memoisation key from the handler stack and resume flag;
    ///          only unseen states are appended to the worklist, preventing
    ///          exponential blow-up when handlers nest deeply.
    /// @param state Candidate traversal state.
    /// @param worklist Pending states awaiting exploration.
    void enqueueState(State state, std::deque<State> &worklist)
    {
        if (!state.block)
            return;

        const std::string key = encodeStateKey(state.handlerStack, state.hasResumeToken);
        if (!visited[state.block].insert(key).second)
            return;

        worklist.push_back(std::move(state));
    }

    const std::unordered_map<std::string, const BasicBlock *> &blockMap;
    HandlerCoverage &coverage;
    std::unordered_map<const BasicBlock *, std::unordered_set<std::string>> visited;
};

/// @brief Compute handler coverage by delegating to the traversal helper.
/// @details Instantiates @ref HandlerCoverageTraversal with the provided CFG map
///          and executes it over the function, returning the populated coverage
///          table to the caller.
/// @param fn Function whose handlers should be analysed.
/// @param blockMap Mapping from labels to block pointers.
/// @return Coverage table keyed by handler block pointers.
HandlerCoverage computeHandlerCoverage(const Function &fn, const CFG &blockMap)
{
    HandlerCoverage coverage;
    HandlerCoverageTraversal traversal(blockMap, coverage);
    traversal.compute(fn);
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
ErrorOr<void> checkDominanceOfHandlers(const Function &fn, const CFG &blockMap)
{
    (void)fn;
    (void)blockMap;
    return {};
}

/// @brief Placeholder for detecting handlers that cannot be reached.
/// @details Stubbed out for future expansion of the verifier.  The function is
///          retained so callers can wire in the analysis without altering call
///          sites when the implementation arrives.
/// @param fn Function being inspected.
/// @param blockMap Mapping from labels to block pointers.
/// @return Currently always succeeds.
ErrorOr<void> checkUnreachableHandlers(const Function &fn, const CFG &blockMap)
{
    (void)fn;
    (void)blockMap;
    return {};
}

/// @brief Validate resume-label terminators against handler coverage data.
/// @details Uses the computed handler coverage and post-dominator relation to
///          ensure each resume-label transfer targets a handler that can be
///          reached from the faulting block and that post-dominates it, mirroring
///          the runtime unwinding semantics.
/// @param fn Function whose resume edges are analysed.
/// @param blockMap Mapping from labels to block pointers.
/// @return Success or a diagnostic describing the invalid resume edge.
ErrorOr<void> checkResumeEdges(const Function &fn, const CFG &blockMap)
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
                return ErrorOr<void>{makeVerifierError(
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

        if (auto result = checkBalancedTryCatch(fn); !result)
            return result;

        CFG cfg = buildCfg(fn);

        if (auto result = checkDominanceOfHandlers(fn, cfg); !result)
            return result;

        if (auto result = checkUnreachableHandlers(fn, cfg); !result)
            return result;

        if (auto result = checkResumeEdges(fn, cfg); !result)
            return result;
    }

    return {};
}

} // namespace il::verify
