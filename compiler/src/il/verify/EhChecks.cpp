//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include <functional>
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

struct Diagnostics
{
    void fail(il::support::Diag diag)
    {
        if (!error)
            error = std::move(diag);
    }

    [[nodiscard]] bool hasError() const noexcept
    {
        return error.has_value();
    }

    il::support::Expected<void> take()
    {
        if (error)
            return il::support::Expected<void>{std::move(*error)};
        return {};
    }

    std::optional<il::support::Diag> error;
};

void emitInvariantFailure(Diagnostics &diags,
                          std::string_view invariant,
                          VerifyDiagCode code,
                          const EhModel &model,
                          const BasicBlock &bb,
                          const Instr &instr,
                          const std::vector<StackState> &states,
                          int stateIndex,
                          int depth)
{
    if (diags.hasError())
        return;

    const std::vector<const BasicBlock *> path = buildPath(states, stateIndex);
    std::string suffix = "[";
    suffix += invariant;
    suffix += "] ";

    switch (code)
    {
        case VerifyDiagCode::EhStackUnderflow:
            suffix += "eh.pop without matching eh.push";
            break;
        case VerifyDiagCode::EhStackLeak:
            suffix += "unmatched eh.push depth ";
            suffix += std::to_string(depth);
            break;
        case VerifyDiagCode::EhResumeTokenMissing:
            suffix += "resume.* requires active resume token";
            break;
        default:
            break;
    }

    suffix += "; path: ";
    suffix += formatPathString(path);

    auto message = formatInstrDiag(model.function(), bb, instr, suffix);
    diags.fail(makeVerifierError(code, instr.loc, std::move(message)));
}

bool checkNoHandlerCrossing(const EhModel &model,
                            const BasicBlock &bb,
                            const Instr &instr,
                            std::vector<const BasicBlock *> &handlerStack,
                            Diagnostics &diags,
                            const std::vector<StackState> &states,
                            int stateIndex)
{
    if (!handlerStack.empty())
    {
        handlerStack.pop_back();
        return true;
    }

    emitInvariantFailure(diags,
                         "checkNoHandlerCrossing",
                         VerifyDiagCode::EhStackUnderflow,
                         model,
                         bb,
                         instr,
                         states,
                         stateIndex,
                         static_cast<int>(handlerStack.size()));
    return false;
}

bool checkUnreachableAfterThrow(const EhModel &model,
                                const BasicBlock &bb,
                                const Instr &instr,
                                std::vector<const BasicBlock *> &handlerStack,
                                bool &hasResumeToken,
                                Diagnostics &diags,
                                const std::vector<StackState> &states,
                                int stateIndex)
{
    if (!hasResumeToken)
    {
        emitInvariantFailure(diags,
                             "checkUnreachableAfterThrow",
                             VerifyDiagCode::EhResumeTokenMissing,
                             model,
                             bb,
                             instr,
                             states,
                             stateIndex,
                             static_cast<int>(handlerStack.size()));
        return false;
    }

    if (!handlerStack.empty())
        handlerStack.pop_back();
    hasResumeToken = false;
    return true;
}

bool checkAllPathsCloseTry(const EhModel &model,
                           const BasicBlock &bb,
                           const Instr &terminator,
                           const std::vector<const BasicBlock *> &handlerStack,
                           Diagnostics &diags,
                           const std::vector<StackState> &states,
                           int stateIndex)
{
    if (terminator.op != Opcode::Ret || handlerStack.empty())
        return true;

    emitInvariantFailure(diags,
                         "checkAllPathsCloseTry",
                         VerifyDiagCode::EhStackLeak,
                         model,
                         bb,
                         terminator,
                         states,
                         stateIndex,
                         static_cast<int>(handlerStack.size()));
    return false;
}

class EhStackTraversal
{
  public:
    EhStackTraversal(const EhModel &model, Diagnostics &diags) : model(model), diags(diags) {}

    bool run()
    {
        if (!model.entry())
            return true;

        StackState initial;
        initial.block = model.entry();
        enqueueState(std::move(initial));

        while (!worklist.empty())
        {
            const int stateIndex = worklist.front();
            worklist.pop_front();
            if (!processState(stateIndex))
                return false;
        }

        return !diags.hasError();
    }

