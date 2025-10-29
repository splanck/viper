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
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::verify
{
namespace
{

/// @brief Encode an exception-handler stack snapshot into a stable cache key.
/// @details Serialises the handler stack into a semicolon-separated string and
///          prefixes a bit that records whether a resume token is active.  The
///          resulting key uniquely identifies the execution state so traversals
///          can detect revisits and avoid infinite loops when exploring
///          recursive handler graphs.
/// @param stack Ordered list of handler blocks currently on the stack.
/// @param hasResumeToken Flag indicating whether a resume token is present.
/// @return Deterministic key suitable for hash-table lookups.
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

/// @brief Reconstruct the control-flow path that produced a state snapshot.
/// @details Walks the @p states parent chain starting at @p index and collects
///          the visited basic blocks in program order.  The path is used to
///          explain verifier diagnostics by showing how the interpreter could
///          reach the offending instruction.
/// @param states Arena of explored execution states.
/// @param index Index of the state whose ancestry should be materialised.
/// @return Ordered list of basic blocks visited before the state.
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

/// @brief Convert a basic-block path into a human-readable string.
/// @details Joins block labels with arrows so diagnostics can include the
///          precise control-flow route.  Empty paths yield an empty string,
///          keeping messages terse for entry-block failures.
/// @param path Basic blocks comprising the path from entry to failure.
/// @return Formatted path string for diagnostic suffixes.
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

struct InvariantContext
{
    const EhModel &model;
    const std::vector<StackState> &states;
    std::optional<il::support::Diag> &failure;
};

/// @brief Emit a diagnostic for a failed EH invariant.
/// @details Appends the reconstructed control-flow path to @p detail and stores
///          the resulting diagnostic in the shared context. Subsequent
///          invocations become no-ops once a failure has already been
///          recorded.
/// @param ctx Shared diagnostic context with access to path information.
/// @param invariant Name of the invariant being checked.
/// @param code Diagnostic code describing the failure category.
/// @param bb Block containing the offending instruction.
/// @param instr Instruction associated with the violation.
/// @param stateIndex Index of the current execution state.
/// @param detail Human-readable suffix appended to the formatted diagnostic.
/// @return Always returns false so callers can propagate failure via early return.
bool emitInvariantFailure(InvariantContext &ctx,
                          std::string_view invariant,
                          VerifyDiagCode code,
                          const BasicBlock &bb,
                          const Instr &instr,
                          int stateIndex,
                          std::string detail)
{
    if (ctx.failure)
        return false;

    const std::vector<const BasicBlock *> path = buildPath(ctx.states, stateIndex);
    if (!path.empty())
    {
        detail.append("; path: ");
        detail.append(formatPathString(path));
    }

    std::string suffix;
    suffix.reserve(invariant.size() + 2 + detail.size());
    suffix.append(invariant);
    suffix.append(": ");
    suffix.append(detail);

    auto message = formatInstrDiag(ctx.model.function(), bb, instr, suffix);
    ctx.failure = makeVerifierError(code, instr.loc, std::move(message));
    return false;
}

bool checkNoHandlerCrossing(InvariantContext &ctx,
                            const BasicBlock &bb,
                            const Instr &instr,
                            int stateIndex,
                            const std::vector<const BasicBlock *> &handlerStack)
{
    if (!handlerStack.empty())
        return true;

    return emitInvariantFailure(ctx,
                                "checkNoHandlerCrossing",
                                VerifyDiagCode::EhStackUnderflow,
                                bb,
                                instr,
                                stateIndex,
                                "eh.pop without matching eh.push");
}

bool checkAllPathsCloseTry(
    InvariantContext &ctx, const BasicBlock &bb, const Instr &terminator, int stateIndex, int depth)
{
    if (terminator.op != Opcode::Ret || depth == 0)
        return true;

    std::string detail = "unmatched eh.push depth ";
    detail += std::to_string(depth);
    return emitInvariantFailure(ctx,
                                "checkAllPathsCloseTry",
                                VerifyDiagCode::EhStackLeak,
                                bb,
                                terminator,
                                stateIndex,
                                std::move(detail));
}

bool checkUnreachableAfterThrow(InvariantContext &ctx,
                                const BasicBlock &bb,
                                const Instr &instr,
                                int stateIndex,
                                bool hasResumeToken)
{
    if (hasResumeToken)
        return true;

    return emitInvariantFailure(ctx,
                                "checkUnreachableAfterThrow",
                                VerifyDiagCode::EhResumeTokenMissing,
                                bb,
                                instr,
                                stateIndex,
                                "resume.* requires active resume token");
}

class EhStackVerifier
{
  public:
    explicit EhStackVerifier(const EhModel &model) : model(model), ctx{model, states, failure} {}

    il::support::Expected<void> verify()
    {
        if (!model.entry())
            return {};

        seedInitialState();
        while (!worklist.empty() && !failure)
        {
            const int stateIndex = worklist.front();
            worklist.pop_front();
            if (!processState(stateIndex))
                break;
        }

        if (failure)
            return il::support::Expected<void>{std::move(*failure)};

        return {};
    }

  private:
    void seedInitialState()
    {
        StackState initial;
        initial.block = model.entry();
        states.push_back(initial);
        recordVisit(*initial.block, initial.handlerStack, initial.hasResumeToken);
        worklist.push_back(0);
    }

    bool processState(int stateIndex)
    {
        StackState &frame = states[stateIndex];
        if (!frame.block)
            return true;

        const BasicBlock &bb = *frame.block;
        std::vector<const BasicBlock *> handlerStack = frame.handlerStack;
        bool hasResumeToken = frame.hasResumeToken;
        const Instr *terminator = nullptr;

        for (const auto &instr : bb.instructions)
        {
            if (!handleInstruction(bb, instr, stateIndex, handlerStack, hasResumeToken))
                return false;

            if (isTerminator(instr.op))
            {
                terminator = &instr;
                break;
            }
        }

        if (!terminator)
            return true;

        frame.handlerStack = handlerStack;
        frame.hasResumeToken = hasResumeToken;
        frame.depth = static_cast<int>(handlerStack.size());

        if (!checkAllPathsCloseTry(ctx, bb, *terminator, stateIndex, frame.depth))
            return false;

        if (terminator->op == Opcode::Trap || terminator->op == Opcode::TrapFromErr)
            return handleTrap(handlerStack, stateIndex);

        enqueueSuccessors(*terminator, handlerStack, hasResumeToken, stateIndex);
        return true;
    }

    bool handleInstruction(const BasicBlock &bb,
                           const Instr &instr,
                           int stateIndex,
                           std::vector<const BasicBlock *> &handlerStack,
                           bool &hasResumeToken)
    {
        switch (instr.op)
        {
            case Opcode::EhPush:
            {
                const BasicBlock *handlerBlock = nullptr;
                if (!instr.labels.empty())
                    handlerBlock = model.findBlock(instr.labels[0]);
                handlerStack.push_back(handlerBlock);
                return true;
            }
            case Opcode::EhPop:
                if (!checkNoHandlerCrossing(ctx, bb, instr, stateIndex, handlerStack))
                    return false;
                handlerStack.pop_back();
                return true;
            case Opcode::ResumeSame:
            case Opcode::ResumeNext:
            case Opcode::ResumeLabel:
                if (!checkUnreachableAfterThrow(ctx, bb, instr, stateIndex, hasResumeToken))
                    return false;
                if (!handlerStack.empty())
                    handlerStack.pop_back();
                hasResumeToken = false;
                return true;
            default:
                return true;
        }
    }

    bool handleTrap(const std::vector<const BasicBlock *> &handlerStack, int stateIndex)
    {
        if (handlerStack.empty())
            return true;

        const BasicBlock *handlerBlock = handlerStack.back();
        if (!handlerBlock)
            return true;

        queueState(handlerBlock, handlerStack, true, stateIndex);
        return true;
    }

    void enqueueSuccessors(const Instr &terminator,
                           const std::vector<const BasicBlock *> &handlerStack,
                           bool hasResumeToken,
                           int stateIndex)
    {
        const std::vector<const BasicBlock *> successors = model.gatherSuccessors(terminator);
        for (const BasicBlock *succ : successors)
            queueState(succ, handlerStack, hasResumeToken, stateIndex);
    }

    bool recordVisit(const BasicBlock &block,
                     const std::vector<const BasicBlock *> &handlerStack,
                     bool hasResumeToken)
    {
        const std::string key = encodeStateKey(handlerStack, hasResumeToken);
        return visited[&block].insert(key).second;
    }

    void queueState(const BasicBlock *block,
                    const std::vector<const BasicBlock *> &handlerStack,
                    bool hasResumeToken,
                    int parentIndex)
    {
        if (!block)
            return;

        if (!recordVisit(*block, handlerStack, hasResumeToken))
            return;

        StackState next;
        next.block = block;
        next.handlerStack = handlerStack;
        next.hasResumeToken = hasResumeToken;
        next.parent = parentIndex;
        next.depth = static_cast<int>(handlerStack.size());

        const int index = static_cast<int>(states.size());
        states.push_back(std::move(next));
        worklist.push_back(index);
    }

    const EhModel &model;
    std::deque<int> worklist;
    std::vector<StackState> states;
    std::unordered_map<const BasicBlock *, std::unordered_set<std::string>> visited;
    std::optional<il::support::Diag> failure;
    InvariantContext ctx;
};

/// @brief Determine whether an opcode can fault and therefore require a handler.
/// @details Classifies EH-related opcodes, branches, and returns as safe while
///          treating all other operations as potential fault sources.  The
///          handler coverage analysis uses this to identify basic blocks that
///          must be dominated by a handler when resume tokens are absent.
/// @param op Opcode under evaluation.
/// @return True when @p op may raise a fault, false for benign instructions.
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

/// @brief Explore reachable blocks to determine which handlers protect them.
/// @details Performs a forward data-flow traversal that tracks the active EH
///          stack and whether a resume token is live.  As the traversal visits
///          potential faulting instructions it records the innermost handler
///          responsible for the current block, building the coverage map used
///          by later checks.
class HandlerCoverageTraversal
{
  public:
    /// @brief Create a traversal wired to the given EH model and coverage map.
    /// @details Stores references so @ref compute can populate @p coverage in
    ///          place without copying data structures.
    /// @param model Exception-handling graph to traverse.
    /// @param coverage Output map that gathers handler-to-block relationships.
    HandlerCoverageTraversal(const EhModel &model, HandlerCoverage &coverage)
        : model(model), coverage(coverage)
    {
    }

    /// @brief Execute the traversal starting at the function entry block.
    /// @details Initialises the work queue, walks reachable basic blocks, and
    ///          records handler coverage whenever a potential fault is observed
    ///          without an active resume token.  The algorithm mirrors the stack
    ///          discipline enforced by the VM to faithfully reproduce handler
    ///          transitions.
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

    /// @brief Update traversal state in response to an EH-related instruction.
    /// @details Tracks handler pushes and pops, toggles resume tokens when
    ///          encountering resume opcodes, and records coverage for faulting
    ///          instructions.  When the instruction is a terminator, the method
    ///          returns it so the caller can enqueue successors.
    /// @param instr Instruction being processed.
    /// @param bb Owning basic block (used for coverage bookkeeping).
    /// @param state Mutable traversal snapshot describing the active handlers.
    /// @return Pointer to the instruction when it terminates the block; null otherwise.
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

    /// @brief Simulate trap unwinding by enqueuing the innermost handler block.
    /// @details When the current state is positioned at a trap terminator, the
    ///          method records coverage for the handler guarding the block and
    ///          schedules that handler for execution with an active resume
    ///          token.  This mirrors the runtime's trap dispatch semantics.
    /// @param bb Faulting block whose handler should be entered.
    /// @param state Traversal state observed at the trap terminator.
    /// @param worklist Queue receiving the synthesised handler state.
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

    /// @brief Queue successor blocks reached after executing a terminator.
    /// @details Duplicates the traversal state for each successor, adjusts the
    ///          resume-token flag when handling @c resume.label, and forwards the
    ///          states to @ref enqueueState for deduplication.
    /// @param terminator Block terminator that produced the successor list.
    /// @param state Traversal state prior to transferring control.
    /// @param worklist Queue that receives unexplored states.
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

    /// @brief Add a state to the worklist if it has not been visited before.
    /// @details Uses @ref encodeStateKey to deduplicate per-block states so the
    ///          traversal remains finite even when handlers recurse.  States
    ///          lacking a block pointer are discarded silently.
    /// @param state Candidate state to explore.
    /// @param worklist Work queue managed by @ref compute.
    void enqueueState(State state, std::deque<State> &worklist)
    {
        if (!state.block)
            return;

        const std::string key = encodeStateKey(state.handlerStack, state.hasResumeToken);
        if (!visited[state.block].insert(key).second)
            return;

        worklist.push_back(state);
    }

    /// @brief Stage a state for future exploration without immediately running it.
    /// @details Identical to the two-argument overload but appends to a pending
    ///          deque used to bootstrap the traversal before the main loop
    ///          begins.  This keeps queue initialisation logic centralised.
    /// @param state Candidate state to schedule.
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

/// @brief Build a mapping from handler blocks to the blocks they protect.
/// @details Drives @ref HandlerCoverageTraversal to explore the function and
///          returns the populated coverage map.  Callers use this data to
///          validate resume targets and ensure handlers dominate the sites they
///          guard.
/// @param model Exception-handling model describing the function.
/// @return Map of handler blocks to the set of basic blocks they cover.
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

/// @brief Compute a post-dominator matrix for the reachable CFG.
/// @details Performs a breadth-first reachability walk to ignore dead blocks,
///          assigns indices to the survivors, and then runs an iterative data
///          flow algorithm to determine post-dominance relationships.  The
///          resulting structure allows constant-time queries when validating
///          EH resume edges.
/// @param model Exception-handling model describing the function under check.
/// @return Matrix-backed summary of post-dominator relationships.
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

/// @brief Query whether one block post-dominates another according to @p info.
/// @details Looks up the indices assigned by @ref computePostDominators and
///          checks the boolean matrix entry.  Missing blocks are treated as not
///          related, which naturally occurs for unreachable code.
/// @param info Precomputed post-dominator summary.
/// @param from Basic block being post-dominated.
/// @param candidate Block that may post-dominate @p from.
/// @return True if @p candidate post-dominates @p from, false otherwise.
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

/// @brief Ensure exception-handler pushes and pops remain balanced.
/// @details Simulates execution from the entry block, tracking the active
///          handler stack and resume-token state.  The verifier reports
///          underflows, leaks at return instructions, and resume operations that
///          lack a token, emitting diagnostics that include the reconstructed
///          control-flow path leading to the violation.
/// @param model Exception-handling model for the function under inspection.
/// @return Empty expected on success, or a diagnostic describing the failure.
il::support::Expected<void> checkEhStackBalance(const EhModel &model)
{
    EhStackVerifier verifier(model);
    return verifier.verify();
}

/// @brief Placeholder for dominance checks on exception handlers.
/// @details The legacy verifier exposes this hook but the modern
///          implementation has not yet adopted the logic.  Returning success
///          keeps behaviour consistent with historical builds while leaving a
///          future seam for more detailed analysis.
/// @param model Exception-handling model (currently unused).
/// @return Always returns success until the check is implemented.
il::support::Expected<void> checkDominanceOfHandlers(const EhModel &model)
{
    (void)model;
    return {};
}

/// @brief Placeholder for detecting handlers that can never be reached.
/// @details Mirrors the legacy verifier interface while deferring the actual
///          implementation.  The stub allows callers to sequence checks without
///          special casing missing functionality.
/// @param model Exception-handling model (currently unused).
/// @return Always returns success until handler reachability analysis lands.
il::support::Expected<void> checkUnreachableHandlers(const EhModel &model)
{
    (void)model;
    return {};
}

/// @brief Validate that resume.label targets post-dominate their triggering blocks.
/// @details Uses handler coverage to identify basic blocks that may resume
///          through a given handler and queries the post-dominator matrix to
///          ensure the resume target is structurally valid.  If a mismatch is
///          detected the function emits a diagnostic pointing out the offending
///          handler and resume site.
/// @param model Exception-handling model that exposes block lookups.
/// @return Success when all resume edges are valid; otherwise a diagnostic.
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
