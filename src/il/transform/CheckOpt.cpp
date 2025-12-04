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
#include "il/utils/Utils.hpp"

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
/// Two checks with the same key test the same condition.
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
            const Value &a = operands[i];
            const Value &b = other.operands[i];
            if (a.kind != b.kind)
                return false;
            switch (a.kind)
            {
                case Value::Kind::Temp:
                    if (a.id != b.id)
                        return false;
                    break;
                case Value::Kind::ConstInt:
                    if (a.i64 != b.i64)
                        return false;
                    break;
                case Value::Kind::ConstFloat:
                    if (a.f64 != b.f64)
                        return false;
                    break;
                case Value::Kind::ConstStr:
                    if (a.str != b.str)
                        return false;
                    break;
                case Value::Kind::GlobalAddr:
                    if (a.str != b.str)
                        return false;
                    break;
                case Value::Kind::NullPtr:
                    break; // Always equal
                default:
                    return false;
            }
        }
        return true;
    }
};

struct CheckKeyHash
{
    size_t operator()(const CheckKey &key) const
    {
        size_t h = static_cast<size_t>(key.op);
        h ^= static_cast<size_t>(key.type.kind) << 8;
        for (const auto &v : key.operands)
        {
            h ^= static_cast<size_t>(v.kind) << 16;
            switch (v.kind)
            {
                case Value::Kind::Temp:
                    h ^= std::hash<unsigned>{}(v.id);
                    break;
                case Value::Kind::ConstInt:
                    h ^= std::hash<long long>{}(v.i64);
                    break;
                case Value::Kind::ConstFloat:
                    h ^= std::hash<double>{}(v.f64);
                    break;
                default:
                    break;
            }
            h = (h << 5) | (h >> 59); // rotate
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

BasicBlock *findBlock(Function &function, const std::string &label)
{
    return viper::il::findBlock(function, label);
}

/// @brief Collect blocks in dominator tree preorder starting from entry.
void collectDomPreorder(BasicBlock *block,
                        const viper::analysis::DomTree &domTree,
                        std::vector<BasicBlock *> &order)
{
    if (!block)
        return;
    order.push_back(block);
    auto it = domTree.children.find(block);
    if (it == domTree.children.end())
        return;
    for (auto *child : it->second)
        collectDomPreorder(child, domTree, order);
}

/// @brief Find the preheader block for a loop (block outside loop targeting header).
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

/// @brief Seed invariant temporaries with values defined outside the loop.
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

/// @brief Check if the check would execute on all paths into the loop.
/// For now, we only hoist checks in the header block to be conservative.
bool isGuaranteedToExecute(const BasicBlock &block, const Loop &loop)
{
    // Conservative: only hoist from header where check must execute on loop entry
    return block.label == loop.headerLabel;
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

    // =========================================================================
    // Phase 1: Dominance-based redundancy elimination
    // =========================================================================
    // Walk blocks in dominator tree preorder. For each check, if an equivalent
    // check already dominates this one, replace uses and mark for deletion.

    std::vector<BasicBlock *> domOrder;
    if (!function.blocks.empty())
        collectDomPreorder(&function.blocks.front(), domTree, domOrder);

    // Map from check key to the dominating check info
    std::unordered_map<CheckKey, DominatingCheck, CheckKeyHash> dominatingChecks;

    // Track instructions to erase (block pointer, instruction index)
    std::vector<std::pair<BasicBlock *, size_t>> toErase;

    for (BasicBlock *block : domOrder)
    {
        for (size_t idx = 0; idx < block->instructions.size(); ++idx)
        {
            Instr &instr = block->instructions[idx];
            if (!isCheckOpcode(instr.op))
                continue;

            CheckKey key = makeCheckKey(instr);
            auto it = dominatingChecks.find(key);

            if (it != dominatingChecks.end())
            {
                // A dominating check exists - this check is redundant
                if (instr.result && it->second.resultId)
                {
                    // Replace uses of this check's result with the dominating check's result
                    viper::il::replaceAllUses(
                        function, *instr.result, Value::temp(*it->second.resultId));
                }
                toErase.push_back({block, idx});
                changed = true;
            }
            else
            {
                // Record this check as the dominating one for this key
                dominatingChecks[key] = DominatingCheck{block, instr.result};
            }
        }
    }

    // Erase redundant checks in reverse order to keep indices valid
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