  private:
    std::vector<StackState> states;

    void enqueueState(StackState state)
    {
        if (!state.block)
            return;

        const std::string key = encodeStateKey(state.handlerStack, state.hasResumeToken);
        if (!visited[state.block].insert(key).second)
            return;

        const int index = static_cast<int>(states.size());
        states.push_back(std::move(state));
        worklist.push_back(index);
    }

    bool processState(int stateIndex)
    {
        const StackState &snapshot = states[stateIndex];
        if (!snapshot.block)
            return true;

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
                if (!checkNoHandlerCrossing(
                        model, bb, instr, handlerStack, diags, states, stateIndex))
                    return false;
            }
            else if (instr.op == Opcode::ResumeSame || instr.op == Opcode::ResumeNext ||
                     instr.op == Opcode::ResumeLabel)
            {
                if (!checkUnreachableAfterThrow(
                        model, bb, instr, handlerStack, hasResumeToken, diags, states, stateIndex))
                    return false;
            }

            if (isTerminator(instr.op))
            {
                terminator = &instr;
                break;
            }
        }

        if (!terminator)
            return true;

        states[stateIndex].handlerStack = handlerStack;
        states[stateIndex].hasResumeToken = hasResumeToken;
        states[stateIndex].depth = static_cast<int>(handlerStack.size());

        if (!checkAllPathsCloseTry(model, bb, *terminator, handlerStack, diags, states, stateIndex))
            return false;

        if (terminator->op == Opcode::Trap || terminator->op == Opcode::TrapFromErr)
        {
            enqueueTrapHandler(stateIndex, handlerStack);
            return true;
        }

        enqueueSuccessors(*terminator, stateIndex, handlerStack, hasResumeToken);
        return true;
    }

    void enqueueTrapHandler(int stateIndex, const std::vector<const BasicBlock *> &handlerStack)
    {
        if (handlerStack.empty())
            return;

        const BasicBlock *handlerBlock = handlerStack.back();
        if (!handlerBlock)
            return;

        StackState nextState;
        nextState.block = handlerBlock;
        nextState.handlerStack = handlerStack;
        nextState.hasResumeToken = true;
        nextState.parent = stateIndex;
        nextState.depth = static_cast<int>(handlerStack.size());
        enqueueState(std::move(nextState));
    }

    void enqueueSuccessors(const Instr &terminator,
                           int stateIndex,
                           const std::vector<const BasicBlock *> &handlerStack,
                           bool hasResumeToken)
    {
        const std::vector<const BasicBlock *> successors = model.gatherSuccessors(terminator);
        for (const BasicBlock *succ : successors)
        {
            StackState nextState;
            nextState.block = succ;
            nextState.handlerStack = handlerStack;
            nextState.hasResumeToken =
                terminator.op == Opcode::ResumeLabel ? false : hasResumeToken;
            nextState.parent = stateIndex;
            nextState.depth = static_cast<int>(handlerStack.size());
            enqueueState(std::move(nextState));
        }
    }

    const EhModel &model;
    Diagnostics &diags;
    std::unordered_map<const BasicBlock *, std::unordered_set<std::string>> visited;
    std::deque<int> worklist;
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
            {
                // Heuristic: when the current block immediately branches to a trap block,
                // attribute coverage to the trap successor instead of the current block.
                if (const Instr *term = model.findTerminator(bb))
                {
                    const std::vector<const BasicBlock *> succs = model.gatherSuccessors(*term);
                    const BasicBlock *trapSucc = nullptr;
                    for (const BasicBlock *s : succs)
                    {
                        if (const Instr *sTerm = model.findTerminator(*s))
                        {
                            if (sTerm->op == Opcode::Trap || sTerm->op == Opcode::TrapFromErr)
                            {
                                trapSucc = s;
                                break;
                            }
                        }
                    }
                    if (trapSucc)
                    {
                        coverage[handlerBlock].insert(trapSucc);
                    }
                    else
                    {
                        coverage[handlerBlock].insert(&bb);
                    }
                }
                else
                {
                    coverage[handlerBlock].insert(&bb);
                }
            }
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

