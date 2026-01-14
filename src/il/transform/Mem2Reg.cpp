//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
// KNOWN LIMITATIONS:
//
// 1. Only entry-block allocas are promoted. Allocas inside loops represent
//    different storage on each iteration and cannot be safely promoted.
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
#include <vector>

using namespace il::core;

namespace viper::passes
{
namespace
{
constexpr unsigned kMaxSROAFields = 8;
constexpr unsigned kMaxSROAAllocaSize = 128;

struct SROAField
{
    Type type{};
    unsigned size = 0;
    unsigned allocaId = 0;
};

struct SROACandidate
{
    BasicBlock *block = nullptr;
    std::size_t allocaIndex = 0;
    unsigned baseId = 0;
    unsigned allocSize = 0;
    bool ok = false;
    std::unordered_map<unsigned, unsigned> offsets; // temp id -> byte offset (includes base)
    std::unordered_map<unsigned, SROAField> fields; // offset -> field info
};

static bool isPromotableScalarType(const Type &type)
{
    switch (type.kind)
    {
        case Type::Kind::I1:
        case Type::Kind::I16:
        case Type::Kind::I32:
        case Type::Kind::I64:
        case Type::Kind::F64:
            return true;
        default:
            return false;
    }
}

static std::optional<unsigned> scalarSize(const Type &type)
{
    switch (type.kind)
    {
        case Type::Kind::I1:
            return 1;
        case Type::Kind::I16:
            return 2;
        case Type::Kind::I32:
            return 4;
        case Type::Kind::I64:
        case Type::Kind::F64:
            return 8;
        default:
            return std::nullopt;
    }
}

static std::optional<unsigned> constOffset(const Value &v)
{
    if (v.kind != Value::Kind::ConstInt || v.i64 < 0)
        return std::nullopt;
    return static_cast<unsigned>(v.i64);
}

static void ensureValueName(Function &F, unsigned id, const std::string &name)
{
    if (name.empty())
        return;
    if (F.valueNames.size() <= id)
        F.valueNames.resize(id + 1);
    F.valueNames[id] = name;
}

struct AllocaInfo
{
    BasicBlock *block{nullptr};
    unsigned id{0};
    Type type{};
    bool addressTaken{false};
    bool hasStore{false};
    bool singleBlock{true};
    bool typeConsistent{true}; ///< False if loads/stores use different types.
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
    // Only collect allocas from the entry block. Allocas inside loops
    // represent different storage on each iteration and cannot be promoted.
    if (!F.blocks.empty())
    {
        BasicBlock &entry = F.blocks.front();
        for (auto &I : entry.instructions)
            if (I.op == Opcode::Alloca && I.result)
                infos[*I.result] = AllocaInfo{&entry, *I.result, Type{}, false, false};
    }

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
                    // Check type consistency: if type was already set and differs, mark inconsistent
                    if (AI.type.kind != Type::Kind::Void && AI.type.kind != I.type.kind)
                        AI.typeConsistent = false;
                    AI.type = I.type;
                }
                else if (I.op == Opcode::Load && oi == 0)
                {
                    // Check type consistency: if type was already set and differs, mark inconsistent
                    if (AI.type.kind != Type::Kind::Void && AI.type.kind != I.type.kind)
                        AI.typeConsistent = false;
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
    // Create a placeholder param BEFORE recursing to break cycles.
    // This is essential for handling loops correctly.
    unsigned pIdx = ensureParam(B, varId, vars, blocks, nextId);
    Value placeholder = Value::temp(B->params[pIdx].id);
    VS.defs[B] = placeholder;
    Value v = readFromPreds(F, B, varId, vars, blocks, nextId, ctx, nullptr);
    // The placeholder is the correct value (the block param that will receive
    // incoming values from predecessors), so we don't need to update VS.defs[B].
    return placeholder;
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
        if (!vars[varId].defs.contains(B))
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
        if (AI.addressTaken || !AI.hasStore || !AI.typeConsistent)
            continue;
        if (!isPromotableScalarType(AI.type))
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
            if (I.op == Opcode::Alloca && I.result && vars.contains(*I.result))
            {
                B->instructions.erase(B->instructions.begin() + i);
                continue;
            }
            if (I.op == Opcode::Load && I.operands.size() &&
                I.operands[0].kind == Value::Kind::Temp && vars.contains(I.operands[0].id))
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
                I.operands[0].kind == Value::Kind::Temp && vars.contains(I.operands[0].id))
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
            if (!queued.contains(S))
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

static bool runSROA(Function &F)
{
    std::unordered_map<unsigned, SROACandidate> candidates;
    std::unordered_map<unsigned, unsigned> owner; // temp -> base alloca id

    for (auto &B : F.blocks)
    {
        for (std::size_t idx = 0; idx < B.instructions.size(); ++idx)
        {
            Instr &I = B.instructions[idx];
            if (I.op != Opcode::Alloca || !I.result || I.operands.empty())
                continue;

            auto sizeOpt = constOffset(I.operands[0]);
            if (!sizeOpt || *sizeOpt == 0 || *sizeOpt > kMaxSROAAllocaSize)
                continue;

            SROACandidate cand;
            cand.block = &B;
            cand.allocaIndex = idx;
            cand.baseId = *I.result;
            cand.allocSize = *sizeOpt;
            cand.ok = true;
            cand.offsets.emplace(*I.result, 0);

            candidates.emplace(*I.result, std::move(cand));
            owner.emplace(*I.result, *I.result);
        }
    }

    if (candidates.empty())
        return false;

    for (auto &B : F.blocks)
    {
        for (auto &I : B.instructions)
        {
            if (I.op == Opcode::GEP && I.operands.size() >= 2 &&
                I.operands[0].kind == Value::Kind::Temp)
            {
                auto ownIt = owner.find(I.operands[0].id);
                if (ownIt != owner.end())
                {
                    auto candIt = candidates.find(ownIt->second);
                    if (candIt != candidates.end() && candIt->second.ok)
                    {
                        auto offOpt = constOffset(I.operands[1]);
                        if (!offOpt || !I.result)
                        {
                            candIt->second.ok = false;
                        }
                        else
                        {
                            // Get the base offset of the source operand to handle chained GEPs
                            // e.g., gep %3, %2, 4 where %2 = gep %1, 8 should have offset 8+4=12
                            auto baseOffIt = candIt->second.offsets.find(I.operands[0].id);
                            unsigned baseOffset =
                                (baseOffIt != candIt->second.offsets.end()) ? baseOffIt->second : 0;
                            unsigned totalOffset = baseOffset + *offOpt;
                            if (totalOffset > candIt->second.allocSize)
                            {
                                candIt->second.ok = false;
                            }
                            else
                            {
                                owner[*I.result] = candIt->second.baseId;
                                candIt->second.offsets[*I.result] = totalOffset;
                            }
                        }
                    }
                }
            }

            auto classifyUse = [&](const Value &v, Instr &Inst, std::size_t operandIdx)
            {
                if (v.kind != Value::Kind::Temp)
                    return;
                auto ownIt = owner.find(v.id);
                if (ownIt == owner.end())
                    return;
                auto candIt = candidates.find(ownIt->second);
                if (candIt == candidates.end())
                    return;
                SROACandidate &cand = candIt->second;
                if (!cand.ok)
                    return;

                if (Inst.op == Opcode::Load || Inst.op == Opcode::Store)
                {
                    if (Inst.operands.empty() || operandIdx != 0)
                    {
                        cand.ok = false;
                        return;
                    }
                    auto offIt = cand.offsets.find(v.id);
                    if (offIt == cand.offsets.end())
                    {
                        cand.ok = false;
                        return;
                    }
                    Type accessType = Inst.type;
                    if (!isPromotableScalarType(accessType))
                    {
                        cand.ok = false;
                        return;
                    }
                    auto szOpt = scalarSize(accessType);
                    if (!szOpt || offIt->second + *szOpt > cand.allocSize)
                    {
                        cand.ok = false;
                        return;
                    }

                    SROAField &field = cand.fields[offIt->second];
                    if (field.size == 0)
                    {
                        field.type = accessType;
                        field.size = *szOpt;
                    }
                    else if (field.type.kind != accessType.kind)
                    {
                        cand.ok = false;
                    }
                    return;
                }

                if (Inst.op == Opcode::GEP && operandIdx == 0)
                    return;

                cand.ok = false;
            };

            for (std::size_t oi = 0; oi < I.operands.size(); ++oi)
                classifyUse(I.operands[oi], I, oi);

            for (auto &argList : I.brArgs)
                for (const auto &arg : argList)
                    classifyUse(arg, I, 0);
        }
    }

    for (auto &[id, cand] : candidates)
    {
        if (!cand.ok || cand.fields.empty() || cand.fields.size() > kMaxSROAFields)
        {
            cand.ok = false;
            continue;
        }

        std::vector<std::pair<unsigned, SROAField *>> ordered;
        ordered.reserve(cand.fields.size());
        for (auto &[off, field] : cand.fields)
            ordered.emplace_back(off, &field);
        std::sort(ordered.begin(),
                  ordered.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });

        unsigned end = 0;
        for (const auto &[off, field] : ordered)
        {
            if (field->size == 0 || off < end || off + field->size > cand.allocSize)
            {
                cand.ok = false;
                break;
            }
            end = off + field->size;
        }
    }

