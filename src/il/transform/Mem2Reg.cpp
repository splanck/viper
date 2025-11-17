//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the memory-to-register promotion pass using the "seal and rename"
// algorithm.  Allocations whose addresses do not escape are rewritten into SSA
// temporaries by introducing block parameters that model phi nodes.  The pass
// runs entirely in place, mutating control-flow edges and instruction operands
// while tracking statistics for promoted variables and eliminated loads/stores.
//
//===----------------------------------------------------------------------===//

#include "il/transform/Mem2Reg.hpp"
#include "il/analysis/CFG.hpp"
#include "il/utils/Utils.hpp"
#include <algorithm>
#include <functional>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

using namespace il::core;

namespace viper::passes
{
namespace
{
struct AllocaInfo
{
    BasicBlock *block{nullptr};
    unsigned id{0};
    Type type{};
    bool addressTaken{false};
    bool hasStore{false};
    bool singleBlock{true};
};

struct VarState
{
    Type type{};
    std::unordered_map<BasicBlock *, Value> defs;
};

struct BlockState
{
    bool sealed = false;
    unsigned totalPreds = 0;
    unsigned seenPreds = 0;
    std::unordered_map<unsigned, unsigned> params;
    std::unordered_set<unsigned> incomplete;
};

using AllocaMap = std::unordered_map<unsigned, AllocaInfo>;
using VarMap = std::unordered_map<unsigned, VarState>;
using BlockMap = std::unordered_map<BasicBlock *, BlockState>;
using LabelIndexCache =
    std::unordered_map<const Instr *, std::unordered_map<std::string, std::size_t>>;

/// @brief Gather information about @c alloca instructions within a function.
///
/// @details Performs two sweeps over @p F.  The first collects every alloca
/// result and records its defining block.  The second inspects each use to mark
/// whether the address escapes, whether a store writes to it, and whether all
/// uses stay inside a single block.  The resulting table drives the promotion
/// logic.
///
/// @param F Function to analyze.
/// @return Map from temp ids to their @c AllocaInfo metadata.
static AllocaMap collectAllocas(Function &F)
{
    AllocaMap infos;
    infos.reserve(F.valueNames.size());
    for (auto &B : F.blocks)
        for (auto &I : B.instructions)
            if (I.op == Opcode::Alloca && I.result)
                infos[*I.result] = AllocaInfo{&B, *I.result, Type{}, false, false};

    for (auto &B : F.blocks)
        for (auto &I : B.instructions)
            for (std::size_t oi = 0; oi < I.operands.size(); ++oi)
            {
                Value &Op = I.operands[oi];
                if (Op.kind != Value::Kind::Temp)
                    continue;
                auto it = infos.find(Op.id);
                if (it == infos.end())
                    continue;
                AllocaInfo &AI = it->second;
                if (&B != AI.block)
                    AI.singleBlock = false;
                if (I.op == Opcode::Store && oi == 0)
                {
                    AI.hasStore = true;
                    AI.type = I.type;
                }
                else if (I.op == Opcode::Load && oi == 0)
                {
                    AI.type = I.type;
                }
                else
                {
                    AI.addressTaken = true;
                }
            }
    return infos;
}

/// @brief Ensure that a block parameter exists for a promoted variable.
///
/// @details Looks up the parameter slot assigned to @p varId in block @p B and
/// creates one when missing.  Newly created parameters receive a fresh
/// temporary identifier and are registered in the @p blocks table so future
/// lookups are constant time.
///
/// @param B Block receiving the parameter.
/// @param varId Identifier of the promoted variable.
/// @param vars State map for variables.
/// @param blocks Per-block state including parameter indices.
/// @param nextId Counter used to generate unique temp ids.
/// @return Index of the block parameter.
/// @sideeffect May append to @p B->params and update @p blocks.
static unsigned ensureParam(
    BasicBlock *B, unsigned varId, VarMap &vars, BlockMap &blocks, unsigned &nextId)
{
    BlockState &BS = blocks[B];
    auto it = BS.params.find(varId);
    if (it != BS.params.end())
        return it->second;
    Param p;
    p.id = nextId++;
    p.type = vars[varId].type;
    p.name = "t" + std::to_string(p.id);
    unsigned idx = B->params.size();
    B->params.push_back(p);
    BS.params[varId] = idx;
    return idx;
}

/// @brief Add an incoming value for a block parameter from a predecessor edge.
///
/// @details Extends the predecessor terminator's branch arguments so that the
/// edge targeting @p B forwards @p val in the slot associated with @p varId.
/// If the parameter does not yet exist the helper creates it via
/// @ref ensureParam.
///
/// @param B Destination block that owns the parameter.
/// @param varId Variable identifier for the promoted alloca.
/// @param Pred Predecessor block supplying the value.
/// @param val Value to pass along the edge.
/// @param vars Variable state table.
/// @param blocks Block state table used to lookup parameter indices.
/// @param nextId Counter used when new parameters must be created.
/// @sideeffect Mutates branch arguments in @p Pred and may add block params.
static void addIncoming(BasicBlock *B,
                        unsigned varId,
                        BasicBlock *Pred,
                        const Value &val,
                        VarMap &vars,
                        BlockMap &blocks,
                        unsigned &nextId,
                        LabelIndexCache *idxCache)
{
    unsigned pIdx = ensureParam(B, varId, vars, blocks, nextId);
    Instr &term = Pred->instructions.back();
    std::size_t target = 0;
    if (idxCache && term.labels.size() >= 6)
    {
        auto &map = (*idxCache)[&term];
        if (map.empty())
        {
            for (std::size_t i = 0; i < term.labels.size(); ++i)
                map.emplace(term.labels[i], i);
        }
        auto it = map.find(B->label);
        if (it != map.end())
            target = it->second;
        else
        {
            for (target = 0; target < term.labels.size(); ++target)
                if (term.labels[target] == B->label)
                    break;
        }
    }
    else
    {
        for (target = 0; target < term.labels.size(); ++target)
            if (term.labels[target] == B->label)
                break;
    }
    if (term.brArgs.size() < term.labels.size())
        term.brArgs.resize(term.labels.size());
    auto &args = term.brArgs[target];
    if (args.size() <= pIdx)
        args.resize(pIdx + 1);
    args[pIdx] = val;
}

/// Forward declaration for recursive SSA renaming.
static Value renameUses(Function &F,
                        BasicBlock *B,
                        unsigned varId,
                        VarMap &vars,
                        BlockMap &blocks,
                        unsigned &nextId,
                        const analysis::CFGContext &ctx);

/// @brief Resolve a promoted variable's value at the start of a block.
///
/// @details When @p B has predecessors, the helper ensures a block parameter is
/// present and recursively renames the variable along each incoming edge,
/// wiring the results into the terminator arguments.  For entry blocks with no
/// predecessors, a zero constant of the variable's type is synthesised.
///
/// @param F Function containing the CFG.
/// @param B Block whose incoming value is requested.
/// @param varId Variable identifier.
/// @param vars Variable state table.
/// @param blocks Block state table.
/// @param nextId Counter for generating temp ids.
/// @return SSA value representing the variable at block entry.
/// @sideeffect May mutate the CFG by adding parameters and arguments.
static Value readFromPreds(Function &F,
                           BasicBlock *B,
                           unsigned varId,
                           VarMap &vars,
                           BlockMap &blocks,
                           unsigned &nextId,
                           const analysis::CFGContext &ctx,
                           LabelIndexCache *idxCache)
{
    auto preds = analysis::predecessors(ctx, *B);
    if (preds.empty())
    {
        const Type &ty = vars[varId].type;
        return ty.kind == Type::Kind::F64 ? Value::constFloat(0.0) : Value::constInt(0);
    }
    unsigned pIdx = ensureParam(B, varId, vars, blocks, nextId);
    Value paramVal = Value::temp(B->params[pIdx].id);
    for (auto *P : preds)
    {
        Value arg = renameUses(F, P, varId, vars, blocks, nextId, ctx);
        addIncoming(B, varId, P, arg, vars, blocks, nextId, idxCache);
    }
    return paramVal;
}

/// @brief Determine the SSA value of a promoted variable within a block.
///
/// @details Consults existing definitions recorded in @p vars.  If the block is
/// not yet sealed, the helper creates a placeholder parameter and records the
/// variable as incomplete so it can be finalised once all predecessors are
/// known.  Otherwise, it merges incoming values via @ref readFromPreds.
///
/// @param F Function being rewritten.
/// @param B Current block.
/// @param varId Variable identifier.
/// @param vars Variable state table.
/// @param blocks Block state table indicating seal status.
/// @param nextId Counter for generating temp ids.
/// @return SSA value for the variable within @p B.
/// @sideeffect May add block parameters and update definition maps.
static Value renameUses(Function &F,
                        BasicBlock *B,
                        unsigned varId,
                        VarMap &vars,
                        BlockMap &blocks,
                        unsigned &nextId,
                        const analysis::CFGContext &ctx)
{
    VarState &VS = vars[varId];
    if (auto it = VS.defs.find(B); it != VS.defs.end())
        return it->second;
    BlockState &BS = blocks[B];
    if (!BS.sealed)
    {
        unsigned pIdx = ensureParam(B, varId, vars, blocks, nextId);
        Value v = Value::temp(B->params[pIdx].id);
        VS.defs[B] = v;
        BS.incomplete.insert(varId);
        return v;
    }
    Value v = readFromPreds(F, B, varId, vars, blocks, nextId, ctx, nullptr);
    VS.defs[B] = v;
    return v;
}

/// @brief Finalise a block once all of its predecessors are known.
///
/// @details Completes the SSA value for every variable recorded in the
/// block's @c incomplete set by merging incoming values via @ref readFromPreds
/// and marking the block as sealed.  Subsequent queries can therefore rely on
/// the existing definitions without creating new placeholders.
///
/// @param F Function containing the block.
/// @param B Block to seal.
/// @param vars Variable state table.
/// @param blocks Block state table.
/// @param nextId Counter for generating temp ids.
/// @sideeffect May mutate the CFG with additional parameters and arguments.
static void sealBlocks(Function &F,
                       BasicBlock *B,
                       VarMap &vars,
                       BlockMap &blocks,
                       unsigned &nextId,
                       const analysis::CFGContext &ctx,
                       LabelIndexCache *idxCache)
{
    BlockState &BS = blocks[B];
    if (BS.sealed)
        return;
    for (unsigned varId : BS.incomplete)
    {
        Value v = readFromPreds(F, B, varId, vars, blocks, nextId, ctx, idxCache);
        if (!vars[varId].defs.count(B))
            vars[varId].defs[B] = v;
    }
    BS.incomplete.clear();
    BS.sealed = true;
}

/// @brief Promote eligible allocas within a function to SSA registers.
///
/// @details Executes the seal-and-rename algorithm, deleting loads, stores, and
/// the allocas themselves.  Optional statistics are incremented to record how
/// many variables were promoted and how many memory operations were removed.
///
/// @param F Function to optimize.
/// @param infos Metadata about allocas gathered by @ref collectAllocas.
/// @param stats Optional statistics accumulator.
/// @sideeffect Mutates blocks and instructions in @p F and updates @p stats.
static void promoteVariables(Function &F,
                             const AllocaMap &infos,
                             Mem2RegStats *stats,
                             const analysis::CFGContext &ctx)
{
    VarMap vars;
    vars.reserve(infos.size());
    for (auto &[id, AI] : infos)
    {
        if (AI.addressTaken || !AI.hasStore)
            continue;
        if (AI.type.kind != Type::Kind::I64 && AI.type.kind != Type::Kind::I32 &&
            AI.type.kind != Type::Kind::I16 && AI.type.kind != Type::Kind::F64 &&
            AI.type.kind != Type::Kind::I1)
            continue;
        vars[id] = VarState{AI.type, {}};
    }

    if (stats)
        stats->promotedVars += vars.size();

    if (vars.empty())
        return;

    unsigned nextId = viper::il::nextTempId(F);

    BlockMap blocks;
    blocks.reserve(F.blocks.size());
    for (auto &B : F.blocks)
    {
        BlockState bs;
        bs.totalPreds = analysis::predecessors(ctx, B).size();
        bs.sealed = bs.totalPreds == 0;
        blocks[&B] = bs;
    }

    std::queue<BasicBlock *> work;
    std::unordered_set<BasicBlock *> queued;
    queued.reserve(F.blocks.size());
    if (!F.blocks.empty())
    {
        work.push(&F.blocks.front());
        queued.insert(&F.blocks.front());
    }

    LabelIndexCache idxCache;

    while (!work.empty())
    {
        BasicBlock *B = work.front();
        work.pop();

        for (std::size_t i = 0; i < B->instructions.size();)
        {
            Instr &I = B->instructions[i];
            if (I.op == Opcode::Alloca && I.result && vars.count(*I.result))
            {
                B->instructions.erase(B->instructions.begin() + i);
                continue;
            }
            if (I.op == Opcode::Load && I.operands.size() &&
                I.operands[0].kind == Value::Kind::Temp && vars.count(I.operands[0].id))
            {
                unsigned varId = I.operands[0].id;
                Value v = renameUses(F, B, varId, vars, blocks, nextId, ctx);
                if (I.result)
                    viper::il::replaceAllUses(F, *I.result, v);
                B->instructions.erase(B->instructions.begin() + i);
                if (stats)
                    stats->removedLoads++;
                continue;
            }
            if (I.op == Opcode::Store && I.operands.size() > 1 &&
                I.operands[0].kind == Value::Kind::Temp && vars.count(I.operands[0].id))
            {
                unsigned varId = I.operands[0].id;
                vars[varId].defs[B] = I.operands[1];
                B->instructions.erase(B->instructions.begin() + i);
                if (stats)
                    stats->removedStores++;
                continue;
            }
            ++i;
        }

        auto succs = analysis::successors(ctx, *B);
        for (auto *S : succs)
        {
            BlockState &SS = blocks[S];
            SS.seenPreds++;
            if (!queued.count(S))
            {
                work.push(S);
                queued.insert(S);
            }
            if (SS.seenPreds == SS.totalPreds)
                sealBlocks(F, S, vars, blocks, nextId, ctx, &idxCache);
        }
    }
}

} // namespace

/// @brief Run memory-to-register promotion across all functions in a module.
///
/// @details Scans each function for promotable allocas, filters out variables
/// whose addresses escape or whose element types are unsupported, and then
/// invokes @ref promoteVariables to perform the transformation.  When provided,
/// @p stats accumulates totals for promoted variables and removed memory
/// operations.
///
/// @param M Module to transform.
/// @param stats Optional statistics collector receiving totals for promoted
///              variables and removed loads/stores.
/// @sideeffect Mutates functions within the module.
void mem2reg(Module &M, Mem2RegStats *stats)
{
    analysis::CFGContext cfg(M);
    for (auto &F : M.functions)
    {
        AllocaMap infos = collectAllocas(F);
        AllocaMap promotable;
        for (auto &[id, info] : infos)
        {
            if (info.addressTaken || !info.hasStore)
                continue;
            if (info.type.kind != Type::Kind::I64 && info.type.kind != Type::Kind::I32 &&
                info.type.kind != Type::Kind::I16 && info.type.kind != Type::Kind::F64 &&
                info.type.kind != Type::Kind::I1)
                continue;
            promotable.emplace(id, info);
        }
        promoteVariables(F, promotable, stats, cfg);
    }
}

} // namespace viper::passes