struct DomInfo
{
    std::unordered_map<const BasicBlock *, size_t> indices;
    std::vector<const BasicBlock *> nodes;
    std::unordered_map<const BasicBlock *, const BasicBlock *> idom;
};

/// @brief Compute forward dominators for the reachable CFG.
/// @details Performs BFS to find reachable blocks, assigns reverse-post-order
///          indices, and iteratively computes immediate dominators using the
///          Cooper–Harvey–Kennedy algorithm. The result allows O(depth) dominance
///          queries by walking the idom chain.
/// @param model Exception-handling model describing the function under check.
/// @return Dominator info with immediate dominator map for all reachable blocks.
DomInfo computeDominators(const EhModel &model)
{
    DomInfo info;
    if (!model.entry())
        return info;

    const BasicBlock *entry = model.entry();

    // BFS to find reachable blocks
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

    // Build reverse-post-order by doing DFS
    std::vector<const BasicBlock *> rpo;
    std::unordered_set<const BasicBlock *> visited;
    std::function<void(const BasicBlock *)> dfs = [&](const BasicBlock *bb)
    {
        if (!visited.insert(bb).second)
            return;
        if (const Instr *terminator = model.findTerminator(*bb))
        {
            for (const BasicBlock *succ : model.gatherSuccessors(*terminator))
            {
                if (reachable.count(succ))
                    dfs(succ);
            }
        }
        rpo.push_back(bb);
    };
    dfs(entry);
    std::reverse(rpo.begin(), rpo.end());

    // Assign indices
    for (size_t i = 0; i < rpo.size(); ++i)
    {
        info.indices[rpo[i]] = i;
        info.nodes.push_back(rpo[i]);
    }

    // Build predecessor map
    std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>> preds;
    for (const BasicBlock *bb : rpo)
    {
        if (const Instr *terminator = model.findTerminator(*bb))
        {
            for (const BasicBlock *succ : model.gatherSuccessors(*terminator))
            {
                if (reachable.count(succ))
                    preds[succ].push_back(bb);
            }
        }
    }

    // Initialize: entry has no dominator
    info.idom[entry] = nullptr;

    // Intersect helper: find nearest common ancestor in dominator tree
    auto intersect = [&](const BasicBlock *b1, const BasicBlock *b2) -> const BasicBlock *
    {
        while (b1 != b2)
        {
            while (info.indices[b1] > info.indices[b2])
            {
                auto it = info.idom.find(b1);
                if (it == info.idom.end() || !it->second)
                    return nullptr;
                b1 = it->second;
            }
            while (info.indices[b2] > info.indices[b1])
            {
                auto it = info.idom.find(b2);
                if (it == info.idom.end() || !it->second)
                    return nullptr;
                b2 = it->second;
            }
        }
        return b1;
    };

    // Iterative dominator computation
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (size_t i = 1; i < rpo.size(); ++i)
        {
            const BasicBlock *bb = rpo[i];
            const auto &predList = preds[bb];

            // Find first predecessor with computed idom
            const BasicBlock *newIdom = nullptr;
            for (const BasicBlock *p : predList)
            {
                if (info.idom.count(p))
                {
                    newIdom = p;
                    break;
                }
            }
            if (!newIdom)
                continue;

            // Intersect with other predecessors
            for (const BasicBlock *p : predList)
            {
                if (p == newIdom || !info.idom.count(p))
                    continue;
                newIdom = intersect(p, newIdom);
                if (!newIdom)
                    break;
            }

            if (!info.idom.count(bb) || info.idom[bb] != newIdom)
            {
                info.idom[bb] = newIdom;
                changed = true;
            }
        }
    }

    return info;
}

