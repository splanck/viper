//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a lightweight loop-invariant code motion pass.  The pass hoists
// trivially safe instructions—those proven non-trapping, side-effect free, and
// operand-invariant—into the loop preheader so they execute only once before the
// loop body.  It relies on LoopSimplify to guarantee the presence of a
// dedicated preheader and uses dominance information to traverse blocks in a
// stable order.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements a lightweight loop-invariant code motion pass.
/// @details Identifies trivially safe, loop-invariant instructions and hoists
///          them to the loop preheader. The pass is conservative, relying on
///          opcode metadata, verifier properties, and basic alias analysis to
///          avoid changing observable program behavior.

#include "il/transform/LICM.hpp"

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/transform/analysis/LoopInfo.hpp"

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"
#include "il/utils/Utils.hpp"
#include "il/verify/VerifierTable.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::transform
{
namespace
{
/// @brief Track a store within a loop for alias-checking load hoisting.
/// @details Captures the pointer operand and its byte size (when known) so
///          load instructions can be compared against loop stores safely.
struct StoreSite
{
    Value ptr;                    ///< Pointer operand written by a store.
    std::optional<unsigned> size; ///< Size in bytes of the store, if known.
};

/// @brief Determine whether an instruction can be hoisted out of the loop.
/// @details Rejects terminators, side-effecting instructions, and any opcode
///          marked as trapping by the verifier. Memory operations are only
///          allowed when they are explicitly safe, such as loads with proven
///          non-aliasing when @p allowLoadHoist is true.
/// @param instr Instruction to test.
/// @param allowLoadHoist Whether loads may be hoisted based on alias analysis.
/// @return True if the instruction is safe to move to the preheader.
bool isSafeToHoist(const Instr &instr, bool allowLoadHoist)
{
    const auto &info = getOpcodeInfo(instr.op);
    if (info.isTerminator || info.hasSideEffects)
        return false;
    if (!instr.labels.empty() || !instr.brArgs.empty())
        return false;

    if (auto spec = verify::lookupSpec(instr.op))
    {
        if (spec->hasSideEffects)
            return false;
    }

    if (auto props = verify::lookup(instr.op))
    {
        if (props->canTrap)
            return false;
    }

    const auto effects = memoryEffects(instr.op);
    if (effects == MemoryEffects::None)
        return true;

    if (allowLoadHoist && effects == MemoryEffects::Read && instr.op == Opcode::Load)
        return true;

    return false;
}

/// @brief Seed the invariant set with values defined outside the loop.
/// @details Inserts function parameters, block parameters, and results of
///          instructions from blocks not in the loop so the invariant set
///          starts with all values known to be loop-external.
/// @param loop Loop being analyzed.
/// @param function Function containing the loop.
/// @param invariants Output set of invariant temporary ids to populate.
void seedInvariants(const Loop &loop, Function &function, std::unordered_set<unsigned> &invariants)
{
    for (const auto &param : function.params)
        invariants.insert(param.id);

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

/// @brief Check whether all operands of @p instr are loop-invariant.
/// @details Treats non-temporary values as inherently invariant, and consults
///          the supplied invariant set for temporaries. Branch argument lists
///          are checked as well to avoid hoisting instructions that feed loop-
///          internal control flow edges.
/// @param instr Instruction whose operands should be validated.
/// @param invariants Set of invariant temporary ids.
/// @return True if every operand and branch argument is invariant.
bool operandsInvariant(const Instr &instr, const std::unordered_set<unsigned> &invariants)
{
    auto isInvariantValue = [&invariants](const Value &value)
    {
        if (value.kind != Value::Kind::Temp)
            return true;
        return invariants.contains(value.id);
    };

    for (const auto &operand : instr.operands)
    {
        if (!isInvariantValue(operand))
            return false;
    }

    for (const auto &argList : instr.brArgs)
        for (const auto &arg : argList)
            if (!isInvariantValue(arg))
                return false;

    return true;
}

/// @brief Collect loop blocks in dominator-tree preorder.
/// @details Walks the dominator tree starting at the loop header and appends
///          only blocks that are part of the loop. This produces a stable order
///          for scanning instructions while preserving dominance relationships.
/// @param block Current dominator-tree node to visit.
/// @param loop Loop being analyzed.
/// @param domTree Dominator tree for the function.
/// @param order Output list of blocks in visitation order.
void collectDominanceOrder(BasicBlock *block,
                           const Loop &loop,
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
    {
        if (!loop.contains(child->label))
            continue;
        collectDominanceOrder(child, loop, domTree, order);
    }
}

} // namespace

/// @brief Look up a basic block by label in a pre-built map.
/// @details Returns nullptr if the label is unknown, avoiding accidental
///          insertion into the map and keeping lookup O(1).
/// @param blocks Map from label to BasicBlock pointer.
/// @param label Label to look up.
/// @return Pointer to the block, or nullptr if not found.
BasicBlock *lookupBlock(const std::unordered_map<std::string, BasicBlock *> &blocks,
                        const std::string &label)
{
    auto it = blocks.find(label);
    return it == blocks.end() ? nullptr : it->second;
}

/// @brief Find the unique loop preheader for a loop header.
/// @details Scans CFG predecessors of the header and looks for exactly one
///          predecessor that is outside the loop. If multiple external preds
///          exist or none are found, the loop is treated as missing a valid
///          preheader and the function returns nullptr.
/// @param loop Loop whose preheader is being queried.
/// @param header Header block of the loop.
/// @param cfg CFG analysis providing predecessor edges.
/// @param blocks Label-to-block map for resolving CFG nodes.
/// @return Pointer to the unique preheader block, or nullptr if ambiguous.
BasicBlock *findPreheader(const Loop &loop,
                          BasicBlock &header,
                          const CFGInfo &cfg,
                          const std::unordered_map<std::string, BasicBlock *> &blocks)
{
    auto predsIt = cfg.predecessors.find(&header);
    if (predsIt == cfg.predecessors.end())
        return nullptr;
    BasicBlock *preheader = nullptr;
    for (const auto *pred : predsIt->second)
    {
        if (!pred || loop.contains(pred->label))
            continue;
        auto *mutablePred = lookupBlock(blocks, pred->label);
        if (!mutablePred)
            continue;
        if (preheader && preheader != mutablePred)
            return nullptr;
        preheader = mutablePred;
    }
    return preheader;
}

/// @brief Return the unique identifier for this pass.
/// @details Used when registering and invoking LICM via the pass registry.
/// @return The canonical pass id string "licm".
std::string_view LICM::id() const
{
    return "licm";
}

/// @brief Execute loop-invariant code motion on a function.
/// @details For each loop with a unique preheader, the pass collects invariant
///          values, scans loop blocks in dominator order, and hoists instructions
///          that are safe and invariant. Load hoisting is permitted only when
///          no memory writes are observed in the loop or alias analysis proves
///          the load does not alias any store. Preserved analyses are returned
///          conservatively when changes are made.
/// @param function Function to optimize.
/// @param analysis Analysis manager providing CFG, dominators, loop info, and AA.
/// @return Preserved analysis set describing which analyses remain valid.
PreservedAnalyses LICM::run(Function &function, AnalysisManager &analysis)
{
    auto &domTree = analysis.getFunctionResult<viper::analysis::DomTree>("dominators", function);
    auto &loopInfo = analysis.getFunctionResult<LoopInfo>("loop-info", function);
    auto &aa = analysis.getFunctionResult<viper::analysis::BasicAA>("basic-aa", function);
    auto &cfg = analysis.getFunctionResult<CFGInfo>("cfg", function);

    std::unordered_map<std::string, BasicBlock *> blockLookup;
    blockLookup.reserve(function.blocks.size());
    for (auto &blk : function.blocks)
        blockLookup.emplace(blk.label, &blk);

    bool changed = false;

    for (const Loop &loop : loopInfo.loops())
    {
        BasicBlock *header = lookupBlock(blockLookup, loop.headerLabel);
        if (!header)
            continue;

        BasicBlock *preheader = findPreheader(loop, *header, cfg, blockLookup);
        if (!preheader)
            continue;

        std::unordered_set<unsigned> invariants;
        invariants.reserve(function.params.size() + header->params.size() + 32);
        seedInvariants(loop, function, invariants);

        bool loopHasMod = false;
        std::vector<StoreSite> loopStores;
        for (const auto &label : loop.blockLabels)
        {
            BasicBlock *blk = lookupBlock(blockLookup, label);
            if (!blk)
                continue;
            for (const auto &ins : blk->instructions)
            {
                if (ins.op == Opcode::Store && !ins.operands.empty())
                {
                    loopStores.push_back(
                        {ins.operands[0], viper::analysis::BasicAA::typeSizeBytes(ins.type)});
                    continue;
                }

                if (ins.op == Opcode::Call || ins.op == Opcode::CallIndirect)
                {
                    auto mr = aa.modRef(ins);
                    if (mr == viper::analysis::ModRefResult::Mod ||
                        mr == viper::analysis::ModRefResult::ModRef)
                    {
                        loopHasMod = true;
                        break;
                    }
                }
                auto me = memoryEffects(ins.op);
                if (me == MemoryEffects::Write || me == MemoryEffects::ReadWrite ||
                    me == MemoryEffects::Unknown)
                {
                    loopHasMod = true;
                    break;
                }
            }
            if (loopHasMod)
                break;
        }

        std::vector<BasicBlock *> blockOrder;
        blockOrder.reserve(loop.blockLabels.size());
        collectDominanceOrder(header, loop, domTree, blockOrder);

        for (BasicBlock *block : blockOrder)
        {
            for (std::size_t idx = 0; idx < block->instructions.size();)
            {
                Instr &instr = block->instructions[idx];
                bool allowLoads = true;
                if (instr.op == Opcode::Load)
                {
                    allowLoads = !loopHasMod;
                    if (allowLoads && !instr.operands.empty())
                    {
                        auto loadSize = viper::analysis::BasicAA::typeSizeBytes(instr.type);
                        for (const auto &store : loopStores)
                        {
                            if (aa.alias(instr.operands[0], store.ptr, loadSize, store.size) !=
                                viper::analysis::AliasResult::NoAlias)
                            {
                                allowLoads = false;
                                break;
                            }
                        }
                    }
                }

                if (!isSafeToHoist(instr, allowLoads) || !operandsInvariant(instr, invariants))
                {
                    ++idx;
                    continue;
                }

                Instr hoisted = std::move(instr);
                block->instructions.erase(block->instructions.begin() + idx);

                std::size_t insertIndex = preheader->instructions.size();
                if (preheader->terminated && insertIndex > 0)
                    --insertIndex;
                auto inserted = preheader->instructions.insert(
                    preheader->instructions.begin() + insertIndex, std::move(hoisted));

                Instr &insertedInstr = *inserted;
                if (insertedInstr.result)
                    invariants.insert(*insertedInstr.result);

                changed = true;
            }
        }
    }

    if (!changed)
        return PreservedAnalyses::all();

    PreservedAnalyses preserved;
    preserved.preserveAllModules();
    preserved.preserveFunction("cfg");
    preserved.preserveFunction("dominators");
    preserved.preserveFunction("loop-info");
    return preserved;
}

} // namespace il::transform
