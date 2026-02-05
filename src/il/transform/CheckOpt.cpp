//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the CheckOpt pass for optimizing check opcodes. The pass performs
// dominance-based redundancy elimination and loop-invariant check hoisting to
// reduce runtime overhead from bounds checks and division-by-zero checks.
//
//===----------------------------------------------------------------------===//

#include "il/transform/CheckOpt.hpp"

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/Dominators.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Value.hpp"
#include "il/utils/UseDefInfo.hpp"
#include "il/utils/Utils.hpp"

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::transform
{
namespace
{

/// @brief Check if an opcode is a check operation that can be optimized.
bool isCheckOpcode(Opcode op)
{
    switch (op)
    {
        case Opcode::IdxChk:
        case Opcode::SDivChk0:
        case Opcode::UDivChk0:
        case Opcode::SRemChk0:
        case Opcode::URemChk0:
        case Opcode::CastFpToSiRteChk:
        case Opcode::CastFpToUiRteChk:
        case Opcode::CastSiNarrowChk:
        case Opcode::CastUiNarrowChk:
            return true;
        default:
            return false;
    }
}

/// @brief Key representing a check condition for redundancy detection.
/// @details Two checks with the same key test the same condition. Uses the
///          shared valueEquals() helper for consistent value comparison.
struct CheckKey
{
    Opcode op;
    Type type;
    std::vector<Value> operands;

    bool operator==(const CheckKey &other) const
    {
        if (op != other.op || operands.size() != other.operands.size())
            return false;
        // Type comparison (only relevant for typed checks like IdxChk)
        if (type.kind != other.type.kind)
            return false;
        for (size_t i = 0; i < operands.size(); ++i)
        {
            if (!valueEquals(operands[i], other.operands[i]))
                return false;
        }
        return true;
    }
};

/// @brief Hash functor for CheckKey using shared value hashing.
/// @details Combines opcode and type with each operand hash using the
///          shared valueHash() helper for consistency.
struct CheckKeyHash
{
    size_t operator()(const CheckKey &key) const
    {
        size_t h = static_cast<size_t>(key.op);
        h ^= static_cast<size_t>(key.type.kind) << 8;
        for (const auto &v : key.operands)
        {
            h ^= valueHash(v) + kHashPhiMix + (h << 6) + (h >> 2);
        }
        return h;
    }
};

/// @brief Build a CheckKey from an instruction.
CheckKey makeCheckKey(const Instr &instr)
{
    CheckKey key;
    key.op = instr.op;
    key.type = instr.type;
    key.operands = instr.operands;
    return key;
}

/// @brief Information about a dominating check instruction.
struct DominatingCheck
{
    BasicBlock *block;
    std::optional<unsigned> resultId;
};

/// @brief Find a basic block by label within a function.
/// @param function The function containing the block.
/// @param label The block label to search for.
/// @return Pointer to the block, or nullptr if not found.
BasicBlock *findBlock(Function &function, const std::string &label)
{
    return viper::il::findBlock(function, label);
}

/// @brief Find the preheader block for a loop.
/// @details The preheader is the unique block outside the loop that branches
///          to the loop header. It provides a safe location for hoisting
///          loop-invariant operations. Returns nullptr if no unique preheader
///          exists (e.g., multiple entry edges or missing block).
/// @param function The function containing the loop.
/// @param loop The loop to find the preheader for.
/// @param header The loop's header block.
/// @return Pointer to the preheader block, or nullptr if not canonical.
BasicBlock *findPreheader(Function &function, const Loop &loop, BasicBlock &header)
{
    BasicBlock *preheader = nullptr;
    for (auto &block : function.blocks)
    {
        if (loop.contains(block.label))
            continue;
        if (!block.terminated || block.instructions.empty())
            continue;
        const Instr &term = block.instructions.back();
        bool targetsHeader = false;
        for (const auto &label : term.labels)
        {
            if (label == header.label)
            {
                targetsHeader = true;
                break;
            }
        }
        if (!targetsHeader)
            continue;
        if (preheader && preheader != &block)
            return nullptr; // Multiple preheaders - not canonical form
        preheader = &block;
    }
    return preheader;
}

/// @brief Seed the invariant set with values defined outside the loop.
/// @details Populates the invariant set with all SSA temporaries that are:
///          - Function parameters (always loop-invariant)
///          - Block parameters of blocks outside the loop
///          - Results of instructions in blocks outside the loop
/// @param loop The loop whose invariants are being computed.
/// @param function The function containing the loop.
/// @param invariants Output set to populate with invariant temporary IDs.
void seedInvariants(const Loop &loop, Function &function, std::unordered_set<unsigned> &invariants)
{
    // Function parameters are always invariant
    for (const auto &param : function.params)
        invariants.insert(param.id);

    // Values defined in blocks outside the loop are invariant
    for (auto &block : function.blocks)
    {
        if (loop.contains(block.label))
            continue;
        for (const auto &param : block.params)
            invariants.insert(param.id);
        for (const auto &instr : block.instructions)
            if (instr.result)
                invariants.insert(*instr.result);
    }
}

/// @brief Check if all operands of an instruction are loop-invariant.
/// @details An operand is invariant if it's a constant (non-Temp) or if its
///          temporary ID is in the invariant set (defined outside the loop).
/// @param instr The instruction to check.
/// @param invariants Set of known loop-invariant temporary IDs.
/// @return True if all operands are loop-invariant.
bool operandsInvariant(const Instr &instr, const std::unordered_set<unsigned> &invariants)
{
    auto isInvariantValue = [&invariants](const Value &value)
    {
        if (value.kind != Value::Kind::Temp)
            return true; // Constants are always invariant
        return invariants.contains(value.id);
    };

    for (const auto &operand : instr.operands)
    {
        if (!isInvariantValue(operand))
            return false;
    }
    return true;
}

/// @brief Check if an instruction would execute on every loop iteration.
/// @details Only instructions that must execute can be safely hoisted to the
///          preheader. Currently uses a conservative approximation: only
///          instructions in the loop header are considered guaranteed.
/// @param block The block containing the instruction.
/// @param loop The loop being analyzed.
/// @return True if instructions in this block are guaranteed to execute.
bool isGuaranteedToExecute(const BasicBlock &block, const Loop &loop)
{
    // Conservative: only hoist from header where check must execute on loop entry
    return block.label == loop.headerLabel;
}

/// @brief Check if a loop contains exception handling operations.
/// @details Loops with EH-sensitive operations (exception handlers, resume
///          instructions, trap instructions) require special care when
///          hoisting. This function conservatively detects such loops.
/// @param loop The loop to check.
/// @param function The function containing the loop.
/// @return True if the loop contains any EH-sensitive operations.
bool loopHasEHSensitiveOps(const Loop &loop, Function &function)
{
    for (const auto &label : loop.blockLabels)
    {
        BasicBlock *block = findBlock(function, label);
        if (!block)
            continue;
        for (const auto &instr : block->instructions)
        {
            switch (instr.op)
            {
                case Opcode::ResumeSame:
                case Opcode::ResumeNext:
                case Opcode::ResumeLabel:
                case Opcode::EhPush:
                case Opcode::EhPop:
                case Opcode::TrapFromErr:
                case Opcode::TrapErr:
                    return true;
                default:
                    break;
            }
        }
    }
    return false;
}

} // namespace

std::string_view CheckOpt::id() const
{
    return "check-opt";
}

PreservedAnalyses CheckOpt::run(Function &function, AnalysisManager &analysis)
{
    if (function.blocks.empty())
        return PreservedAnalyses::all();

    auto &domTree = analysis.getFunctionResult<viper::analysis::DomTree>("dominators", function);
    auto &loopInfo = analysis.getFunctionResult<LoopInfo>("loop-info", function);

    bool changed = false;

    // Build use-def chains once for O(uses) replacement
    viper::il::UseDefInfo useInfo(function);

    // =========================================================================
    // Phase 1: Dominance-based redundancy elimination
    // =========================================================================
    // Walk dominator tree with scoped map so siblings do not incorrectly share
    // availability. Only checks that dominate the current block may be reused.
    std::unordered_map<CheckKey, DominatingCheck, CheckKeyHash> available;
    std::unordered_map<CheckKey, unsigned, CheckKeyHash> depthCount;
    std::vector<std::pair<BasicBlock *, size_t>> toErase;

    std::function<void(BasicBlock *)> visit = [&](BasicBlock *block)
    {
        if (!block)
            return;
        std::vector<CheckKey> added;
        for (size_t idx = 0; idx < block->instructions.size(); ++idx)
        {
            Instr &instr = block->instructions[idx];
            if (!isCheckOpcode(instr.op))
                continue;
            CheckKey key = makeCheckKey(instr);
            auto it = available.find(key);
            if (it != available.end() && domTree.dominates(it->second.block, block))
            {
                if (instr.result && it->second.resultId)
                {
                    useInfo.replaceAllUses(*instr.result, Value::temp(*it->second.resultId));
                }
                toErase.push_back({block, idx});
                changed = true;
            }
            else
            {
                available[key] = DominatingCheck{block, instr.result};
                ++depthCount[key];
                added.push_back(key);
            }
        }

        auto childIt = domTree.children.find(block);
        if (childIt != domTree.children.end())
        {
            for (auto *child : childIt->second)
                visit(child);
        }

        for (const auto &k : added)
        {
            auto cntIt = depthCount.find(k);
            if (cntIt != depthCount.end())
            {
                if (--cntIt->second == 0)
                {
                    depthCount.erase(cntIt);
                    available.erase(k);
                }
            }
        }
    };

    visit(&function.blocks.front());

    for (auto it = toErase.rbegin(); it != toErase.rend(); ++it)
    {
        BasicBlock *block = it->first;
        size_t idx = it->second;
        block->instructions.erase(block->instructions.begin() + static_cast<std::ptrdiff_t>(idx));
    }

    // =========================================================================
    // Phase 2: Loop-invariant check hoisting
    // =========================================================================
    // For each loop, identify checks whose operands are loop-invariant and
    // hoist them to the preheader.

    for (const Loop &loop : loopInfo.loops())
    {
        BasicBlock *header = findBlock(function, loop.headerLabel);
        if (!header)
            continue;

        BasicBlock *preheader = findPreheader(function, loop, *header);
        if (!preheader)
            continue;
        if (loopHasEHSensitiveOps(loop, function))
            continue;

        // Seed invariants with out-of-loop definitions
        std::unordered_set<unsigned> invariants;
        invariants.reserve(function.params.size() + 32);
        seedInvariants(loop, function, invariants);

        // Also include results from hoisted checks as invariant
        // (enabling cascading hoists in a single pass)

        // Process each block in the loop
        for (const std::string &blockLabel : loop.blockLabels)
        {
            BasicBlock *block = findBlock(function, blockLabel);
            if (!block)
                continue;

            // Only hoist from blocks where the check is guaranteed to execute
            if (!isGuaranteedToExecute(*block, loop))
                continue;

            for (size_t idx = 0; idx < block->instructions.size();)
            {
                Instr &instr = block->instructions[idx];

                if (!isCheckOpcode(instr.op) || !operandsInvariant(instr, invariants))
                {
                    ++idx;
                    continue;
                }

                // Hoist the check to the preheader
                Instr hoisted = std::move(instr);
                block->instructions.erase(block->instructions.begin() +
                                          static_cast<std::ptrdiff_t>(idx));

                // Insert before the terminator in the preheader
                size_t insertIdx = preheader->instructions.size();
                if (preheader->terminated && insertIdx > 0)
                    --insertIdx;
                auto inserted = preheader->instructions.insert(
                    preheader->instructions.begin() + static_cast<std::ptrdiff_t>(insertIdx),
                    std::move(hoisted));

                // Mark the hoisted result as invariant for potential cascading
                if (inserted->result)
                    invariants.insert(*inserted->result);

                changed = true;
                // Don't increment idx - we removed the current instruction
            }
        }
    }

    if (!changed)
        return PreservedAnalyses::all();

    // Invalidate analyses since we modified instructions
    PreservedAnalyses preserved;
    preserved.preserveAllModules();
    // CFG structure is preserved (no block additions/removals or edge changes)
    preserved.preserveFunction("cfg");
    // Dominators are preserved (we only remove instructions, not blocks)
    preserved.preserveFunction("dominators");
    // Loop info is preserved (loop structure unchanged)
    preserved.preserveFunction("loop-info");
    return preserved;
}

void registerCheckOptPass(PassRegistry &registry)
{
    registry.registerFunctionPass("check-opt", []() { return std::make_unique<CheckOpt>(); });
}

} // namespace il::transform