/// @brief Query whether one block dominates another according to @p info.
/// @details Walks up the immediate dominator chain from @p target until reaching
///          @p dominator or the entry block. A block always dominates itself.
/// @param info Precomputed dominator summary.
/// @param dominator Block that may dominate @p target.
/// @param target Block being tested for domination.
/// @return True if @p dominator dominates @p target, false otherwise.
bool isDominator(const DomInfo &info, const BasicBlock *dominator, const BasicBlock *target)
{
    if (!dominator || !target)
        return false;
    if (dominator == target)
        return true;

    const BasicBlock *current = target;
    while (current)
    {
        auto it = info.idom.find(current);
        if (it == info.idom.end())
            return false;
        current = it->second;
        if (current == dominator)
            return true;
    }
    return false;
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
    if (!model.entry())
        return {};

    Diagnostics diags;
    EhStackTraversal traversal(model, diags);
    traversal.run();
    return diags.take();
}

/// @brief Validate that exception handlers dominate the blocks they protect.
/// @details Computes forward dominators and handler coverage, then verifies that
///          the block containing the eh.push instruction dominates every block in
///          the handler's coverage set. This ensures that handlers are installed
///          before any protected code executes, maintaining structured EH semantics.
/// @param model Exception-handling model describing the function under check.
/// @return Empty expected on success, or a diagnostic describing the violation.
il::support::Expected<void> checkDominanceOfHandlers(const EhModel &model)
{
    if (!model.entry())
        return {};

    const HandlerCoverage coverage = computeHandlerCoverage(model);
    if (coverage.empty())
        return {};

    const DomInfo domInfo = computeDominators(model);

    // Build a map from handler blocks to their eh.push sites
    std::unordered_map<const BasicBlock *, std::pair<const BasicBlock *, const Instr *>>
        handlerToEhPush;
    for (const auto &bb : model.function().blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == Opcode::EhPush && !instr.labels.empty())
            {
                const BasicBlock *handlerBlock = model.findBlock(instr.labels[0]);
                if (handlerBlock)
                    handlerToEhPush[handlerBlock] = {&bb, &instr};
            }
        }
    }

    // For each handler, verify the eh.push block dominates all protected blocks
    for (const auto &[handlerBlock, protectedBlocks] : coverage)
    {
        if (!handlerBlock)
            continue;

        auto ehPushIt = handlerToEhPush.find(handlerBlock);
        if (ehPushIt == handlerToEhPush.end())
            continue; // No eh.push found, skip (malformed but caught elsewhere)

        const BasicBlock *ehPushBlock = ehPushIt->second.first;
        const Instr *ehPushInstr = ehPushIt->second.second;

        for (const BasicBlock *protectedBlock : protectedBlocks)
        {
            if (!protectedBlock)
                continue;

            // The block containing eh.push must dominate the protected block.
            // This ensures the handler is installed before the protected code runs.
            if (!isDominator(domInfo, ehPushBlock, protectedBlock))
            {
                std::string suffix = "eh.push block ";
                suffix += ehPushBlock->label;
                suffix += " does not dominate protected block ";
                suffix += protectedBlock->label;
                suffix += " (handler ^";
                suffix += handlerBlock->label;
                suffix += ")";

                auto message =
                    formatInstrDiag(model.function(), *ehPushBlock, *ehPushInstr, suffix);
                return il::support::Expected<void>{makeVerifierError(
                    VerifyDiagCode::EhHandlerNotDominant, ehPushInstr->loc, std::move(message))};
            }
        }
    }

    return {};
}