    bool changed = false;
    unsigned nextId = viper::il::nextTempId(F);

    for (auto &[id, cand] : candidates)
    {
        if (!cand.ok)
            continue;

        BasicBlock &B = *cand.block;
        auto findAllocaIndex = [&]() -> std::size_t
        {
            for (std::size_t i = 0; i < B.instructions.size(); ++i)
            {
                Instr &I = B.instructions[i];
                if (I.op == Opcode::Alloca && I.result && *I.result == cand.baseId)
                    return i;
            }
            return B.instructions.size();
        };

        std::size_t insertPos = findAllocaIndex();
        if (insertPos == B.instructions.size())
            continue;

        std::unordered_map<unsigned, unsigned> offsetToAlloca;
        offsetToAlloca.reserve(cand.fields.size());

        std::size_t fieldIdx = 0;
        for (auto &entry : cand.fields)
        {
            Instr alloc;
            alloc.op = Opcode::Alloca;
            alloc.type = Type(Type::Kind::Ptr);
            alloc.result = nextId;
            alloc.operands.push_back(Value::constInt(entry.second.size));
            offsetToAlloca[entry.first] = nextId;
            entry.second.allocaId = nextId;

            std::string baseName;
            if (cand.baseId < F.valueNames.size())
                baseName = F.valueNames[cand.baseId];
            if (baseName.empty())
                baseName = "sroa." + std::to_string(cand.baseId);
            ensureValueName(F, nextId, baseName + ".f" + std::to_string(fieldIdx++));

            B.instructions.insert(B.instructions.begin() + static_cast<long>(insertPos),
                                  std::move(alloc));
            ++insertPos;
            ++nextId;
        }

        for (auto &Blk : F.blocks)
        {
            for (auto &I : Blk.instructions)
            {
                if ((I.op != Opcode::Load && I.op != Opcode::Store) || I.operands.empty())
                    continue;

                if (I.operands[0].kind != Value::Kind::Temp)
                    continue;

                auto offIt = cand.offsets.find(I.operands[0].id);
                if (offIt == cand.offsets.end())
                    continue;

                auto fieldIt = offsetToAlloca.find(offIt->second);
                if (fieldIt == offsetToAlloca.end())
                    continue;

                I.operands[0] = Value::temp(fieldIt->second);
            }
        }

        for (auto &Blk : F.blocks)
        {
            for (std::size_t i = 0; i < Blk.instructions.size();)
            {
                Instr &I = Blk.instructions[i];
                bool erase = false;

                if (I.op == Opcode::GEP && I.result && cand.offsets.contains(*I.result) &&
                    *I.result != cand.baseId)
                {
                    erase = true;
                }
                else if (I.op == Opcode::Alloca && I.result && *I.result == cand.baseId)
                {
                    erase = true;
                }

                if (erase)
                {
                    Blk.instructions.erase(Blk.instructions.begin() + static_cast<long>(i));
                    changed = true;
                    continue;
                }
                ++i;
            }
        }
    }

    return changed;
}

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
        runSROA(F);

        AllocaMap infos = collectAllocas(F);
        AllocaMap promotable;
        for (auto &[id, info] : infos)
        {
            if (info.addressTaken || !info.hasStore || !info.typeConsistent)
                continue;
            if (!isPromotableScalarType(info.type))
                continue;
            promotable.emplace(id, info);
        }
        promoteVariables(F, promotable, stats, cfg);
    }
}

} // namespace viper::passes
