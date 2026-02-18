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

#include "il/transform/AnalysisIDs.hpp"
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

/// @brief Test whether a check instruction is trivially satisfied by constant operands.
///
/// @details After SCCP runs its constant-rewriting phase, any operand that was proven
///          constant appears in the IR as a ConstInt/ConstFloat literal rather than a
///          Temp reference.  This helper exploits that property to eliminate checks
///          whose condition can be verified at compile time without consulting the
///          SCCP lattice directly.
///
///          Rules applied per opcode:
///          - IdxChk(index, lo, hi)     — all three ConstInt and lo <= index < hi
///          - SDivChk0 / UDivChk0 /
///            SRemChk0 / URemChk0
///            (lhs, divisor)            — divisor is a non-zero ConstInt
///
/// @param instr Check instruction to inspect.
/// @param replacementOut When the function returns true and the check has a result,
///        this is set to the value that should replace all uses of the result
///        (the pass-through value of the check).
/// @return True when the check condition is statically guaranteed to succeed.
bool isCheckTriviallyTrue(const Instr &instr, Value &replacementOut)
{
    auto isConstInt = [](const Value &v) { return v.kind == Value::Kind::ConstInt; };

    switch (instr.op)
    {
        case Opcode::IdxChk:
        {
            // idx.chk index lo hi — passes when lo <= index < hi
            if (instr.operands.size() < 3)
                return false;
            const Value &index = instr.operands[0];
            const Value &lo    = instr.operands[1];
            const Value &hi    = instr.operands[2];
            if (!isConstInt(index) || !isConstInt(lo) || !isConstInt(hi))
                return false;
            if (lo.i64 <= index.i64 && index.i64 < hi.i64)
            {
                replacementOut = index; // result is the index value
                return true;
            }
            return false;
        }

        case Opcode::SDivChk0:
        case Opcode::SRemChk0:
        {
            // sdiv/srem.chk0 lhs divisor — passes when divisor != 0
            if (instr.operands.size() < 2)
                return false;
            const Value &divisor = instr.operands[1];
            if (!isConstInt(divisor))
                return false;
            if (divisor.i64 != 0)
            {
                replacementOut = divisor; // result is the divisor
                return true;
            }
            return false;
        }

        case Opcode::UDivChk0:
        case Opcode::URemChk0:
        {
            // udiv/urem.chk0 lhs divisor — passes when divisor != 0 (unsigned)
            if (instr.operands.size() < 2)
                return false;
            const Value &divisor = instr.operands[1];
            if (!isConstInt(divisor))
                return false;
            // Reinterpret as unsigned: any non-zero bit pattern passes.
            const auto udivisor = static_cast<unsigned long long>(divisor.i64);
            if (udivisor != 0)
            {
                replacementOut = divisor; // result is the divisor
                return true;
            }
            return false;
        }

        default:
            return false;
    }
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

    auto &domTree = analysis.getFunctionResult<viper::analysis::DomTree>(kAnalysisDominators, function);
    auto &loopInfo = analysis.getFunctionResult<LoopInfo>(kAnalysisLoopInfo, function);

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

            // Constant-operand elimination: if SCCP has already inlined the
            // operands as literals and the check condition is statically
            // satisfied, remove the check and replace its result with the
            // pass-through value.  This avoids redundant runtime checks for
            // patterns like idx.chk(5, 0, 10) where the bounds are known.
            //
            // Type constraint: ConstInt values type as I64 in the verifier.
            // When the check result type is narrower (e.g. I32 for IdxChk)
            // and the result has uses, substituting an I64 constant would
            // produce a type mismatch that the verifier rejects.  In that
            // case fall through to the dominance-based check instead.
            Value trivialReplacement;
            if (isCheckTriviallyTrue(instr, trivialReplacement))
            {
                if (instr.result && useInfo.hasUses(*instr.result))
                {
                    // Check whether the replacement type matches the result type.
                    // ConstInt values type as I64; if the instruction result is
                    // narrower we cannot safely substitute without a type error.
                    const bool replacementIsI64ConstInt =
                        trivialReplacement.kind == Value::Kind::ConstInt &&
                        !trivialReplacement.isBool;
                    if (replacementIsI64ConstInt && instr.type.kind != Type::Kind::I64)
                    {
                        // Type mismatch — fall through to dominance-based check.
                        // The dominance check can still replace with a same-typed
                        // Temp if a dominating occurrence already holds the result.
                    }
                    else
                    {
                        useInfo.replaceAllUses(*instr.result, trivialReplacement);
                        toErase.push_back({block, idx});
                        changed = true;
                        continue;
                    }
                }
                else
                {
                    // Result is either absent or has no live uses — safe to erase
                    // without substitution regardless of type.
                    toErase.push_back({block, idx});
                    changed = true;
                    continue;
                }
            }

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
    preserved.preserveFunction(kAnalysisCFG);
    // Dominators are preserved (we only remove instructions, not blocks)
    preserved.preserveFunction(kAnalysisDominators);
    // Loop info is preserved (loop structure unchanged)
    preserved.preserveFunction(kAnalysisLoopInfo);
    return preserved;
}

void registerCheckOptPass(PassRegistry &registry)
{
    registry.registerFunctionPass("check-opt", []() { return std::make_unique<CheckOpt>(); });
}

} // namespace il::transform