/// @brief Validate that all exception handler blocks are reachable from entry.
/// @details Identifies handler blocks by scanning for eh.push instructions, then
///          performs a CFG traversal from the function entry to determine which
///          blocks are reachable. Unreachable handlers indicate dead code that
///          could never execute, which is usually a sign of malformed IL.
/// @param model Exception-handling model describing the function under check.
/// @return Empty expected on success, or a diagnostic listing unreachable handlers.
il::support::Expected<void> checkUnreachableHandlers(const EhModel &model)
{
    if (!model.entry())
        return {};

    // Collect all handler blocks referenced by eh.push instructions
    std::unordered_set<const BasicBlock *> handlerBlocks;
    for (const auto &bb : model.function().blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == Opcode::EhPush && !instr.labels.empty())
            {
                if (const BasicBlock *handlerBlock = model.findBlock(instr.labels[0]))
                    handlerBlocks.insert(handlerBlock);
            }
        }
    }

    if (handlerBlocks.empty())
        return {};

    // Compute reachable blocks via BFS from entry
    // Note: We consider both normal CFG edges AND exception edges (trap -> handler)
    std::unordered_set<const BasicBlock *> reachable;
    std::deque<const BasicBlock *> worklist;
    worklist.push_back(model.entry());
    reachable.insert(model.entry());

    // Track which handlers are on the EH stack at each block for trap edges
    std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>> blockHandlerStack;
    blockHandlerStack[model.entry()] = {};

    // Track handlers that SHOULD be reachable (have faulting instructions in protected region)
    std::unordered_set<const BasicBlock *> shouldBeReachable;

    while (!worklist.empty())
    {
        const BasicBlock *bb = worklist.front();
        worklist.pop_front();

        std::vector<const BasicBlock *> currentStack = blockHandlerStack[bb];

        // Process instructions to track EH stack and detect potential faults
        for (const auto &instr : bb->instructions)
        {
            if (instr.op == Opcode::EhPush && !instr.labels.empty())
            {
                if (const BasicBlock *handlerBlock = model.findBlock(instr.labels[0]))
                    currentStack.push_back(handlerBlock);
            }
            else if (instr.op == Opcode::EhPop)
            {
                if (!currentStack.empty())
                    currentStack.pop_back();
            }
            // Any potentially faulting instruction marks the current handler as
            // "should be reachable" since it could trap at runtime
            else if (!currentStack.empty() && isPotentialFaultingOpcode(instr.op))
            {
                const BasicBlock *handlerBlock = currentStack.back();
                if (handlerBlock)
                {
                    shouldBeReachable.insert(handlerBlock);
                    if (reachable.insert(handlerBlock).second)
                    {
                        worklist.push_back(handlerBlock);
                        blockHandlerStack[handlerBlock] = currentStack;
                    }
                }
            }
        }

        // Find terminator and process successors
        if (const Instr *terminator = model.findTerminator(*bb))
        {
            // Normal CFG successors
            for (const BasicBlock *succ : model.gatherSuccessors(*terminator))
            {
                if (reachable.insert(succ).second)
                {
                    worklist.push_back(succ);
                    blockHandlerStack[succ] = currentStack;
                }
            }

            // Exception edge: trap/trap_from_err can transfer to handler
            if (terminator->op == Opcode::Trap || terminator->op == Opcode::TrapFromErr)
            {
                if (!currentStack.empty())
                {
                    const BasicBlock *handlerBlock = currentStack.back();
                    if (handlerBlock)
                    {
                        shouldBeReachable.insert(handlerBlock);
                        if (reachable.insert(handlerBlock).second)
                        {
                            worklist.push_back(handlerBlock);
                            blockHandlerStack[handlerBlock] = currentStack;
                        }
                    }
                }
            }
        }
    }

    // Check if any handler blocks that SHOULD be reachable are unreachable.
    // Handlers with no faulting instructions in their protected region are allowed
    // to be unreachable (they're just unused, not invalid).
    std::vector<std::string> unreachableLabels;
    for (const BasicBlock *handler : handlerBlocks)
    {
        // Only report if the handler should be reachable but isn't
        if (shouldBeReachable.count(handler) && reachable.find(handler) == reachable.end())
            unreachableLabels.push_back(handler->label);
    }

    if (!unreachableLabels.empty())
    {
        // Sort for deterministic output
        std::sort(unreachableLabels.begin(), unreachableLabels.end());

        std::string suffix = "unreachable handler block";
        if (unreachableLabels.size() > 1)
            suffix += "s";
        suffix += ": ";
        for (size_t i = 0; i < unreachableLabels.size(); ++i)
        {
            if (i > 0)
                suffix += ", ";
            suffix += "^";
            suffix += unreachableLabels[i];
        }

        std::string message = "function '";
        message += model.function().name;
        message += "': ";
        message += suffix;

        return il::support::Expected<void>{
            makeVerifierError(VerifyDiagCode::EhHandlerUnreachable, {}, std::move(message))};
    }

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
